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
#include <xserver-properties.h>

#include "cmt.h"
#include "cmt-properties.h"
#include "event.h"

typedef enum PropType {
    PropTypeInt,
    PropTypeShort,
    PropTypeBool,
    PropTypeString,
    PropTypeReal,
} PropType;

struct GesturesProp {
    GesturesProp* next;
    Atom atom;
    PropType type;
    union {
        void* v;
        int* i;
        short* h;
        GesturesPropBool* b;
        const char** s;
        double* r;
    } val;
    void* handler_data;
    GesturesPropGetHandler get;
    GesturesPropSetHandler set;
};

/* XIProperty callbacks */
static int PropertySet(DeviceIntPtr, Atom, XIPropertyValuePtr, BOOL);
static int PropertyGet(DeviceIntPtr, Atom);
static int PropertyDel(DeviceIntPtr, Atom);

/* Property List management functions */
static GesturesProp* PropList_Find(DeviceIntPtr, Atom);
static void PropList_Insert(DeviceIntPtr, GesturesProp*);
static void PropList_Remove(DeviceIntPtr, GesturesProp*);
static void PropList_Free(DeviceIntPtr);

/* Property helper functions */
static int PropChange(DeviceIntPtr, Atom, PropType, void*);
static GesturesProp* PropCreate(DeviceIntPtr, const char*, PropType, void*,
                                void*);

/* Typed PropertySet Callback Handlers */
static int PropSet_Int(DeviceIntPtr, GesturesProp*, XIPropertyValuePtr, BOOL);
static int PropSet_Short(DeviceIntPtr, GesturesProp*, XIPropertyValuePtr, BOOL);
static int PropSet_Bool(DeviceIntPtr, GesturesProp*, XIPropertyValuePtr, BOOL);
static int PropSet_String(DeviceIntPtr, GesturesProp*, XIPropertyValuePtr,BOOL);
static int PropSet_Real(DeviceIntPtr, GesturesProp*, XIPropertyValuePtr, BOOL);

/* Property Provider implementation */
static GesturesProp* PropCreate_Int(void*, const char*, int*, const int);
static GesturesProp* PropCreate_Short(void*, const char*, short*, const short);
static GesturesProp* PropCreate_Bool(void*, const char*, GesturesPropBool*,
                                     const GesturesPropBool);
static GesturesProp* PropCreate_String(void*, const char*, const char**,
                                       const char*);
static GesturesProp* PropCreate_Real(void*, const char*, double*, const double);
static void Prop_RegisterHandlers(void*, GesturesProp*, void*,
                                  GesturesPropGetHandler,
                                  GesturesPropSetHandler);
static void Prop_Free(void*, GesturesProp*);


/**
 * Global GesturesPropProvider
 */
GesturesPropProvider prop_provider = {
    PropCreate_Int,
    PropCreate_Short,
    PropCreate_Bool,
    PropCreate_String,
    PropCreate_Real,
    Prop_RegisterHandlers,
    Prop_Free
};


/**
 * Initialize Device Properties
 */
