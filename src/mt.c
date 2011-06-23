/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mt.h"

#include <linux/input.h>

#include <xf86Xinput.h>

#include "cmt.h"

const char *mt_axis_names[] = {
    "Touch Major",
    "Touch Minor",
    "Width Major",
    "Width Minor",
    "Orientation",
    "Position X",
    "Position Y",
    "Tool Type",
    "Blob ID",
    "Tracking ID",
    "Pressure",
    "Distance",
};

static void MT_Slot_Print(InputInfoPtr, MtSlotPtr);

/**
 * MT Slot Accessors
 */

int
MT_Slot_Value_Get(MtSlotPtr slot, int code)
{
    switch (code) {
    case ABS_MT_TOUCH_MAJOR:
        return slot->touch_major;
    case ABS_MT_TOUCH_MINOR:
        return slot->touch_minor;
    case ABS_MT_WIDTH_MAJOR:
        return  slot->width_major;
    case ABS_MT_WIDTH_MINOR:
        return  slot->width_minor;
    case ABS_MT_ORIENTATION:
        return  slot->orientation;
    case ABS_MT_POSITION_X:
        return  slot->position_x;
    case ABS_MT_POSITION_Y:
        return  slot->position_y;
    case ABS_MT_TOOL_TYPE:
        return  slot->tool_type;
    case ABS_MT_BLOB_ID:
        return  slot->blob_id;
    case ABS_MT_TRACKING_ID:
        return slot->tracking_id;
    case ABS_MT_PRESSURE:
        return slot->pressure;
    case ABS_MT_DISTANCE:
        return slot->distance;
    default:
        return -1;
    }
}

void
MT_Slot_Value_Set(MtSlotPtr slot, int code, int value)
{
    switch (code) {
    case ABS_MT_TOUCH_MAJOR:
        slot->touch_major = value;
        break;
    case ABS_MT_TOUCH_MINOR:
        slot->touch_minor = value;
        break;
    case ABS_MT_WIDTH_MAJOR:
        slot->width_major = value;
        break;
    case ABS_MT_WIDTH_MINOR:
        slot->width_minor = value;
        break;
    case ABS_MT_ORIENTATION:
        slot->orientation = value;
        break;
    case ABS_MT_POSITION_X:
        slot->position_x = value;
        break;
    case ABS_MT_POSITION_Y:
        slot->position_y = value;
        break;
    case ABS_MT_TOOL_TYPE:
        slot->tool_type = value;
        break;
    case ABS_MT_BLOB_ID:
        slot->blob_id = value;
        break;
    case ABS_MT_TRACKING_ID:
        slot->tracking_id = value;
        break;
    case ABS_MT_PRESSURE:
        slot->pressure = value;
        break;
    case ABS_MT_DISTANCE:
        slot->distance = value;
        break;
    }
}

int
MTB_Init(InputInfoPtr info, int min, int max, int current)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    int num_slots;
    int i;

    evstate->slot_min = min;
    evstate->slot_max = max;

    num_slots = max - min + 1;
    evstate->slots = calloc(sizeof(MtSlotRec), num_slots);
    if (evstate->slots == NULL)
        return BadAlloc;

    for (i=0; i < num_slots; i++)
        evstate->slots[i].tracking_id = -1;

    MT_Slot_Set(info, current);

    return Success;
}

void
MT_Free(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;

    free(evstate->slots);
    evstate->slots = NULL;
}

void
MT_Slot_Set(InputInfoPtr info, int value)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;

    if (value < evstate->slot_min || value > evstate->slot_max) {
        evstate->slot_current = NULL;
        xf86IDrvMsg(info, X_ERROR, "MT Slot %d is out of range [%d .. %d].\n",
                    value, evstate->slot_min, evstate->slot_max);
        return;
    }

    evstate->slot_current = &evstate->slots[value - evstate->slot_min];
}


static void
MT_Slot_Print(InputInfoPtr info, MtSlotPtr slot)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    int i;

    if (slot == NULL)
        return;

    for (i = _ABS_MT_FIRST; i <= _ABS_MT_LAST; i++) {
        if (evstate->mt_axes[MT_CODE(i)] == NULL)
            continue;

        xf86IDrvMsg(info, X_INFO, "  %s = %d\n",
            mt_axis_names[MT_CODE(i)], MT_Slot_Value_Get(slot, i));
    }
}

void
MT_Print_Slots(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    int i;

    for (i = evstate->slot_min; i <= evstate->slot_max; i++) {
        MtSlotPtr slot = &evstate->slots[i - evstate->slot_min];
        if (slot->tracking_id == -1)
            continue;
        xf86IDrvMsg(info, X_INFO, "Slot %d:\n", i);
        MT_Slot_Print(info, slot);
    }
}
