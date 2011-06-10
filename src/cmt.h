/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CMT_H_
#define _CMT_H_

#include <xf86.h>

#include "properties.h"

/*
 * xf86IDrvMsg is not introduced until ABI12.
 * Until then, manually prepend our module name to the format string.
 */
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 12
#define xf86IDrvMsg(info, x, ...) \
    xf86Msg((x), "cmt: " __VA_ARGS__)
#endif


typedef struct {
    char* device;
    long  handlers;

    CmtProperties props;
} CmtDeviceRec, *CmtDevicePtr;

#endif
