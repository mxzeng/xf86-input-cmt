/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/Xatom.h>
#include <X11/extensions/XI.h>
#include <xf86Xinput.h>
#include <xorg-server.h>
#include <xserver-properties.h>

#include "cmt.h"

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


/*
 * xf86IDrvMsg is not introducted until ABI12.
 * Until then, manually prepend our module name to the format string.
 */
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 12
#define xf86IDrvMsg(info, x, ...) \
    xf86Msg((x), "cmt: " __VA_ARGS__)
#endif

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

    xf86IDrvMsg(info, X_INFO, "NewPreInit\n");

    if (!(cmt = calloc(1, sizeof(*cmt))))
        return BadAlloc;

    info->type_name               = XI_TOUCHPAD;
    info->device_control          = DeviceControl;
    info->read_input              = ReadInput;
    info->control_proc            = NULL;
    info->switch_mode             = NULL;  /* Only support Absolute mode */
    info->private                 = cmt;

    xf86ProcessCommonOptions(info, info->options);

    return Success;
}

static void
UnInit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
    CmtDevicePtr device = info->private;

    xf86IDrvMsg(info, X_INFO, "UnInit\n");
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
    xf86IDrvMsg(info, X_INFO, "ReadInput\n");
}

/**
 * device control event handlers
 */
static Bool
DeviceInit(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    xf86IDrvMsg(info, X_INFO, "Init\n");

    return Success;
}

static Bool
DeviceOn(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    xf86IDrvMsg(info, X_INFO, "On\n");

    return Success;
}

static Bool
DeviceOff(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    xf86IDrvMsg(info, X_INFO, "Off\n");

    return Success;
}

static Bool
DeviceClose(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    xf86IDrvMsg(info, X_INFO, "Close\n");

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
