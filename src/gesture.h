/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef _GESTURE_H_
#define _GESTURE_H_

#include <gestures/gestures.h>
#include <xf86.h>
#include <xf86Xinput.h>

#include "event.h"
#include "properties.h"

typedef struct {
    GestureInterpreter* interpreter;  /* The interpreter from Gestures lib */
} GestureRec, *GesturePtr;

int Gesture_Init(GesturePtr, InputInfoPtr);
void Gesture_Free(GesturePtr);

/*
 * Here we store the DeviceIntPtr which is used to perform gestures
 */
void Gesture_Device_On(GesturePtr, DeviceIntPtr);

/*
 * Here we cancel performing gestures, forgetting the DeviceIntPtr we were
 * passed earlier, if any.
 */
void Gesture_Device_Off(GesturePtr);

/*
 * Sends the current hardware state to the Gestures library.
 */
void Gesture_Process_Slots(GesturePtr, EventStatePtr, struct timeval*);

#endif
