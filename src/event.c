/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "event.h"

#include <linux/input.h>

#include <xf86Xinput.h>

#include "cmt.h"

/**
 * Process Input Events
 */
void
Event_Process(InputInfoPtr info, struct input_event* ev)
{
    xf86IDrvMsg(info, X_INFO, "@ %ld.%06ld %u[%u] = %d\n",
                ev->time.tv_sec, ev->time.tv_usec, ev->type, ev->code,
                ev->value);
}
