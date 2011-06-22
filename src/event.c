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

static void Absinfo_Print(InputInfoPtr, struct input_absinfo*);

static void Event_Syn(InputInfoPtr, struct input_event*);
static void Event_Syn_Report(InputInfoPtr, struct input_event*);
static void Event_Syn_MT_Report(InputInfoPtr, struct input_event*);

static void Event_Key(InputInfoPtr, struct input_event*);

static void Event_Abs(InputInfoPtr, struct input_event*);

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
Event_IdentifyDevice(InputInfoPtr info)
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
            struct input_absinfo* absinfo = &cmt->absinfo[i];
            xf86IDrvMsg(info, X_INFO, "Has ABS axis %d\n", i);
            len = ioctl(info->fd, EVIOCGABS(i), absinfo);
            if (len < 0) {
                xf86IDrvMsg(info, X_ERROR, "ioctl EVIOCGABS(%d) failed: %s\n",
                            i, strerror(errno));
                return !Success;
            }
            Absinfo_Print(info, absinfo);
        }
    }

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
    xf86IDrvMsg(info, X_INFO, "@ %ld.%06ld  KEY [%d] = %d\n",
        ev->time.tv_sec, ev->time.tv_usec, ev->code, ev->value);
}

static void
Event_Abs(InputInfoPtr info, struct input_event* ev)
{
    xf86IDrvMsg(info, X_INFO, "@ %ld.%06ld  ABS [%d] = %d\n",
        ev->time.tv_sec, ev->time.tv_usec, ev->code, ev->value);
}

