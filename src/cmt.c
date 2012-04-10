/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cmt.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <exevents.h>
#include <X11/extensions/XI.h>
#include <X11/Xatom.h>
#include <xf86Xinput.h>
#include <xf86_OSproc.h>
#include <xserver-properties.h>

#include "event.h"
#include "properties.h"

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 12
#error Unsupported XInput version. Major version 12 and above required.
#endif

/* Number of events to attempt to read from kernel on each SIGIO */
#define NUM_EVENTS          16

#ifndef AXIS_LABEL_PROP_ABS_START_TIME
#define AXIS_LABEL_PROP_ABS_START_TIME     "Abs Start Timestamp"
#endif
#ifndef AXIS_LABEL_PROP_ABS_END_TIME
#define AXIS_LABEL_PROP_ABS_END_TIME       "Abs End Timestamp"
#endif

#define AXIS_LABEL_PROP_ABS_FLING_VX       "Abs Fling X Velocity"
#define AXIS_LABEL_PROP_ABS_FLING_VY       "Abs Fling Y Velocity"
#define AXIS_LABEL_PROP_ABS_FLING_STATE    "Abs Fling State"

#define AXIS_LABEL_PROP_ABS_DBL_START_TIME "Abs Dbl Start Timestamp"
#define AXIS_LABEL_PROP_ABS_DBL_END_TIME   "Abs Dbl End Timestamp"
#define AXIS_LABEL_PROP_ABS_DBL_FLING_VX   "Abs Dbl Fling X Velocity"
#define AXIS_LABEL_PROP_ABS_DBL_FLING_VY   "Abs Dbl Fling Y Velocity"
/**
 * Forward declarations
 */
static int PreInit(InputDriverPtr, InputInfoPtr, int);
static void UnInit(InputDriverPtr, InputInfoPtr, int);

static pointer Plug(pointer, pointer, int*, int*);
static void Unplug(pointer);

static int DeviceControl(DeviceIntPtr, int);
static void ReadInput(InputInfoPtr);

static Bool DeviceInit(DeviceIntPtr);
static Bool DeviceOn(DeviceIntPtr);
static Bool DeviceOff(DeviceIntPtr);
static Bool DeviceClose(DeviceIntPtr);

static Bool OpenDevice(InputInfoPtr);
static int InitializeXDevice(DeviceIntPtr dev);

/**
 * X Input driver information and PreInit / UnInit routines
 */
_X_EXPORT InputDriverRec CMT = {
    1,
    "cmt",
    NULL,
    PreInit,
    UnInit,
    NULL,
    0
};

static int
PreInit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
    CmtDevicePtr cmt;
    int rc;

    DBG(info, "NewPreInit\n");

    cmt = calloc(1, sizeof(*cmt));
    if (!cmt)
        return BadAlloc;

    info->type_name               = XI_TOUCHPAD;
    info->device_control          = DeviceControl;
    info->read_input              = ReadInput;
    info->control_proc            = NULL;
    info->switch_mode             = NULL;  /* Only support Absolute mode */
    info->private                 = cmt;
    info->fd                      = -1;

    rc = OpenDevice(info);
    if (rc != Success)
        goto Error_OpenDevice;

    rc = Event_Init(info);
    if (rc != Success)
        goto Error_Event_Init;

    xf86ProcessCommonOptions(info, info->options);

    if (info->fd >= 0) {
        close(info->fd);
        info->fd = -1;
    }

    rc = Gesture_Init(&cmt->gesture, Event_Get_Slot_Count(info));
    if (rc != Success)
        goto Error_Gesture_Init;

    return Success;

Error_Gesture_Init:
    Event_Free(info);
Error_Event_Init:
    if (info->fd >= 0) {
        close(info->fd);
        info->fd = -1;
    }
Error_OpenDevice:
    free(cmt);
    info->private = NULL;
    return rc;
}

