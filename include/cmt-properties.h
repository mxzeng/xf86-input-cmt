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

/* 32 bit, 4 values, left, right, top, bottom */
#define CMT_PROP_AREA "Active Area"

/* 32 bit, 2 values, vertical, horizontal */
#define CMT_PROP_RES "Sensor Resolution"

#endif
