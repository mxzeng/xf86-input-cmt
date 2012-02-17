/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "event.h"

#include <errno.h>
#include <linux/input.h>
#include <time.h>

#include "cmt.h"
#include "mt.h"

#ifndef BTN_TOOL_QUINTTAP
#define BTN_TOOL_QUINTTAP  0x148  /* Five fingers on trackpad */
#endif

/* Set clockid to be used for timestamps */
#ifndef EVIOCSCLOCKID
#define EVIOCSCLOCKID  _IOW('E', 0xa0, int)
#endif

static inline Bool TestBit(int, unsigned long*);

static void Absinfo_Print(InputInfoPtr, struct input_absinfo*);
static void Event_Print(InputInfoPtr, struct input_event*);

static void Event_Syn(InputInfoPtr, struct input_event*);
static void Event_Syn_Report(InputInfoPtr, struct input_event*);
static void Event_Syn_MT_Report(InputInfoPtr, struct input_event*);

static void Event_Key(InputInfoPtr, struct input_event*);

static void Event_Abs(InputInfoPtr, struct input_event*);
static void Event_Abs_MT(InputInfoPtr, struct input_event*);
static void SemiMtSetAbsPressure(InputInfoPtr, struct input_event*);


/**
 * Helper functions
 */
static inline Bool
TestBit(int bit, unsigned long* array)
{
    return !!(array[bit / LONG_BITS] & (1L << (bit % LONG_BITS)));
}

/**
 * Input Device Event Property accessors
 */
int
Event_Get_Left(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_X];
    return absinfo->minimum;
}

int
Event_Get_Right(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_X];
    return absinfo->maximum;
}

int
Event_Get_Top(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_Y];
    return absinfo->minimum;
}

int
Event_Get_Bottom(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_Y];
    return absinfo->maximum;
}

int
Event_Get_Res_Y(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_Y];
    return absinfo->resolution;
}

int
Event_Get_Res_X(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_X];
    return absinfo->resolution;
}

int
Event_Get_Button_Pad(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    return TestBit(INPUT_PROP_BUTTONPAD, cmt->prop_bitmask);
}

int
Event_Get_Semi_MT(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    return TestBit(INPUT_PROP_SEMI_MT, cmt->prop_bitmask);
}

int
Event_Get_T5R2(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    return (Event_Get_Touch_Count(info) > evstate->slot_count);
}

int
Event_Get_Touch_Count(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;

    if (TestBit(BTN_TOOL_QUINTTAP, cmt->key_bitmask))
        return 5;
    if (TestBit(BTN_TOOL_QUADTAP, cmt->key_bitmask))
        return 4;
    if (TestBit(BTN_TOOL_TRIPLETAP, cmt->key_bitmask))
        return 3;
    if (TestBit(BTN_TOOL_DOUBLETAP, cmt->key_bitmask))
        return 2;
    return 1;
}

static int
Event_Enable_Monotonic(InputInfoPtr info)
{
    unsigned int clk = CLOCK_MONOTONIC;
    return (ioctl(info->fd, EVIOCSCLOCKID, &clk) == 0) ? Success : !Success;
}


/**
 * Probe Device Input Event Support
 */