int
PropertiesInit(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;
    GesturesProp *dump_debug_log_prop;

    cmt->handlers = XIRegisterPropertyHandler(dev, PropertySet, PropertyGet,
                                              PropertyDel);
    if (cmt->handlers == 0)
        return BadAlloc;

    /* Create Device Properties */

    /* Read Only properties */
    PropCreate_String(dev, XI_PROP_DEVICE_NODE, NULL, cmt->device);
    PropCreate_Short(dev, XI_PROP_VENDOR_ID, NULL, cmt->id.vendor);
    PropCreate_Short(dev, XI_PROP_PRODUCT_ID, NULL, cmt->id.product);

    /*
     * Useable trackpad area. If not configured in .conf file,
     * use x/y valuator min/max as reported by kernel driver.
     */
    PropCreate_Int(dev, CMT_PROP_AREA_LEFT, &props->area_left,
                   Event_Get_Left(info));
    PropCreate_Int(dev, CMT_PROP_AREA_RIGHT, &props->area_right,
                   Event_Get_Right(info));
    PropCreate_Int(dev, CMT_PROP_AREA_TOP, &props->area_top,
                   Event_Get_Top(info));
    PropCreate_Int(dev, CMT_PROP_AREA_BOTTOM, &props->area_bottom,
                   Event_Get_Bottom(info));

    /*
     * Trackpad resolution (pixels/mm). If not configured in .conf file,
     * use x/y resolution as reported by kernel driver.
     */
    PropCreate_Int(dev, CMT_PROP_RES_Y, &props->res_y, Event_Get_Res_Y(info));
    PropCreate_Int(dev, CMT_PROP_RES_X, &props->res_x, Event_Get_Res_X(info));

    PropCreate_Bool(dev, CMT_PROP_SCROLL_BTN, &props->scroll_btns, TRUE);
    PropCreate_Bool(dev, CMT_PROP_SCROLL_AXES, &props->scroll_axes, FALSE);
    dump_debug_log_prop = PropCreate_Bool(dev,
                                          CMT_PROP_DUMP_DEBUG_LOG,
                                          &props->dump_debug_log,
                                          FALSE);
    Prop_RegisterHandlers(dev, dump_debug_log_prop, info, NULL,
                          Event_Dump_Debug_Log);

    return Success;
}

/**
 * Cleanup Device Properties
 */
void
PropertiesClose(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    PropList_Free(dev);
    XIUnregisterPropertyHandler(dev, cmt->handlers);
}

/**
 * Type-Specific Device Property Set Handlers
 */
static int
PropSet_Int(DeviceIntPtr dev, GesturesProp* prop, XIPropertyValuePtr val,
            BOOL checkonly)
{
    InputInfoPtr info = dev->public.devicePrivate;

    if (val->type != XA_INTEGER || val->format != 32 || val->size != 1)
        return BadMatch;

    if (!checkonly) {
        *prop->val.i = *(CARD32*)val->data;
        DBG(info, "\"%s\" = %d\n", NameForAtom(prop->atom), *prop->val.i);
    }

    return Success;
}

static int
PropSet_Short(DeviceIntPtr dev, GesturesProp* prop, XIPropertyValuePtr val,
              BOOL checkonly)
{
    InputInfoPtr info = dev->public.devicePrivate;

    if (val->type != XA_INTEGER || val->format != 16 || val->size != 1)
        return BadMatch;

    if (!checkonly) {
        *prop->val.h = *(CARD16*)val->data;
        DBG(info, "\"%s\" = %d\n", NameForAtom(prop->atom), *prop->val.h);
    }

    return Success;
}

static int
PropSet_Bool(DeviceIntPtr dev, GesturesProp* prop, XIPropertyValuePtr val,
             BOOL checkonly)
{
    InputInfoPtr info = dev->public.devicePrivate;

    if (val->type != XA_INTEGER || val->format != 8 || val->size != 1)
        return BadMatch;

    if (!checkonly) {
        *prop->val.b = !!(*(CARD8*)val->data);
        DBG(info, "\"%s\" = %s\n", NameForAtom(prop->atom),
            *prop->val.b ? "True" : "False");
    }

    return Success;
}

static int
PropSet_String(DeviceIntPtr dev, GesturesProp* prop, XIPropertyValuePtr val,
               BOOL checkonly)
{
    InputInfoPtr info = dev->public.devicePrivate;

    if (val->type != XA_STRING || val->format != 8)
        return BadMatch;

    if (!checkonly) {
        *prop->val.s = val->data;
        DBG(info, "\"%s\" = \"%s\"\n", NameForAtom(prop->atom), *prop->val.s);
    }

    return Success;
}

