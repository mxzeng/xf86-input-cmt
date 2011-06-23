/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef _EVENT_H_
#define _EVENT_H_

#include <linux/input.h>

#include <xf86Xinput.h>

#include "mt.h"

typedef struct {
    int slot_min;
    int slot_max;
    MtSlotPtr slots;
    MtSlotPtr slot_current;

    struct input_absinfo* mt_axes[_ABS_MT_CNT];
} EventStateRec, *EventStatePtr;

int Event_IdentifyDevice(InputInfoPtr);
void Event_Process(InputInfoPtr, struct input_event*);

#endif