int
Event_Init(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    int i;
    int len;

    if (ioctl(info->fd, EVIOCGID, &cmt->id) < 0) {
         xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGID failed: %s\n",
                     strerror(errno));
         return !Success;
    }
    xf86IDrvMsg(info, X_PROBED,
        "vendor: %02X, product: %02X\n", cmt->id.vendor, cmt->id.product);

    if (ioctl(info->fd, EVIOCGNAME(sizeof(cmt->name) - 1), cmt->name) < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGNAME failed: %s\n",
                    strerror(errno));
        return !Success;
    }
    xf86IDrvMsg(info, X_PROBED, "name: %s\n", cmt->name);

    len = ioctl(info->fd, EVIOCGPROP(sizeof(cmt->prop_bitmask)),
                cmt->prop_bitmask);
    if (len < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGPROP failed: %s\n",
                    strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->prop_bitmask))
            PROBE_DBG(info, "Has Property %d\n", i);
    }

    len = ioctl(info->fd, EVIOCGBIT(0, sizeof(cmt->bitmask)), cmt->bitmask);
    if (len < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGBIT failed: %s\n",
                    strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->bitmask))
            PROBE_DBG(info, "Has Event %d\n", i);
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
            PROBE_DBG(info, "Has KEY %d\n", i);
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
            PROBE_DBG(info, "Has LED %d\n", i);
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
            PROBE_DBG(info, "Has REL %d\n", i);
    }

    /*
     * TODO(djkurtz): Solve the race condition between MT slot initialization
     *    from absinfo, and incoming/lost input events.
     *    Specifically, if kernel driver sends MT_SLOT event between absinfo
     *    probe and when we start listening for input events.
     */

    len = ioctl(info->fd, EVIOCGBIT(EV_ABS, sizeof(cmt->abs_bitmask)),
                cmt->abs_bitmask);
    if (len < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGBIT(EV_ABS) failed: %s\n",
                    strerror(errno));
        return !Success;
    }

    for (i = ABS_X; i <= ABS_MAX; i++) {
        if (TestBit(i, cmt->abs_bitmask)) {
            struct input_absinfo* absinfo = &cmt->absinfo[i];
            PROBE_DBG(info, "Has ABS axis %d\n", i);
            len = ioctl(info->fd, EVIOCGABS(i), absinfo);
            if (len < 0) {
                xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGABS(%d) failed: %s\n",
                            i, strerror(errno));
                /*
                 * Clean up in case where error happens after MTB_Init() has
                 * already allocated slots.
                 */
                goto error_MT_Free;
            }

            Absinfo_Print(info, absinfo);

            if (i == ABS_MT_SLOT) {
                int rc;
                rc = MTB_Init(info, absinfo->minimum, absinfo->maximum,
                              absinfo->value);
                if (rc != Success)
                    return rc;
            } else if (IS_ABS_MT(i)) {
                evstate->mt_axes[MT_CODE(i)] = absinfo;
            }
        }
    }

    /*
     * TODO(djkurtz): probe driver for current MT slot states when supported
     * by kernel input subsystem.
     */

    return Success;

error_MT_Free:
    MT_Free(info);
    return !Success;
}

void
Event_Free(InputInfoPtr info)
{
    MT_Free(info);
}

void
Event_Open(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;

    /* Select monotonic input event timestamps, if supported by kernel */
    cmt->is_monotonic = (Event_Enable_Monotonic(info) == Success);
    xf86IDrvMsg(info, X_PROBED, "Using %s input event time stamps\n",
                cmt->is_monotonic ? "monotonic" : "realtime");
}

/**
 * Debug Print Helper Functions
 */
static void
Absinfo_Print(InputInfoPtr info, struct input_absinfo* absinfo)
{
    PROBE_DBG(info, "    min = %d\n", absinfo->minimum);
    PROBE_DBG(info, "    max = %d\n", absinfo->maximum);
    if (absinfo->fuzz)
        PROBE_DBG(info, "    fuzz = %d\n", absinfo->fuzz);
    if (absinfo->resolution)
        PROBE_DBG(info, "    res = %d\n", absinfo->resolution);
}

