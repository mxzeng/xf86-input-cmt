/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef _EVENT_H_
#define _EVENT_H_

#include <linux/input.h>

#include <xorg-server.h>
#include <xf86Xinput.h>

#include "mt.h"

#define BUTTON_LEFT                 0x01
#define BUTTON_RIGHT                0x02
#define BUTTON_MIDDLE               0x04

/* 1 MiB debug buffer of struct input_event objects */
#define DEBUG_BUF_SIZE      65536

typedef struct {
    int slot_min;
    int slot_count;
    MtSlotPtr slots;
    MtSlotPtr slot_current;

    struct input_absinfo* mt_axes[_ABS_MT_CNT];

    unsigned long buttons;
    unsigned touch_cnt;

    /* Log of recent input_event structs for debugging */
    struct input_event debug_buf[DEBUG_BUF_SIZE];
    size_t debug_buf_tail;
} EventStateRec, *EventStatePtr;

int Event_Init(InputInfoPtr);
void Event_Free(InputInfoPtr);
void Event_Open(InputInfoPtr);
Bool Event_Process(InputInfoPtr, struct input_event*);
void Event_Dump_Debug_Log(void *);

int Event_Get_Left(InputInfoPtr);
int Event_Get_Right(InputInfoPtr);
int Event_Get_Top(InputInfoPtr);
int Event_Get_Bottom(InputInfoPtr);
int Event_Get_Res_Y(InputInfoPtr);
int Event_Get_Res_X(InputInfoPtr);
int Event_Get_Button_Pad(InputInfoPtr);
int Event_Get_Semi_MT(InputInfoPtr);
int Event_Get_T5R2(InputInfoPtr);
int Event_Get_Touch_Count(InputInfoPtr);
int Event_Get_Touch_Count_Max(InputInfoPtr);
int Event_Get_Slot_Count(InputInfoPtr);
void Event_Sync_State(InputInfoPtr);

#endif