static void
UnInit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
    CmtDevicePtr cmt;

    if (!info)
        return;

    DBG(info, "UnInit\n");

    cmt = info->private;
    if (cmt) {
        Gesture_Free(&cmt->gesture);
        free(cmt->device);
        cmt->device = NULL;
        Event_Free(info);
        free(cmt);
        info->private = NULL;
    }
    xf86DeleteInput(info, flags);
}

/**
 * X input driver entry points
 */
static Bool
DeviceControl(DeviceIntPtr dev, int mode)
{
    switch (mode) {
    case DEVICE_INIT:
        return DeviceInit(dev);

    case DEVICE_ON:
        return DeviceOn(dev);

    case DEVICE_OFF:
        return DeviceOff(dev);

    case DEVICE_CLOSE:
        return DeviceClose(dev);
    }

    return BadValue;
}

static void
ReadInput(InputInfoPtr info)
{
    struct input_event ev[NUM_EVENTS];
    int i;
    int len;
    Bool sync_evdev_state = FALSE;
    CmtDevicePtr cmt = info->private;

    do {
        len = read(info->fd, &ev, sizeof(ev));
        if (len <= 0) {
            if (errno == ENODEV) { /* May happen after resume */
                xf86RemoveEnabledDevice(info);
                close(info->fd);
                info->fd = -1;
            } else if (errno != EAGAIN) {
                ERR(info, "Read error: %s\n", strerror(errno));
            }
            break;
        }

        /* kernel always delivers complete events, so len must be sizeof *ev */
        if (len % sizeof(*ev)) {
            ERR(info, "Read error: %s\n", strerror(errno));
            break;
        }

        /* Process events ... */
        for (i = 0; i < len/sizeof(ev[0]); i++) {
            if (sync_evdev_state)
                break;
            if (timercmp(&ev[i].time, &cmt->before_sync_time, <)) {
                /* Ignore events before last sync time */
                continue;
            } else if (timercmp(&ev[i].time, &cmt->after_sync_time, >)) {
                /* Event_Process returns TRUE if SYN_DROPPED detected */
                sync_evdev_state = Event_Process(info, &ev[i]);
            } else {
                /* If the event occurred during sync, then sync again */
                sync_evdev_state = TRUE;
            }
        }

    } while (len == sizeof(ev));
    /* Keep reading if kernel supplied NUM_EVENTS events. */

    if (sync_evdev_state)
        Event_Sync_State(info);
}

/**
 * device control event handlers
 */
static Bool
DeviceInit(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    int rc;

    DBG(info, "DeviceInit\n");

    InitializeXDevice(dev);
    dev->public.on = FALSE;

    rc = PropertiesInit(dev);
    if (rc != Success)
        return rc;

    Gesture_Device_Init(&cmt->gesture, dev);

    return Success;
}

static Bool
DeviceOn(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    int rc;

    DBG(info, "DeviceOn\n");

    rc = OpenDevice(info);
    if (rc != Success)
        return rc;
    Event_Open(info);

    xf86FlushInput(info->fd);
    xf86AddEnabledDevice(info);
    dev->public.on = TRUE;
    Gesture_Device_On(&cmt->gesture);
    return Success;
}

static Bool
DeviceOff(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    DBG(info, "DeviceOff\n");

    dev->public.on = FALSE;
    Gesture_Device_Off(&cmt->gesture);
    if (info->fd != -1) {
        xf86RemoveEnabledDevice(info);
        close(info->fd);
        info->fd = -1;
    }
    return Success;
}

static Bool
DeviceClose(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    DBG(info, "DeviceClose\n");

    DeviceOff(dev);
    Gesture_Device_Close(&cmt->gesture);
    PropertiesClose(dev);
    return Success;
}


/**
 * Open Device Node
 */
static Bool
OpenDevice(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;

    if (!cmt->device) {
        cmt->device = xf86CheckStrOption(info->options, "Device", NULL);
        if (!cmt->device) {
            ERR(info, "No Device specified.\n");
            return BadValue;
        }
        xf86IDrvMsg(info, X_CONFIG, "Opening Device: \"%s\"\n", cmt->device);
    }

    if (info->fd < 0) {
        do {
            info->fd = open(cmt->device, O_RDWR | O_NONBLOCK, 0);
        } while (info->fd < 0 && errno == EINTR);

        if (info->fd < 0) {
            ERR(info, "Cannot open \"%s\".\n", cmt->device);
            return BadValue;
        }
    }

    return Success;
}


