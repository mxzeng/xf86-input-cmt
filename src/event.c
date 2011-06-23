/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "event.h"

#include <errno.h>
#include <linux/input.h>

#include <xf86Xinput.h>

#include "cmt.h"
#include "mt.h"


static void Absinfo_Print(InputInfoPtr, struct input_absinfo*);

static void Event_Syn(InputInfoPtr, struct input_event*);
static void Event_Syn_Report(InputInfoPtr, struct input_event*);
static void Event_Syn_MT_Report(InputInfoPtr, struct input_event*);

static void Event_Key(InputInfoPtr, struct input_event*);

static void Event_Abs(InputInfoPtr, struct input_event*);
static void Event_Abs_MT_Slot(InputInfoPtr, struct input_event*);
static void Event_Abs_MT(InputInfoPtr, struct input_event*);


/**
 * Helper functions
 */
static inline Bool
TestBit(int bit, unsigned long* array)
{
    return array[bit / LONG_BITS] & (1L << (bit % LONG_BITS));
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
    xf86IDrvMsg(info, X_INFO, "vendor: %02X, product: %02X\n", cmt->id.vendor,
                cmt->id.product);

    if (ioctl(info->fd, EVIOCGNAME(sizeof(cmt->name) - 1), cmt->name) < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGNAME failed: %s\n",
                    strerror(errno));
        return !Success;
    }
    xf86IDrvMsg(info, X_INFO, "name: %s\n", cmt->name);

    len = ioctl(info->fd, EVIOCGPROP(sizeof(cmt->prop_bitmask)),
                cmt->prop_bitmask);
    if (len < 0) {
        xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGPROP failed: %s\n",
                    strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->prop_bitmask))
            xf86IDrvMsg(info, X_INFO, "Has Property %d\n", i);
    }

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
            xf86IDrvMsg(info, X_INFO, "Has ABS axis %d\n", i);
            len = ioctl(info->fd, EVIOCGABS(i), absinfo);
            if (len < 0) {
                xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGABS(%d) failed: %s\n",
                            i, strerror(errno));
                return !Success;
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
}

static void
Absinfo_Print(InputInfoPtr info, struct input_absinfo* absinfo)
{
    xf86IDrvMsg(info, X_INFO, "    min = %d\n", absinfo->minimum);
    xf86IDrvMsg(info, X_INFO, "    max = %d\n", absinfo->maximum);
    if (absinfo->fuzz)
        xf86IDrvMsg(info, X_INFO, "    fuzz = %d\n", absinfo->fuzz);
    if (absinfo->resolution)
        xf86IDrvMsg(info, X_INFO, "    res = %d\n", absinfo->resolution);
}

void
Event_Free(InputInfoPtr info)
{
    MT_Free(info);
}

/**
 * Process Input Events
 */
void
Event_Process(InputInfoPtr info, struct input_event* ev)
{
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
        xf86IDrvMsg(info, X_INFO, "@ %ld.%06ld %u[%u] = %d\n",
                    ev->time.tv_sec, ev->time.tv_usec, ev->type, ev->code,
                    ev->value);
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
    MT_Print_Slots(info);
    xf86IDrvMsg(info, X_INFO, "@ %ld.%06ld  ---------- SYN_REPORT -------\n",
        ev->time.tv_sec, ev->time.tv_usec);
}

static void
Event_Syn_MT_Report(InputInfoPtr info, struct input_event* ev)
{
    xf86IDrvMsg(info, X_INFO, "@ %ld.%06ld  ........ MT_SYN_REPORT .......\n",
        ev->time.tv_sec, ev->time.tv_usec);
}

static void
Event_Key(InputInfoPtr info, struct input_event* ev)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    unsigned value = ev->value;

    xf86IDrvMsg(info, X_INFO, "@ %ld.%06ld  KEY [%d] = %d\n",
        ev->time.tv_sec, ev->time.tv_usec, ev->code, ev->value);

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
    }
}

static void
Event_Abs(InputInfoPtr info, struct input_event* ev)
{
    if (ev->code == ABS_MT_SLOT) {
        Event_Abs_MT_Slot(info, ev);
    } else if (IS_ABS_MT(ev->code)) {
        Event_Abs_MT(info, ev);
    } else {
        xf86IDrvMsg(info, X_INFO, "@ %ld.%06ld  ABS [%d] = %d\n",
            ev->time.tv_sec, ev->time.tv_usec, ev->code, ev->value);
    }
}

static void
Event_Abs_MT_Slot(InputInfoPtr info, struct input_event* ev)
{
    xf86IDrvMsg(info, X_INFO, "@ %ld.%06ld  .......... MT SLOT %d ........\n",
        ev->time.tv_sec, ev->time.tv_usec, ev->value);

    MT_Slot_Set(info, ev->value);
}

static void
Event_Abs_MT(InputInfoPtr info, struct input_event* ev)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    struct input_absinfo* axis = evstate->mt_axes[MT_CODE(ev->code)];
    MtSlotPtr slot = evstate->slot_current;

    xf86IDrvMsg(info, X_INFO, "@ %ld.%06ld  ABS_MT[%02x] = %d\n",
        ev->time.tv_sec, ev->time.tv_usec, ev->code, ev->value);

    if (axis == NULL) {
        xf86IDrvMsg(info, X_ERROR,
            "ABS_MT[%02x] was not reported by this device\n", ev->code);
        return;
    }

    /* Warn about out of range data, but don't ignore */
    if ((ev->code != ABS_MT_TRACKING_ID)
                    && ((ev->value < axis->minimum)
                        || (ev->value > axis->maximum))) {
        xf86IDrvMsg(info, X_INFO,
            "ABS_MT[%02x] = %d : value out of range [%d .. %d]\n",
            ev->code, ev->value, axis->minimum, axis->maximum);
    }

    if (slot == NULL) {
        xf86IDrvMsg(info, X_ERROR, "MT slot not set. Ignoring ABS_MT event\n");
        return;
    }

    MT_Slot_Value_Set(slot, ev->code, ev->value);
}

