/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _PROPERTIES_H_
#define _PROPERTIES_H_

#include <xf86.h>
#include <xf86Xinput.h>

/**
 * Descriptions of xorg configuration variables for the driver.
 *
 * CMT_CONF_* - Device configuration setting name, used in .conf files
 * CMT_DEF_*  - Device configuration property default value,
 *              used if not set in .conf file.
 */

#define CMT_CONF_TAPTOCLICK "Tap To Click"
#define CMT_DEF_TAPTOCLICK  FALSE

#define CMT_CONF_MOTION_SPEED "Motion Speed"
#define CMT_DEF_MOTION_SPEED  100

#define CMT_CONF_SCROLL_SPEED_V "Vertical Scroll Speed"
#define CMT_DEF_SCROLL_SPEED_V  100

#define CMT_CONF_SCROLL_SPEED_H "Horizontal Scroll Speed"
#define CMT_DEF_SCROLL_SPEED_H  100

#define CMT_CONF_AREA_LEFT   "Left"
#define CMT_DEF_AREA_LEFT     0

#define CMT_CONF_AREA_RIGHT  "Right"
#define CMT_DEF_AREA_RIGHT    1024

#define CMT_CONF_AREA_TOP    "Top"
#define CMT_DEF_AREA_TOP      0

#define CMT_CONF_AREA_BOTTOM "Bottom"
#define CMT_DEF_AREA_BOTTOM   512


typedef struct {
    Bool tap_to_click;
    int  motion_speed;
    int  scroll_speed_v;
    int  scroll_speed_h;
    int  area_left;
    int  area_right;
    int  area_top;
    int  area_bottom;
} CmtProperties, *CmtPropertiesPtr;

int PropertyInit(DeviceIntPtr);
void ProcessConfOptions(InputInfoPtr, pointer);

#endif
