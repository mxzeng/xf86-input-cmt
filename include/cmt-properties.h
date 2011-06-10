/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CMT_PROPERTIES_H_
#define _CMT_PROPERTIES_H_

/**
 * Descriptions of properties exported by the driver.
 *
 * CMT_PROP_* - Device property name, used with xinput set-prop
 */

/* 8 bit (BOOL) */
#define CMT_PROP_TAPTOCLICK "Tap To Click"

/* 32 bit */
#define CMT_PROP_MOTION_SPEED "Motion Speed"

/* 32 bit, 2 values, vertical, horizontal */
#define CMT_PROP_SCROLL_SPEED "Scroll Speed"

#endif
