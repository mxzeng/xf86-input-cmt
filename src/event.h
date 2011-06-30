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

#define BUTTON_LEFT                 0x01
#define BUTTON_RIGHT                0x02
#define BUTTON_MIDDLE               0x04

typedef struct {
    int slot_min;
    int slot_max;
    MtSlotPtr slots;
    MtSlotPtr slot_current;

    struct input_absinfo* mt_axes[_ABS_MT_CNT];

    unsigned buttons;
} EventStateRec, *EventStatePtr;

int Event_Init(InputInfoPtr);
void Event_Free(InputInfoPtr);
void Event_Process(InputInfoPtr, struct input_event*);

int Event_Get_Left(InputInfoPtr);
int Event_Get_Right(InputInfoPtr);
int Event_Get_Top(InputInfoPtr);
int Event_Get_Bottom(InputInfoPtr);

/* Some useful bit twiddling routines */
static inline unsigned
Bit_Set(unsigned start, unsigned mask)
{
    return start | mask;
}

static inline unsigned
Bit_Clr(unsigned start, unsigned mask)
{
    return start & ~mask;
}

static inline unsigned
Bit_Flip(unsigned start, unsigned mask)
{
    return start ^ mask;
}

static inline unsigned
Bit_Assign(unsigned start, unsigned mask, unsigned val)
{
    return val ? (start | mask) : (start & ~mask);
}

#endif
