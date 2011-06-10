/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "properties.h"

#include <exevents.h>
#include <inputstr.h>
#include <X11/Xatom.h>
#include <X11/extensions/XI.h>
#include <xf86.h>
#include <xf86Xinput.h>
#include <xorg-server.h>
#include <xserver-properties.h>

#include "cmt.h"
#include "cmt-properties.h"

/* Property Atoms */
Atom prop_tap_to_click;
Atom prop_motion_speed;

static int PropertySet(DeviceIntPtr, Atom, XIPropertyValuePtr, BOOL);
static int PropertyGet(DeviceIntPtr, Atom);
static int PropertyDel(DeviceIntPtr, Atom);

static Atom PropMake_Int(DeviceIntPtr, char*, int, int, pointer);

static void PropInit_TapToClick(DeviceIntPtr, CmtPropertiesPtr);
static void PropInit_MotionSpeed(DeviceIntPtr, CmtPropertiesPtr);


/**
 * Process Configuration Options
 */
void
ProcessConfOptions(InputInfoPtr info, pointer opts)
{
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;

    /* Initialize device 'props' from xorg 'opts' */
    props->tap_to_click = xf86SetBoolOption(opts, CMT_CONF_TAPTOCLICK,
                                            CMT_DEF_TAPTOCLICK);

    props->motion_speed = xf86SetIntOption(opts, CMT_CONF_MOTION_SPEED,
                                           CMT_DEF_MOTION_SPEED);
}

/**
 * Initialize Device Properties
 */
int
PropertyInit(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;

    cmt->handlers = XIRegisterPropertyHandler(info->dev, PropertySet,
                                              PropertyGet, PropertyDel);
    if (cmt->handlers == 0)
        return BadAlloc;

    /* Create and initialize Device Properties and their Atoms */
    PropInit_TapToClick(dev, props);
    PropInit_MotionSpeed(dev, props);

    return Success;
}

/**
 * Device Property Handlers
 */
static int
PropertySet(DeviceIntPtr dev, Atom atom, XIPropertyValuePtr prop,
            BOOL checkonly)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;

    xf86IDrvMsg(info, X_INFO, "PropertySet: %s (%d)\n", NameForAtom(atom),
                (int)atom);

    if (atom == prop_tap_to_click) {
        if (prop->type != XA_INTEGER || prop->format != 8 || prop->size != 1)
            return BadMatch;

        if (!checkonly)
            props->tap_to_click = *(BOOL*)prop->data;

    } else if (atom == prop_motion_speed) {
        if (prop->type != XA_INTEGER || prop->format != 32 || prop->size != 1)
            return BadMatch;

        if (!checkonly)
            props->motion_speed = *(INT32*)prop->data;

    }

    return Success;
}

static int
PropertyGet(DeviceIntPtr dev, Atom property)
{
    return Success;
}

static int
PropertyDel(DeviceIntPtr dev, Atom property)
{
    return Success;
}

/**
 * By-Type Device Property Creators
 */
static Atom
PropMake_Int(DeviceIntPtr dev, char* name, int size, int len, pointer vals)
{
    Atom atom;

    atom = MakeAtom(name, strlen(name), TRUE);
    XIChangeDeviceProperty(dev, atom, XA_INTEGER, size, PropModeReplace, len,
                           vals, FALSE);
    XISetDevicePropertyDeletable(dev, atom, FALSE);

    return atom;
}

/**
 * Device Property Initializers
 */
static void
PropInit_TapToClick(DeviceIntPtr dev, CmtPropertiesPtr props)
{
    uint8_t vals[1];

    vals[0] = (uint8_t)props->tap_to_click;
    prop_tap_to_click = PropMake_Int(dev, CMT_PROP_TAPTOCLICK, 8, 1, vals);
}

static void
PropInit_MotionSpeed(DeviceIntPtr dev, CmtPropertiesPtr props)
{
    uint32_t vals[1];

    vals[0] = (uint32_t)props->motion_speed;
    prop_motion_speed = PropMake_Int(dev, CMT_PROP_MOTION_SPEED, 32, 1, vals);
}