/**
 * Setup X Input Device Classes
 */

/*
 *  Alter the control parameters for the mouse. Note that all special
 *  protocol values are handled by dix.
 */
static void
PointerCtrl(DeviceIntPtr device, PtrCtrl *ctrl)
{
}

static Atom
InitAtom(const char* name)
{
    Atom atom = XIGetKnownProperty(name);
    if (!atom)
        atom = MakeAtom(name, strlen(name), TRUE);
    return atom;
}

static int
InitializeXDevice(DeviceIntPtr dev)
{
    static const char* axes_names[CMT_NUM_AXES] = {
        AXIS_LABEL_PROP_REL_X,
        AXIS_LABEL_PROP_REL_Y,
        AXIS_LABEL_PROP_REL_HWHEEL,
        AXIS_LABEL_PROP_REL_WHEEL,
        AXIS_LABEL_PROP_ABS_FLING_VX,
        AXIS_LABEL_PROP_ABS_FLING_VY,
        AXIS_LABEL_PROP_ABS_FLING_STATE,
        AXIS_LABEL_PROP_ABS_START_TIME,
        AXIS_LABEL_PROP_ABS_END_TIME,
        AXIS_LABEL_PROP_ABS_DBL_FLING_VX,
        AXIS_LABEL_PROP_ABS_DBL_FLING_VY,
        AXIS_LABEL_PROP_ABS_DBL_START_TIME,
        AXIS_LABEL_PROP_ABS_DBL_END_TIME
    };
    static const char* btn_names[CMT_NUM_BUTTONS] = {
        BTN_LABEL_PROP_BTN_LEFT,
        BTN_LABEL_PROP_BTN_MIDDLE,
        BTN_LABEL_PROP_BTN_RIGHT,
        BTN_LABEL_PROP_BTN_BACK,
        BTN_LABEL_PROP_BTN_FORWARD,
    };

    Atom axes_labels[CMT_NUM_AXES] = { 0 };
    Atom btn_labels[CMT_NUM_BUTTONS] = { 0 };
    /* Map our button numbers to standard ones. */
    CARD8 map[CMT_NUM_BUTTONS + 1] = {
        0,  /* Ignored */
        1,
        2,
        3,
        8,  /* Back */
        9   /* Forward */
    };
    int i;

    /* TODO: Prop to adjust button mapping */
    for (i = 0; i < CMT_NUM_BUTTONS; i++)
        btn_labels[i] = XIGetKnownProperty(btn_names[i]);

    for (i = 0; i < CMT_NUM_AXES; i++)
        axes_labels[i] = InitAtom(axes_names[i]);

    InitPointerDeviceStruct((DevicePtr)dev,
                            map,
                            CMT_NUM_BUTTONS, btn_labels,
                            PointerCtrl,
                            GetMotionHistorySize(),
                            CMT_NUM_AXES, axes_labels);

    for (i = 0; i < CMT_NUM_AXES; i++) {
        int mode = (i == CMT_AXIS_X || i == CMT_AXIS_Y) ? Relative : Absolute;
        xf86InitValuatorAxisStruct(
            dev, i, axes_labels[i], -1, -1, 1, 0, 1, mode);
        xf86InitValuatorDefaults(dev, i);
    }

    return Success;
}


/**
 * X module information and plug / unplug routines
 */
static XF86ModuleVersionInfo versionRec =
{
    "cmt",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData cmtModuleData =
{
    &versionRec,
    Plug,
    Unplug
};


static pointer
Plug(pointer module, pointer options, int* errmaj, int* errmin)
{
    xf86AddInputDriver(&CMT, module, 0);
    return module;
}

static void
Unplug(pointer p)
{
}
