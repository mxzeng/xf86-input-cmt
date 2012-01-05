/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef _GESTURE_H_
#define _GESTURE_H_

#include <gestures/gestures.h>

#include <xorg-server.h>
#include <xf86.h>
#include <xf86Xinput.h>

#include "event.h"
#include "properties.h"

typedef struct {
    GestureInterpreter* interpreter;  /* The interpreter from Gestures lib */
    DeviceIntPtr dev;
} GestureRec, *GesturePtr;

int Gesture_Init(GesturePtr);
void Gesture_Free(GesturePtr);

/*
 * Pass Device specific properties to gestures
 */
void Gesture_Device_Init(GesturePtr, DeviceIntPtr);

/*
 * Start performing gestures
 */
void Gesture_Device_On(GesturePtr);

/*
 * Here we cancel performing gestures, forgetting the DeviceIntPtr we were
 * passed earlier, if any.
 */
void Gesture_Device_Off(GesturePtr);

/*
 * Called to perform cleanup when the X server is closing the device.
 */
void Gesture_Device_Close(GesturePtr);

/*
 * Sends the current hardware state to the Gestures library.
 */
void Gesture_Process_Slots(GesturePtr, EventStatePtr, struct timeval*);

#endif