#define CASE_DBG(i, ev, x) \
    case x: \
        DBG(i, "@ %ld.%06ld %s = %d\n", \
            ev->time.tv_sec, ev->time.tv_usec, #x, ev->value); \
        break

static void
Event_Print(InputInfoPtr info, struct input_event* ev)
{
    switch (ev->type) {
    case EV_SYN:
        switch (ev->code) {
        case SYN_REPORT:
            DBG(info, "@ %ld.%06ld  ---------- SYN_REPORT -------\n",
                ev->time.tv_sec, ev->time.tv_usec);
            break;
        case SYN_MT_REPORT:
            DBG(info, "@ %ld.%06ld  ........ SYN_MT_REPORT ......\n",
                ev->time.tv_sec, ev->time.tv_usec);
            break;
        default:
            DBG(info, "@ %ld.%06ld  ????????? SYN_UNKNOWN ???????\n",
                ev->time.tv_sec, ev->time.tv_usec);
            break;
        }
        break;
    case EV_ABS:
        switch (ev->code) {
        CASE_DBG(info, ev, ABS_X);
        CASE_DBG(info, ev, ABS_Y);
        CASE_DBG(info, ev, ABS_Z);
        CASE_DBG(info, ev, ABS_PRESSURE);
        CASE_DBG(info, ev, ABS_TOOL_WIDTH);
        CASE_DBG(info, ev, ABS_MT_TOUCH_MAJOR);
        CASE_DBG(info, ev, ABS_MT_TOUCH_MINOR);
        CASE_DBG(info, ev, ABS_MT_WIDTH_MAJOR);
        CASE_DBG(info, ev, ABS_MT_WIDTH_MINOR);
        CASE_DBG(info, ev, ABS_MT_ORIENTATION);
        CASE_DBG(info, ev, ABS_MT_POSITION_X);
        CASE_DBG(info, ev, ABS_MT_POSITION_Y);
        CASE_DBG(info, ev, ABS_MT_TOOL_TYPE);
        CASE_DBG(info, ev, ABS_MT_BLOB_ID);
        CASE_DBG(info, ev, ABS_MT_TRACKING_ID);
        CASE_DBG(info, ev, ABS_MT_PRESSURE);
        case ABS_MT_SLOT:
            DBG(info, "@ %ld.%06ld  .......... MT SLOT %d ........\n",
                ev->time.tv_sec, ev->time.tv_usec, ev->value);
            break;
        default:
            DBG(info, "@ %ld.%06ld ABS[%d] = %d\n",
                ev->time.tv_sec, ev->time.tv_usec, ev->code, ev->value);
            break;

        }
        break;
    case EV_KEY:
        switch (ev->code) {
        CASE_DBG(info, ev, BTN_LEFT);
        CASE_DBG(info, ev, BTN_RIGHT);
        CASE_DBG(info, ev, BTN_MIDDLE);
        CASE_DBG(info, ev, BTN_TOUCH);
        CASE_DBG(info, ev, BTN_TOOL_FINGER);
        CASE_DBG(info, ev, BTN_TOOL_DOUBLETAP);
        CASE_DBG(info, ev, BTN_TOOL_TRIPLETAP);
        CASE_DBG(info, ev, BTN_TOOL_QUADTAP);
        CASE_DBG(info, ev, BTN_TOOL_QUINTTAP);
        default:
            DBG(info, "@ %ld.%06ld KEY[%d] = %d\n",
                ev->time.tv_sec, ev->time.tv_usec, ev->code, ev->value);
        }
        break;
    default:
        DBG(info, "@ %ld.%06ld %u[%u] = %d\n", ev->time.tv_sec,
            ev->time.tv_usec, ev->type, ev->code, ev->value);
        break;
    }
}
#undef CASE_DBG

/**
 * Process Input Events
 */
void
Event_Process(InputInfoPtr info, struct input_event* ev)
{
    Event_Print(info, ev);

    switch (ev->type) {
    case EV_SYN:
        Event_Syn(info, ev);
        break;

    case EV_KEY:
        Event_Key(info, ev);
        break;

    case EV_ABS:
        Event_Abs(info, ev);
        break;

    default:
        break;
    }
}


static void
Event_Syn(InputInfoPtr info, struct input_event* ev)
{
    switch (ev->code) {
    case SYN_REPORT:
        Event_Syn_Report(info, ev);
        break;
    case SYN_MT_REPORT:
        Event_Syn_MT_Report(info, ev);
        break;
    }
}

static void
Event_Syn_Report(InputInfoPtr info, struct input_event* ev)
{
    CmtDevicePtr cmt = info->private;
    GesturePtr gesture = &cmt->gesture;
    EventStatePtr evstate = &cmt->evstate;

    Gesture_Process_Slots(gesture, evstate, &ev->time);
    MT_Print_Slots(info);
}

static void
Event_Syn_MT_Report(InputInfoPtr info, struct input_event* ev)
{
    /* TODO(djkurtz): Handle MT-A */
}

static void
Event_Key(InputInfoPtr info, struct input_event* ev)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    unsigned value = ev->value;

    switch (ev->code) {
    case BTN_LEFT:
        evstate->buttons = Bit_Assign(evstate->buttons, BUTTON_LEFT, value);
        break;
    case BTN_RIGHT:
        evstate->buttons = Bit_Assign(evstate->buttons, BUTTON_RIGHT, value);
        break;
    case BTN_MIDDLE:
        evstate->buttons = Bit_Assign(evstate->buttons, BUTTON_MIDDLE, value);
        break;
    case BTN_TOUCH:
        if (value == 0)
            evstate->touch_cnt = 0;
        break;
    case BTN_TOOL_FINGER:
        if (value)
            evstate->touch_cnt = 1;
        break;
    case BTN_TOOL_DOUBLETAP:
        if (value)
            evstate->touch_cnt = 2;
        break;
    case BTN_TOOL_TRIPLETAP:
        if (value)
            evstate->touch_cnt = 3;
        break;
    case BTN_TOOL_QUADTAP:
        if (value)
            evstate->touch_cnt = 4;
        break;
    case BTN_TOOL_QUINTTAP:
        if (value)
            evstate->touch_cnt = 5;
        break;
    }
}

