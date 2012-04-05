/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CMT_H_
#define _CMT_H_

#include <linux/input.h>

#include <gestures/gestures.h>

#include <xorg-server.h>
#include <xf86.h>

#include "event.h"
#include "gesture.h"
#include "properties.h"

/* Message Log Verbosity for debug messages */
#define DBG_VERB    7

#define DBG(info, format, ...) \
    xf86IDrvMsgVerb((info), X_INFO, DBG_VERB, "%s():%d: " format, \
        __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define PROBE_DBG(info, format, ...) \
    xf86IDrvMsgVerb((info), X_PROBED, DBG_VERB, "%s():%d: " format, \
        __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define CONFIG_DBG(info, format, ...) \
    xf86IDrvMsgVerb((info), X_CONFIG, DBG_VERB, "%s():%d: " format, \
        __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define ERR(info, ...) \
        xf86IDrvMsg((info), X_ERROR, ##__VA_ARGS__)


#define LONG_BITS (sizeof(long) * 8)

/* Number of longs needed to hold the given number of bits */
#define NLONGS(x) (((x) + LONG_BITS - 1) / LONG_BITS)

typedef struct {
    CmtProperties props;
    EventStateRec evstate;
    GestureRec gesture;
    GesturesProp* prop_list;

    char* device;
    long  handlers;

    /* kernel driver information */
    struct input_id id;
    char name[1024];
    unsigned long bitmask[NLONGS(EV_CNT)];
    unsigned long key_bitmask[NLONGS(KEY_CNT)];
    unsigned long key_state_bitmask[NLONGS(KEY_CNT)];
    unsigned long rel_bitmask[NLONGS(REL_CNT)];
    unsigned long abs_bitmask[NLONGS(ABS_CNT)];
    unsigned long led_bitmask[NLONGS(LED_CNT)];
    struct input_absinfo absinfo[ABS_CNT];
    unsigned long prop_bitmask[NLONGS(INPUT_PROP_CNT)];
    int is_monotonic:1;
    struct timeval before_sync_time;
    struct timeval after_sync_time;

    /*
     * For relative mode devices, the timestamp on Motion Events is reported
     * using a relative valuator.  To pass the absolute timestamp through the
     * X server, we track the previous timestamp, and send a delta.
     */
    unsigned int last_start_time;
    unsigned int last_end_time;
} CmtDeviceRec, *CmtDevicePtr;

#endif
