/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef _LIBEVDEV_EVENT_H_
#define _LIBEVDEV_EVENT_H_

#include "libevdev_log.h"
#include "libevdev_mt.h"


/* 1 MiB debug buffer of struct input_event objects */
#define DEBUG_BUF_SIZE      65536


typedef struct {
    int slot_min;
    int slot_count;
    MtSlotPtr slots;
    MtSlotPtr slot_current;

    struct input_absinfo* mt_axes[_ABS_MT_CNT];

    /* Log of recent input_event structs for debugging */
    struct input_event debug_buf[DEBUG_BUF_SIZE];
    size_t debug_buf_tail;
} EventStateRec, *EventStatePtr;

int Event_Init(EvDevicePtr);
void Event_Free(EvDevicePtr);
void Event_Open(EvDevicePtr);
bool Event_Process(EvDevicePtr, struct input_event*);
void Event_Dump_Debug_Log(void *);

int Event_Get_Left(EvDevicePtr);
int Event_Get_Right(EvDevicePtr);
int Event_Get_Top(EvDevicePtr);
int Event_Get_Bottom(EvDevicePtr);
int Event_Get_Res_Y(EvDevicePtr);
int Event_Get_Res_X(EvDevicePtr);
int Event_Get_Button_Pad(EvDevicePtr);
int Event_Get_Semi_MT(EvDevicePtr);
int Event_Get_T5R2(EvDevicePtr);
int Event_Get_Touch_Count(EvDevicePtr);
int Event_Get_Touch_Count_Max(EvDevicePtr);
int Event_Get_Slot_Count(EvDevicePtr);
int Event_Get_Button_Left(EvDevicePtr);
int Event_Get_Button_Middle(EvDevicePtr);
int Event_Get_Button_Right(EvDevicePtr);
void Event_Sync_State(EvDevicePtr);

#endif
