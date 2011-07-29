/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gesture.h"

#include <gestures/gestures.h>

#include "cmt.h"
#include "properties.h"

static unsigned MT_XButtons_To_Gestures_Buttons(unsigned);

/*
 * Callback for Gestures library.
 */
static void Gesture_Gesture_Ready(void* client_data,
                                  const struct Gesture* gesture);

int
Gesture_Init(GesturePtr rec, InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;
    EventStatePtr evstate = &cmt->evstate;

    struct HardwareProperties hwprops;
    rec->interpreter = NewGestureInterpreter();
    if (!rec->interpreter)
        return !Success;

    /* TODO: support different models */
    hwprops.left            = props->area_left;
    hwprops.top             = props->area_top;
    hwprops.right           = props->area_right;
    hwprops.bottom          = props->area_bottom;
    hwprops.res_x           = props->res_x;
    hwprops.res_y           = props->res_y;
    hwprops.screen_x_dpi    = 133;
    hwprops.screen_y_dpi    = 133;
    hwprops.max_finger_cnt  = evstate->slot_count;
    hwprops.supports_t5r2   = Event_Get_T5R2(info);;
    hwprops.support_semi_mt = Event_Get_Semi_MT(info);
    /* buttonpad means a physical button under the touch surface */
    hwprops.is_button_pad   = Event_Get_Button_Pad(info);

    GestureInterpreterSetHardwareProperties(rec->interpreter, &hwprops);
    return Success;
}

void
Gesture_Free(GesturePtr rec)
{
    DeleteGestureInterpreter(rec->interpreter);
    rec->interpreter = NULL;
}

void
Gesture_Device_On(GesturePtr rec, DeviceIntPtr dev)
{
    GestureInterpreterSetCallback(rec->interpreter,
                                  &Gesture_Gesture_Ready,
                                  dev);
}

void
Gesture_Device_Off(GesturePtr rec)
{
    GestureInterpreterSetCallback(rec->interpreter, NULL, NULL);
}

static unsigned
MT_XButtons_To_Gestures_Buttons(unsigned xbuttons)
{
    unsigned ret = 0;
    if (xbuttons & BUTTON_LEFT)
        ret |= GESTURES_BUTTON_LEFT;
    if (xbuttons & BUTTON_MIDDLE)
        ret |= GESTURES_BUTTON_MIDDLE;
    if (xbuttons & BUTTON_RIGHT)
        ret |= GESTURES_BUTTON_RIGHT;
    return ret;
}

void
Gesture_Process_Slots(GesturePtr rec,
                      EventStatePtr evstate,
                      struct timeval* tv)
{
    int i;
    MtSlotPtr slot;
    struct FingerState fingers[evstate->slot_count];
    struct HardwareState hwstate = {
        StimeFromTimeval(tv),
        MT_XButtons_To_Gestures_Buttons(evstate->buttons),
        0,
        fingers
    };
    int current_finger;

    if (!rec->interpreter)
        return;

    current_finger = 0;
    for (i = 0; i < evstate->slot_count; i++) {
        slot = &evstate->slots[i];
        if (slot->tracking_id == -1)
            continue;
        fingers[current_finger].touch_major = (float)slot->touch_major;
        fingers[current_finger].touch_minor = (float)slot->touch_minor;
        fingers[current_finger].width_major = (float)slot->width_major;
        fingers[current_finger].width_minor = (float)slot->width_minor;
        fingers[current_finger].pressure    = (float)slot->pressure;
        fingers[current_finger].orientation = (float)slot->orientation;
        fingers[current_finger].position_x  = (float)slot->position_x;
        fingers[current_finger].position_y  = (float)slot->position_y;
        fingers[current_finger].tracking_id = slot->tracking_id;
        current_finger++;
    }
    hwstate.finger_cnt = current_finger;
    GestureInterpreterPushHardwareState(rec->interpreter, &hwstate);
}

static void Gesture_Gesture_Ready(void* client_data,
                                  const struct Gesture* gesture)
{
    /* TODO: These constants would be affected by X input driver
       button remapping. */
    const int kScrollBtnUp    = 4;
    const int kScrollBtnDown  = 5;
    const int kScrollBtnLeft  = 6;
    const int kScrollBtnRight = 7;
    DeviceIntPtr dev = client_data;
    InputInfoPtr info = dev->public.devicePrivate;
    int hscroll, vscroll;

    switch (gesture->type) {
        case kGestureTypeContactInitiated:
            /* TODO(adlr): handle contact initiated */
            break;
        case kGestureTypeMove:
            DBG(info, "Gesture Move: (%d, %d)\n",
                (int)gesture->details.move.dx, (int)gesture->details.move.dy);
            xf86PostMotionEvent(dev, 0, 0, 2,
                (int)gesture->details.move.dx, (int)gesture->details.move.dy);
            break;
        case kGestureTypeScroll:
            hscroll = (int)gesture->details.scroll.dx;
            vscroll = (int)gesture->details.scroll.dy;
            DBG(info, "Gesture Scroll: (%d, %d)\n", hscroll, vscroll);
            for (int type = 0; type < 2; type++) {
                int button = 0;
                int magnitude = 0;
                if (type == 0) {  /* hscroll */
                    magnitude = abs(hscroll);
                    button = hscroll < 0 ? kScrollBtnLeft : kScrollBtnRight;
                } else {  /* vscroll */
                    magnitude = abs(vscroll);
                    button = vscroll < 0 ? kScrollBtnUp : kScrollBtnDown;
                }
                for (int i = 0; i < magnitude; i++) {
                    xf86PostButtonEvent(dev, 0, button, 1, 0, 0);
                    xf86PostButtonEvent(dev, 0, button, 0, 0, 0);
                }
            }
            break;
        case kGestureTypeButtonsChange:
            DBG(info, "Gesture Button Change: down=0x%02x up=0x%02x\n",
                gesture->details.buttons.down, gesture->details.buttons.up);
            if (gesture->details.buttons.down & GESTURES_BUTTON_LEFT)
                xf86PostButtonEvent(dev, 0, 1, 1, 0, 0);
            if (gesture->details.buttons.down & GESTURES_BUTTON_MIDDLE)
                xf86PostButtonEvent(dev, 0, 2, 1, 0, 0);
            if (gesture->details.buttons.down & GESTURES_BUTTON_RIGHT)
                xf86PostButtonEvent(dev, 0, 3, 1, 0, 0);
            if (gesture->details.buttons.up & GESTURES_BUTTON_LEFT)
                xf86PostButtonEvent(dev, 0, 1, 0, 0, 0);
            if (gesture->details.buttons.up & GESTURES_BUTTON_MIDDLE)
                xf86PostButtonEvent(dev, 0, 2, 0, 0, 0);
            if (gesture->details.buttons.up & GESTURES_BUTTON_RIGHT)
                xf86PostButtonEvent(dev, 0, 3, 0, 0, 0);
            break;
    }
}
