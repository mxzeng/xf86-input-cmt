/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CMT_H_
#define _CMT_H_

#include <linux/input.h>

#include <xf86.h>

#include "event.h"
#include "gesture.h"
#include "properties.h"

/*
 * xf86IDrvMsg is not introduced until ABI12.
 * Until then, manually prepend our module name to the format string.
 */
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 12
#define xf86IDrvMsgVerb(info, type, verb, format, ...) \
    xf86MsgVerb((type), (verb), "%s: %s: " format, info->drv->driverName, \
        info->name, ##__VA_ARGS__)

#define xf86IDrvMsg(info, type, format, ...) \
    xf86Msg((type), "%s: %s: " format, info->drv->driverName, info->name, \
        ##__VA_ARGS__)
#endif

#define LONG_BITS (sizeof(long) * 8)

/* Number of longs needed to hold the given number of bits */
#define NLONGS(x) (((x) + LONG_BITS - 1) / LONG_BITS)

typedef struct {
    CmtProperties props;
    EventStateRec evstate;
    GestureRec gesture;

    char* device;
    long  handlers;

    /* kernel driver information */
    struct input_id id;
    char name[1024];
    unsigned long bitmask[NLONGS(EV_CNT)];
    unsigned long key_bitmask[NLONGS(KEY_CNT)];
    unsigned long rel_bitmask[NLONGS(REL_CNT)];
    unsigned long abs_bitmask[NLONGS(ABS_CNT)];
    unsigned long led_bitmask[NLONGS(LED_CNT)];
    struct input_absinfo absinfo[ABS_CNT];
    unsigned long prop_bitmask[NLONGS(INPUT_PROP_CNT)];
} CmtDeviceRec, *CmtDevicePtr;

#endif
