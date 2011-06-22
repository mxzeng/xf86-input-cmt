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

#include <linux/input.h>

#include <exevents.h>
#include <X11/extensions/XI.h>
#include <X11/Xatom.h>
#include <xf86.h>
#include <xf86Xinput.h>
#include <xf86_OSproc.h>
#include <xorg-server.h>
#include <xserver-properties.h>

#include "properties.h"
#include "event.h"

/* Number of events to attempt to read from kernel on each SIGIO */
#define NUM_EVENTS          16

/* Number of buttons to define on X Input device. */
#define CMT_NUM_BUTTONS     7

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
static int IdentifyDevice(InputInfoPtr);
static int InitializeXDevice(DeviceIntPtr dev);


/**
 * Helper functions
 */
static inline Bool
TestBit(int bit, unsigned long* array)
{
    return array[bit / LONG_BITS] & (1L << (bit % LONG_BITS));
}


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

    xf86IDrvMsg(info, X_INFO, "PreInit\n");

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

    xf86IDrvMsg(info, X_INFO, "NewPreInit\n");

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
        goto PreInit_error;

    ProcessConfOptions(info, info->options);

    if (IdentifyDevice(info) != Success) {
        rc = BadMatch;
        goto PreInit_error;
    }

    xf86ProcessCommonOptions(info, info->options);

    if (info->fd != -1) {
        close(info->fd);
        info->fd = -1;
    }

    return Success;

PreInit_error:
    if (info->fd >= 0)
        close(info->fd);
    info->fd = -1;
    return rc;
}

static void
UnInit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
    CmtDevicePtr cmt;

    if (!info)
        return;

    xf86IDrvMsg(info, X_INFO, "UnInit\n");

    cmt = info->private;
    if (cmt) {
        free(cmt->device);
        cmt->device = NULL;
        free(cmt);
    }
    info->private = NULL;
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
                /* Use X_NONE to avoid alloc */
                xf86MsgVerb(X_NONE, 0, "%s: Read error: %s\n", info->name,
                            strerror(errno));
            }
            break;
        }

        /* kernel always delivers complete events, so len must be sizeof *ev */
        if (len % sizeof(*ev)) {
            /* Use X_NONE to avoid alloc */
            xf86MsgVerb(X_NONE, 0, "%s: Read error: %s\n", info->name,
                        strerror(errno));
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

    xf86IDrvMsg(info, X_INFO, "DeviceInit\n");

    InitializeXDevice(dev);
    dev->public.on = FALSE;
    return PropertyInit(dev);
}

static Bool
DeviceOn(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    int rc;

    xf86IDrvMsg(info, X_INFO, "DeviceOn\n");

    rc = OpenDevice(info);
    if (rc != Success)
        return rc;

    xf86FlushInput(info->fd);
    xf86AddEnabledDevice(info);
    dev->public.on = TRUE;
    return Success;
}

static Bool
DeviceOff(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;

    xf86IDrvMsg(info, X_INFO, "DeviceOff\n");

    if (info->fd != -1) {
        xf86RemoveEnabledDevice(info);
        close(info->fd);
        info->fd = -1;
    }
    dev->public.on = FALSE;
    return Success;
}