static int
PropSet_Real(DeviceIntPtr dev, GesturesProp* prop, XIPropertyValuePtr val,
             BOOL checkonly)
{
    InputInfoPtr info = dev->public.devicePrivate;
    Atom XA_FLOAT = XIGetKnownProperty(XATOM_FLOAT);

    if (val->type != XA_FLOAT || val->format != 32 || val->size != 1)
        return BadMatch;

    if (!checkonly) {
        *prop->val.r = *(float*)val->data;
        DBG(info, "\"%s\" = %g\n", NameForAtom(prop->atom), *prop->val.r);
    }

    return Success;
}

/**
 * Device Property Handlers
 */
static int
PropertySet(DeviceIntPtr dev, Atom atom, XIPropertyValuePtr val,
            BOOL checkonly)
{
    GesturesProp* prop;
    int rc;

    prop = PropList_Find(dev, atom);
    if (!prop)
        return Success; /* Unknown or uninitialized Property */

    if (prop->val.v == NULL)
        return BadAccess; /* Read-only property */

    switch (prop->type) {
    case PropTypeInt:
        rc = PropSet_Int(dev, prop, val, checkonly);
        break;
    case PropTypeShort:
        rc = PropSet_Short(dev, prop, val, checkonly);
        break;
    case PropTypeBool:
        rc = PropSet_Bool(dev, prop, val, checkonly);
        break;
    case PropTypeString:
        rc = PropSet_String(dev, prop, val, checkonly);
        break;
    case PropTypeReal:
        rc = PropSet_Real(dev, prop, val, checkonly);
        break;
    default:
        rc = BadMatch; /* Unknown property type */
        break;
    }

    if (!checkonly && rc == Success && prop->set)
        prop->set(prop->handler_data);

    return rc;
}

static int
PropertyGet(DeviceIntPtr dev, Atom property)
{
    GesturesProp* prop;

    prop = PropList_Find(dev, property);
    if (!prop)
        return Success; /* Unknown or uninitialized Property */

    // If get handler returns true, update the property value in the server.
    if (prop->get && prop->get(prop->handler_data))
        PropChange(dev, prop->atom, prop->type, prop->val.v);

    return Success;
}

static int
PropertyDel(DeviceIntPtr dev, Atom property)
{
    return Success;
}

/**
 * Property List Management
 */

static GesturesProp*
PropList_Find(DeviceIntPtr dev, Atom atom)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    GesturesProp* p;

    for (p = cmt->prop_list; p && p->atom != atom; p = p->next)
        continue;

    return p;
}

static void
PropList_Insert(DeviceIntPtr dev, GesturesProp* prop)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    prop->next = cmt->prop_list;
    cmt->prop_list = prop;
}

static void
PropList_Remove(DeviceIntPtr dev, GesturesProp* prop)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    GesturesProp* p;

    if (!cmt->prop_list || !prop)
        return;

    if (cmt->prop_list == prop) {
        cmt->prop_list = prop->next;
        return;
    }

    for (p = cmt->prop_list; p->next; p = p->next)
        if (p->next == prop) {
            p->next = p->next->next;
            return;
        }
}

static void
PropList_Free(DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;

    while (cmt->prop_list)
        Prop_Free(dev, cmt->prop_list);
}

static void
Prop_Free(void* priv, GesturesProp* prop)
{
    DeviceIntPtr dev = priv;
    InputInfoPtr info = dev->public.devicePrivate;

    if (!prop)
        return;

    DBG(info, "Freeing Property: \"%s\"\n", NameForAtom(prop->atom));
    PropList_Remove(dev, prop);
    XIDeleteDeviceProperty(dev, prop->atom, FALSE);
    free(prop);
}

static int PropChange(DeviceIntPtr dev, Atom atom, PropType type, void* val)
{
    Atom type_atom;
    int size;
    int format;

    switch (type) {
    case PropTypeInt:
        type_atom = XA_INTEGER;
        size = 1;
        format = 32;
        break;
    case PropTypeShort:
        type_atom = XA_INTEGER;
        size = 1;
        format = 16;
        break;
    case PropTypeBool:
        type_atom = XA_INTEGER;
        size = 1;
        format = 8;
        break;
    case PropTypeString:
        type_atom = XA_STRING;
        size = strlen((const char*)val);
        format = 8;
        break;
    case PropTypeReal:
        type_atom = XIGetKnownProperty(XATOM_FLOAT);
        size = 1;
        format = 32;
        break;
    default: /* Unknown type */
        return BadMatch;
    }

    return XIChangeDeviceProperty(dev, atom, type_atom, format,
                                  PropModeReplace, size, val, FALSE);
}

