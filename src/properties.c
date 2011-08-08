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
Atom prop_active_area;
Atom prop_active_res;

static int PropertySet(DeviceIntPtr, Atom, XIPropertyValuePtr, BOOL);
static int PropertyGet(DeviceIntPtr, Atom);
static int PropertyDel(DeviceIntPtr, Atom);

static Atom PropMake(DeviceIntPtr, char*, Atom, int, int, pointer);
static Atom PropMake_Int(DeviceIntPtr, char*, int, int, pointer);
static Atom PropMake_String(DeviceIntPtr, char*, pointer);

static void PropInit_Device(DeviceIntPtr);
static void PropInit_ProductId(DeviceIntPtr);
static void PropInit_ActiveArea(DeviceIntPtr);
static void PropInit_Resolution(DeviceIntPtr);


/**
 * Process Configuration Options
 */
void
ProcessConfOptions(InputInfoPtr info, pointer opts)
{
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;

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

    /*
     * Initialize useable trackpad area. If not user configured,
     * use x/y valuator min/max as reported by kernel driver.
     */
    props->res_y = xf86SetIntOption(opts, CMT_CONF_RES_Y,
                                         Event_Get_Res_Y(info));

    props->res_x = xf86SetIntOption(opts, CMT_CONF_RES_X,
                                        Event_Get_Res_X(info));
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
    PropInit_ActiveArea(dev);
    PropInit_Resolution(dev);

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
        DBG(info, "PropertySet: %s (%d)\n", NameForAtom(atom), (int)atom);

    if (atom == prop_active_area) {
        if (prop->type != XA_INTEGER || prop->format != 32 || prop->size != 4)
            return BadMatch;

        if (!checkonly) {
            props->area_left   = ((INT32*)prop->data)[0];
            props->area_right  = ((INT32*)prop->data)[1];
            props->area_top    = ((INT32*)prop->data)[2];
            props->area_bottom = ((INT32*)prop->data)[3];
        }

    } else if (atom == prop_active_res) {
        if (prop->type != XA_INTEGER || prop->format != 32 || prop->size != 2)
            return BadMatch;

        if (!checkonly) {
            props->res_y = ((INT32*)prop->data)[0];
            props->res_x = ((INT32*)prop->data)[1];
        }
    } else if (atom == prop_device || atom == prop_product_id) {
        xf86IDrvMsg(info, X_WARNING, "Cannot set read only prop: %s (%d)\n",
                    NameForAtom(atom), (int)atom);
        return BadAccess; /* Read-only properties */
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
    prop_product_id = PropMake_Int(dev, XI_PROP_PRODUCT_ID, 32, 2, vals);
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

static void
PropInit_Resolution(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;
    uint32_t vals[2];

    vals[0] = (uint32_t)props->res_y;
    vals[1] = (uint32_t)props->res_x;
    prop_active_res = PropMake_Int(dev, CMT_PROP_RES, 32, 2, vals);
}