static Bool
DeviceClose(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;

    xf86IDrvMsg(info, X_INFO, "DeviceClose\n");

    DeviceOff(dev);
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


static int
IdentifyDevice(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    int i;
    int len;

    if (ioctl(info->fd, EVIOCGID, &cmt->id) < 0) {
         xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGID failed: %s\n",
                     strerror(errno));
         return !Success;
    }
    xf86IDrvMsg(info, X_INFO, "vendor: %02X, product: %02X\n", cmt->id.vendor,
                cmt->id.product);

    if (ioctl(info->fd, EVIOCGNAME(sizeof(cmt->name) - 1), cmt->name) < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGNAME failed: %s\n",
                    strerror(errno));
        return !Success;
    }
    xf86IDrvMsg(info, X_INFO, "name: %s\n", cmt->name);

    len = ioctl(info->fd, EVIOCGBIT(0, sizeof(cmt->bitmask)), cmt->bitmask);
    if (len < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGBIT failed: %s\n",
                    strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->bitmask))
            xf86IDrvMsg(info, X_INFO, "Has Event %d\n", i);
    }

    len = ioctl(info->fd, EVIOCGBIT(EV_KEY, sizeof(cmt->key_bitmask)),
                cmt->key_bitmask);
    if (len < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGBIT(EV_KEY) failed: %s\n",
                    strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->key_bitmask))
            xf86IDrvMsg(info, X_INFO, "Has KEY %d\n", i);
    }

    len = ioctl(info->fd, EVIOCGBIT(EV_LED, sizeof(cmt->led_bitmask)),
                cmt->led_bitmask);
    if (len < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGBIT(EV_LED) failed: %s\n",
                    strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->led_bitmask))
            xf86IDrvMsg(info, X_INFO, "Has LED %d\n", i);
    }

    len = ioctl(info->fd, EVIOCGBIT(EV_REL, sizeof(cmt->rel_bitmask)),
                cmt->rel_bitmask);
    if (len < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGBIT(EV_REL) failed: %s\n",
                    strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->rel_bitmask))
            xf86IDrvMsg(info, X_INFO, "Has REL %d\n", i);
    }

    len = ioctl(info->fd, EVIOCGBIT(EV_ABS, sizeof(cmt->abs_bitmask)),
                cmt->abs_bitmask);
    if (len < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGBIT(EV_ABS) failed: %s\n",
                    strerror(errno));
        return !Success;
    }

    for (i = ABS_X; i <= ABS_MAX; i++) {
        if (TestBit(i, cmt->abs_bitmask)) {
            struct input_absinfo* absinfo;
            xf86IDrvMsg(info, X_INFO, "Has ABS axis %d\n", i);
            len = ioctl(info->fd, EVIOCGABS(i), &cmt->absinfo[i]);
            if (len < 0) {
                xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGABS(%d) failed: %s\n",
                            i, strerror(errno));
                return !Success;
            }
            absinfo = &cmt->absinfo[i];
            xf86IDrvMsg(info, X_INFO, "    min = %d\n", absinfo->minimum);
            xf86IDrvMsg(info, X_INFO, "    max = %d\n", absinfo->maximum);
            xf86IDrvMsg(info, X_INFO, "    res = %d\n", absinfo->resolution);
            xf86IDrvMsg(info, X_INFO, "    fuzz = %d\n", absinfo->fuzz);
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
    unsigned char map[CMT_NUM_BUTTONS + 1];
    int i;
    Atom btn_labels[CMT_NUM_BUTTONS] = { 0 };
    Atom axes_labels[2] = { 0 };

    axes_labels[0] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_X);
    axes_labels[1] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_Y);

    btn_labels[0] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_LEFT);
    btn_labels[1] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_MIDDLE);
    btn_labels[2] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_RIGHT);
    btn_labels[3] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_UP);
    btn_labels[4] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_DOWN);
    btn_labels[5] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_LEFT);
    btn_labels[6] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_RIGHT);

    /* TODO: Prop to adjust button mapping */
    for (i = 0; i <= CMT_NUM_BUTTONS; i++)
        map[i] = i;

    InitPointerDeviceStruct((DevicePtr)dev, map, CMT_NUM_BUTTONS, btn_labels,
                            PointerCtrl, GetMotionHistorySize(), 2,
                            axes_labels);

    /* X Valuator */
    xf86InitValuatorAxisStruct(dev, 0, axes_labels[0], -1, -1, 1, 0, 1);
    xf86InitValuatorDefaults(dev, 0);

    /* Y Valuator */
    xf86InitValuatorAxisStruct(dev, 1, axes_labels[1], -1, -1, 1, 0, 1);
    xf86InitValuatorDefaults(dev, 1);

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
    xf86IDrvMsg(info, X_INFO, "Plug\n");
    xf86AddInputDriver(&CMT, module, 0);
    return module;
}

static void
Unplug(pointer p)
{
    xf86IDrvMsg(info, X_INFO, "Unplug\n");
}
