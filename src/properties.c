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

static int PropertySet(DeviceIntPtr, Atom, XIPropertyValuePtr, BOOL);
static int PropertyGet(DeviceIntPtr, Atom);
static int PropertyDel(DeviceIntPtr, Atom);

static Atom PropMake_Int(DeviceIntPtr, char*, int, int, pointer);

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

    /* TODO: Create and initialize Device Properties and their Atoms */

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
