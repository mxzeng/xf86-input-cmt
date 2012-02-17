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

/* Number of events to attempt to read from kernel on each SIGIO */
#define NUM_EVENTS          16

/* Number of buttons and axes to define on X Input device. */
#define CMT_NUM_BUTTONS     7
#define CMT_NUM_AXES        6

#ifndef AXIS_LABEL_PROP_ABS_START_TIME
#define AXIS_LABEL_PROP_ABS_START_TIME      "Abs Start Timestamp"
#endif
#ifndef AXIS_LABEL_PROP_ABS_END_TIME
#define AXIS_LABEL_PROP_ABS_END_TIME      "Abs End Timestamp"
#endif

/**
 * Forward declarations
 */
static InputInfoPtr PreInit(InputDriverPtr, IDevPtr, int);
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

/*
 *  For ABI <12, module loader calls old-style PreInit().
 *     old-style PreInit() then calls new-style NewPreInit().
 *  For ABI >=12, module loader calls new-style PreInit() directly.
 */
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 12
static int
NewPreInit(InputDriverPtr drv, InputInfoPtr info, int flags);

static InputInfoPtr
PreInit(InputDriverPtr drv, IDevPtr idev, int flags)
{
    InputInfoPtr info;

    /* Allocate a new InputInfoRec and add it to the head xf86InputDevs. */
    info = xf86AllocateInput(drv, 0);
    if (!info) {
        return NULL;
    }

    /* initialize the InputInfoRec */
    info->name                    = idev->identifier;
    info->reverse_conversion_proc = NULL;
    info->dev                     = NULL;
    info->private_flags           = 0;
    info->flags                   = XI86_SEND_DRAG_EVENTS;
    info->conf_idev               = idev;
    info->always_core_feedback    = 0;

    DBG(info, "PreInit\n");

    xf86CollectInputOptions(info, NULL, NULL);

    if (NewPreInit(drv, info, flags) != Success)
        return NULL;

    info->flags |= XI86_CONFIGURED;

    return info;
}

static int
NewPreInit(InputDriverPtr drv, InputInfoPtr info, int flags)
#else
static int
PreInit(InputDriverPtr drv, InputInfoPtr info, int flags)
#endif
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

    rc = Gesture_Init(&cmt->gesture);
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

    do {
        len = read(info->fd, &ev, sizeof(ev));
        if (len <= 0) {
            if (errno == ENODEV) { /* May happen after resume */
                xf86RemoveEnabledDevice(info);
                close(info->fd);
                info->fd = -1;
            } else if (errno != EAGAIN) {
                xf86IDrvMsg(info, X_ERROR, "Read error: %s\n", strerror(errno));
            }
            break;
        }

        /* kernel always delivers complete events, so len must be sizeof *ev */
        if (len % sizeof(*ev)) {
            xf86IDrvMsg(info, X_ERROR, "Read error: %s\n", strerror(errno));
            break;
        }

        /* Process events ... */
        for (i = 0; i < len/sizeof(ev[0]); i++)
            Event_Process(info, &ev[i]);

    } while (len == sizeof(ev));
    /* Keep reading if kernel supplied NUM_EVENTS events. */
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
            xf86IDrvMsg(info, X_ERROR, "No Device specified.\n");
            return BadValue;
        }
        xf86IDrvMsg(info, X_CONFIG, "Opening Device: \"%s\"\n", cmt->device);
    }

    if (info->fd < 0) {
        do {
            info->fd = open(cmt->device, O_RDWR | O_NONBLOCK, 0);
        } while (info->fd < 0 && errno == EINTR);

        if (info->fd < 0) {
            xf86IDrvMsg(info, X_ERROR, "Cannot open \"%s\".\n", cmt->device);
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


static int
InitializeXDevice(DeviceIntPtr dev)
{
    CARD8 map[CMT_NUM_BUTTONS + 1];
    int i;
    Atom btn_labels[CMT_NUM_BUTTONS] = { 0 };
    Atom axes_labels[CMT_NUM_AXES] = { 0 };

    axes_labels[0] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_X);
    axes_labels[1] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_Y);
    axes_labels[2] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_START_TIME);
    if (!axes_labels[2])
        axes_labels[2] = MakeAtom(AXIS_LABEL_PROP_ABS_START_TIME,
                                  strlen(AXIS_LABEL_PROP_ABS_START_TIME),
                                  TRUE);
    axes_labels[3] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_END_TIME);
    if (!axes_labels[3])
        axes_labels[3] = MakeAtom(AXIS_LABEL_PROP_ABS_END_TIME,
                                  strlen(AXIS_LABEL_PROP_ABS_END_TIME),
                                  TRUE);
    axes_labels[4] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_WHEEL);
    axes_labels[5] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_HWHEEL);

    btn_labels[0] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_LEFT);
    btn_labels[1] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_MIDDLE);
    btn_labels[2] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_RIGHT);
    btn_labels[3] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_UP);
    btn_labels[4] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_DOWN);
    btn_labels[5] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_LEFT);
    btn_labels[6] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_RIGHT);

    /* TODO: Prop to adjust button mapping */
    /* Note: Must initialize entire (CMT_NUM_BUTTONS + 1)-length map */
    for (i = 0; i <= CMT_NUM_BUTTONS; i++)
        map[i] = i;

    InitPointerDeviceStruct((DevicePtr)dev, map, CMT_NUM_BUTTONS, btn_labels,
                            PointerCtrl, GetMotionHistorySize(), CMT_NUM_AXES,
                            axes_labels);

    for (i=0; i < CMT_NUM_AXES; i++) {
        xf86InitValuatorAxisStruct(dev, i, axes_labels[i], -1, -1, 1, 0, 1);
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