static void
SemiMtSetAbsPressure(InputInfoPtr info, struct input_event* ev)
{
    /*
     * Update all active slots with the same ABS_PRESSURE value if it is a
     * semi-mt device.
     */
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    int i;

    for (i = 0; i < evstate->slot_count; i++) {
        MtSlotPtr slot = &evstate->slots[i];
        if (slot->tracking_id == -1)
            continue;
        slot->pressure = ev->value;
    }
}

static void
Event_Abs(InputInfoPtr info, struct input_event* ev)
{
    if (ev->code == ABS_MT_SLOT)
        MT_Slot_Set(info, ev->value);
    else if (IS_ABS_MT(ev->code))
        Event_Abs_MT(info, ev);
    else if ((ev->code == ABS_PRESSURE) && Event_Get_Semi_MT(info))
        SemiMtSetAbsPressure(info, ev);
}

static void
Event_Abs_MT(InputInfoPtr info, struct input_event* ev)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    struct input_absinfo* axis = evstate->mt_axes[MT_CODE(ev->code)];
    MtSlotPtr slot = evstate->slot_current;

    if (axis == NULL) {
        xf86IDrvMsg(info, X_ERROR,
            "ABS_MT[%02x] was not reported by this device\n", ev->code);
        return;
    }

    /* Warn about out of range data, but don't ignore */
    if ((ev->code != ABS_MT_TRACKING_ID)
                    && ((ev->value < axis->minimum)
                        || (ev->value > axis->maximum))) {
        xf86IDrvMsgVerb(info, X_WARNING, DBG_VERB,
            "ABS_MT[%02x] = %d : value out of range [%d .. %d]\n",
            ev->code, ev->value, axis->minimum, axis->maximum);
    }

    if (slot == NULL) {
        xf86IDrvMsg(info, X_ERROR,
            "MT slot not set. Ignoring ABS_MT event\n");
        return;
    }

    MT_Slot_Value_Set(slot, ev->code, ev->value);
}
