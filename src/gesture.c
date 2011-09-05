/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gesture.h"

#include <gestures/gestures.h>
#include <xorg/xf86_OSproc.h>

#include "cmt.h"
#include "properties.h"

static unsigned MT_XButtons_To_Gestures_Buttons(unsigned);

/*
 * Gestures timer functions
 */
static GesturesTimer* Gesture_TimerCreate(void*);
static void Gesture_TimerSet(void*,
                             GesturesTimer*,
                             stime_t,
                             GesturesTimerCallback,
                             void*);
static void Gesture_TimerCancel(void*, GesturesTimer*);
static void Gesture_TimerFree(void*, GesturesTimer*);

static CARD32 Gesture_TimerCallback(OsTimerPtr, CARD32, pointer);

struct GesturesTimer {
    OsTimerPtr timer;
    GesturesTimerCallback callback;
    void* data;
};

static GesturesTimerProvider Gesture_GesturesTimerProvider = {
    .create_fn = Gesture_TimerCreate,
    .set_fn = Gesture_TimerSet,
    .cancel_fn = Gesture_TimerCancel,
    .free_fn = Gesture_TimerFree
};

/*
 * Callback for Gestures library.
 */
static void Gesture_Gesture_Ready(void* client_data,
                                  const struct Gesture* gesture);

int
Gesture_Init(GesturePtr rec)
{
    rec->interpreter = NewGestureInterpreter();
    if (!rec->interpreter)
        return !Success;
    GestureInterpreterSetTimerProvider(rec->interpreter,
                                       &Gesture_GesturesTimerProvider,
                                       NULL);

    return Success;
}

void
Gesture_Free(GesturePtr rec)
{
    GestureInterpreterSetTimerProvider(rec->interpreter, NULL, NULL);
    DeleteGestureInterpreter(rec->interpreter);
    rec->interpreter = NULL;
    rec->dev = NULL;
}

void
Gesture_Device_Init(GesturePtr rec, DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;
    EventStatePtr evstate = &cmt->evstate;
    struct HardwareProperties hwprops;

    /* Store the device for which to generate gestures */
    rec->dev = dev;

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
    hwprops.max_touch_cnt   = Event_Get_Touch_Count(info);
    hwprops.supports_t5r2   = Event_Get_T5R2(info);
    hwprops.support_semi_mt = Event_Get_Semi_MT(info);
    /* buttonpad means a physical button under the touch surface */
    hwprops.is_button_pad   = Event_Get_Button_Pad(info);

    GestureInterpreterSetHardwareProperties(rec->interpreter, &hwprops);
    GestureInterpreterSetPropProvider(rec->interpreter, &prop_provider,
                                      rec->dev);
}

void
Gesture_Device_On(GesturePtr rec)
{
    GestureInterpreterSetCallback(rec->interpreter, &Gesture_Gesture_Ready,
                                  rec->dev);
}

void
Gesture_Device_Off(GesturePtr rec)
{
    GestureInterpreterSetCallback(rec->interpreter, NULL, NULL);
}

void
Gesture_Device_Close(GesturePtr rec)
{
    GestureInterpreterSetPropProvider(rec->interpreter, NULL, NULL);
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
        evstate->touch_cnt,
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
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;
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
            if (props->scroll_axes)
                xf86PostMotionEvent(dev, 0, 2, 2, vscroll, hscroll);
            if (props->scroll_btns) {
                int button;
                int magnitude;
                button = hscroll < 0 ? kScrollBtnLeft : kScrollBtnRight;
                magnitude = hscroll < 0 ? -hscroll : hscroll;
                for (int i = 0; i < magnitude; i++) {
                    xf86PostButtonEvent(dev, 0, button, 1, 0, 0);
                    xf86PostButtonEvent(dev, 0, button, 0, 0, 0);
                }
                button = vscroll < 0 ? kScrollBtnUp : kScrollBtnDown;
                magnitude = vscroll < 0 ? -vscroll : vscroll;
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

static GesturesTimer*
Gesture_TimerCreate(void* unused)
{
    GesturesTimer* ret = (GesturesTimer*)calloc(1, sizeof(GesturesTimer));
    if (!ret)
        return NULL;
    ret->timer = TimerSet(NULL, 0, 0, NULL, 0);
    if (!ret->timer) {
        free(ret);
        return NULL;
    }
    return ret;
}

static void
Gesture_TimerSet(void* unused,
                 GesturesTimer* timer,
                 stime_t delay,
                 GesturesTimerCallback callback,
                 void* data)
{
    CARD32 ms = delay * 1000.0;

    if (!timer)
        return;
    timer->callback = callback;
    timer->data = data;
    if (ms == 0)
        ms = 1;
    TimerSet(timer->timer, 0, ms, Gesture_TimerCallback, timer);
}

static void
Gesture_TimerCancel(void* unused, GesturesTimer* timer)
{
    TimerCancel(timer->timer);
}

static void
Gesture_TimerFree(void* unused, GesturesTimer* timer)
{
    TimerFree(timer->timer);
    timer->timer = NULL;
    free(timer);
}

static CARD32
Gesture_TimerCallback(OsTimerPtr timer,
                      CARD32 millis,
                      pointer data)
{
    int sigstate = xf86BlockSIGIO();
    GesturesTimer* tm = (GesturesTimer*)data;
    struct timeval tv;
    stime_t now;
    stime_t rc;

    gettimeofday(&tv, NULL);
    now = StimeFromTimeval(&tv);

    rc = tm->callback(now, tm->data);
    if (rc >= 0.0) {
        CARD32 ms = rc * 1000.0;
        if (ms == 0)
            ms = 1;
        TimerSet(timer, 0, ms, Gesture_TimerCallback, tm);
    }

    xf86UnblockSIGIO(sigstate);
    return 0;
}
