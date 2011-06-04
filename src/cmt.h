/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CMT_H_
#define _CMT_H_

#include <xf86.h>

typedef struct {
    char* device;

    Bool tap_to_click;
} CmtDeviceRec, *CmtDevicePtr;

#endif