/**
 * Device Property Creators
 */
static GesturesProp*
PropCreate(DeviceIntPtr dev, const char* name, PropType type, void* val,
           void* init)
{
    InputInfoPtr info = dev->public.devicePrivate;
    GesturesProp* prop;
    Atom atom;

    DBG(info, "Creating Property: \"%s\"\n", name);

    atom = MakeAtom(name, strlen(name), TRUE);
    if (atom == BAD_RESOURCE)
        return NULL;

    if (PropChange(dev, atom, type, init) != Success)
        return NULL;

    XISetDevicePropertyDeletable(dev, atom, FALSE);

    prop = PropList_Find(dev, atom);
    if (!prop) {
        prop = calloc(1, sizeof(*prop));
        if (!prop)
            return NULL;
        PropList_Insert(dev, prop);
    }

    prop->atom = atom;
    prop->type = type;
    prop->val.v = val;

    return prop;
}

GesturesProp*
PropCreate_Int(void* priv, const char* name, int* val, const int init)
{
    DeviceIntPtr dev = priv;
    InputInfoPtr info = dev->public.devicePrivate;
    int cfg;
    CARD32 cval;

    cfg = xf86SetIntOption(info->options, name, init);
    if (val)
        *val = cfg;
    cval = (CARD32)cfg;

    return PropCreate(dev, name, PropTypeInt, val, &cval);
}

GesturesProp*
PropCreate_Short(void* priv, const char* name, short* val, const short init)
{
    DeviceIntPtr dev = priv;
    InputInfoPtr info = dev->public.devicePrivate;
    short cfg;
    CARD16 cval;

    cfg = xf86SetIntOption(info->options, name, init);
    if (val)
        *val = cfg;
    cval = (CARD16)cfg;

    return PropCreate(dev, name, PropTypeShort, val, &cval);
}

GesturesProp*
PropCreate_Bool(void* priv, const char* name, GesturesPropBool* val,
                const GesturesPropBool init)
{
    DeviceIntPtr dev = priv;
    InputInfoPtr info = dev->public.devicePrivate;
    BOOL cfg;
    CARD8 cval;

    cfg = xf86SetBoolOption(info->options, name, (BOOL)!!init);
    if (val)
        *val = cfg;
    cval = (CARD8)!!cfg;

    return PropCreate(dev, name, PropTypeBool, val, &cval);
}

GesturesProp*
PropCreate_String(void* priv, const char* name, const char** val,
                  const char* init)
{
    DeviceIntPtr dev = priv;
    InputInfoPtr info = dev->public.devicePrivate;
    const char* cfg;

    cfg = xf86SetStrOption(info->options, name, (char *)init);
    if (val)
        *val = cfg;

    return PropCreate(dev, name, PropTypeString, val, (char *)cfg);
}

GesturesProp*
PropCreate_Real(void* priv, const char* name, double* val, const double init)
{
    DeviceIntPtr dev = priv;
    InputInfoPtr info = dev->public.devicePrivate;
    double cfg;
    float cval;

    cfg = xf86SetRealOption(info->options, name, init);
    if (val)
        *val = cfg;
    cval = (float)cfg;

    return PropCreate(dev, name, PropTypeReal, val, &cval);
}

static void Prop_RegisterHandlers(void* priv, GesturesProp* prop,
                                  void* handler_data,
                                  GesturesPropGetHandler get,
                                  GesturesPropSetHandler set)
{
    // Sanity checks
    if (!priv || !prop)
        return;

    prop->handler_data = handler_data;
    prop->get = get;
    prop->set = set;
}
