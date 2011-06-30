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

#ifndef XI_PROP_PRODUCT_ID
#define XI_PROP_PRODUCT_ID "Device Product ID"
#endif

#ifndef XI_PROP_DEVICE_NODE
#define XI_PROP_DEVICE_NODE "Device Node"
#endif

/* Property Atoms */
Atom prop_device;
Atom prop_product_id;
Atom prop_tap_to_click;
Atom prop_motion_speed;
Atom prop_scroll_speed;
Atom prop_active_area;

static int PropertySet(DeviceIntPtr, Atom, XIPropertyValuePtr, BOOL);
static int PropertyGet(DeviceIntPtr, Atom);
static int PropertyDel(DeviceIntPtr, Atom);

static Atom PropMake(DeviceIntPtr, char*, Atom, int, int, pointer);
static Atom PropMake_Int(DeviceIntPtr, char*, int, int, pointer);
static Atom PropMake_String(DeviceIntPtr, char*, pointer);

static void PropInit_Device(DeviceIntPtr);
static void PropInit_ProductId(DeviceIntPtr);
static void PropInit_TapToClick(DeviceIntPtr);
static void PropInit_MotionSpeed(DeviceIntPtr);
static void PropInit_ScrollSpeed(DeviceIntPtr);
static void PropInit_ActiveArea(DeviceIntPtr);


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

    props->scroll_speed_v = xf86SetIntOption(opts, CMT_CONF_SCROLL_SPEED_V,
                                             CMT_DEF_SCROLL_SPEED_V);

    props->scroll_speed_h = xf86SetIntOption(opts, CMT_CONF_SCROLL_SPEED_H,
                                             CMT_DEF_SCROLL_SPEED_H);

    /*
     * Initialize useable trackpad area. If not user configured,
     * use x/y valuator min/max as reported by kernel driver.
     */
    props->area_left = xf86SetIntOption(opts, CMT_CONF_AREA_LEFT,
                                        Event_Get_Left(info));

    props->area_right = xf86SetIntOption(opts, CMT_CONF_AREA_RIGHT,
                                         Event_Get_Right(info));

    props->area_top = xf86SetIntOption(opts, CMT_CONF_AREA_TOP,
                                       Event_Get_Top(info));

    props->area_bottom = xf86SetIntOption(opts, CMT_CONF_AREA_BOTTOM,
                                          Event_Get_Bottom(info));
}

/**
 * Initialize Device Properties
 */
int
PropertyInit(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    cmt->handlers = XIRegisterPropertyHandler(info->dev, PropertySet,
                                              PropertyGet, PropertyDel);
    if (cmt->handlers == 0)
        return BadAlloc;

    /* Create and initialize Device Properties and their Atoms */
    PropInit_Device(dev);
    PropInit_ProductId(dev);
    PropInit_TapToClick(dev);
    PropInit_MotionSpeed(dev);
    PropInit_ScrollSpeed(dev);
    PropInit_ActiveArea(dev);

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

    if (!checkonly)
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

    } else if (atom == prop_scroll_speed) {
        if (prop->type != XA_INTEGER || prop->format != 32 || prop->size != 2)
            return BadMatch;

        if (!checkonly) {
            props->scroll_speed_v = ((INT32*)prop->data)[0];
            props->scroll_speed_h = ((INT32*)prop->data)[1];
        }

    } else if (atom == prop_active_area) {
        if (prop->type != XA_INTEGER || prop->format != 32 || prop->size != 4)
            return BadMatch;

        if (!checkonly) {
            props->area_left   = ((INT32*)prop->data)[0];
            props->area_right  = ((INT32*)prop->data)[1];
            props->area_top    = ((INT32*)prop->data)[2];
            props->area_bottom = ((INT32*)prop->data)[3];
        }
    } else if (atom == prop_device || atom == prop_product_id)
        return BadAccess; /* Read-only properties */

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
PropMake(DeviceIntPtr dev, char* name, Atom type, int size, int len,
         pointer vals)
{
    Atom atom;

    atom = MakeAtom(name, strlen(name), TRUE);
    XIChangeDeviceProperty(dev, atom, type, size, PropModeReplace, len, vals,
                           FALSE);
    XISetDevicePropertyDeletable(dev, atom, FALSE);

    return atom;
}

static Atom
PropMake_Int(DeviceIntPtr dev, char* name, int size, int len, pointer vals)
{
    return PropMake(dev, name, XA_INTEGER, size, len, vals);
}

static Atom
PropMake_String(DeviceIntPtr dev, char* name, pointer str)
{
    return PropMake(dev, name, XA_STRING, 8, strlen(str), str);
}

/**
 * Device Property Initializers
 */

static void
PropInit_Device(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    prop_device = PropMake_String(dev, XI_PROP_DEVICE_NODE, cmt->device);
}

static void
PropInit_ProductId(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    uint32_t vals[2];

    vals[0] = cmt->id.vendor;
    vals[1] = cmt->id.product;
    prop_device = PropMake_Int(dev, XI_PROP_PRODUCT_ID, 32, 2, vals);
}

static void
PropInit_TapToClick(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;
    uint8_t vals[1];

    vals[0] = (uint8_t)props->tap_to_click;
    prop_tap_to_click = PropMake_Int(dev, CMT_PROP_TAPTOCLICK, 8, 1, vals);
}

static void
PropInit_MotionSpeed(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;
    uint32_t vals[1];

    vals[0] = (uint32_t)props->motion_speed;
    prop_motion_speed = PropMake_Int(dev, CMT_PROP_MOTION_SPEED, 32, 1, vals);
}

static void
PropInit_ScrollSpeed(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;
    uint32_t vals[2];

    vals[0] = (uint32_t)props->scroll_speed_v;
    vals[1] = (uint32_t)props->scroll_speed_h;
    prop_scroll_speed = PropMake_Int(dev, CMT_PROP_SCROLL_SPEED, 32, 2, vals);
}

static void
PropInit_ActiveArea(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;
    uint32_t vals[4];

    vals[0] = (uint32_t)props->area_left;
    vals[1] = (uint32_t)props->area_right;
    vals[2] = (uint32_t)props->area_top;
    vals[3] = (uint32_t)props->area_bottom;
    prop_active_area = PropMake_Int(dev, CMT_PROP_AREA, 32, 4, vals);
}
