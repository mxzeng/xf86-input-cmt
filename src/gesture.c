/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gesture.h"

#include <time.h>

#include <gestures/gestures.h>
#include <xorg/xf86_OSproc.h>

#include "cmt.h"
#include "properties.h"

#define MAX_VALUATORS 16

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
    void* callback_data;
    int is_monotonic:1;
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
Gesture_Init(GesturePtr rec, size_t max_fingers)
{
    rec->interpreter = NewGestureInterpreter();
    if (!rec->interpreter)
        return !Success;
    rec->fingers = malloc(max_fingers * sizeof(struct FingerState));
    if (!rec->fingers)
        goto Error_Alloc_Fingers;
    rec->mask = valuator_mask_new(MAX_VALUATORS);
    if (!rec->mask)
        goto Error_Alloc_Mask;
    return Success;

Error_Alloc_Mask:
    free(rec->fingers);
    rec->fingers = NULL;
Error_Alloc_Fingers:
    DeleteGestureInterpreter(rec->interpreter);
    rec->interpreter = NULL;
    return BadAlloc;
}

void
Gesture_Free(GesturePtr rec)
{
    // free gesture interpreter first, this will cancel all timers.
    DeleteGestureInterpreter(rec->interpreter);
    rec->interpreter = NULL;
    rec->dev = NULL;

    valuator_mask_free(&rec->mask);

    if (rec->fingers) {
        free(rec->fingers);
        rec->fingers = NULL;
    }
}

void
Gesture_Device_Init(GesturePtr rec, DeviceIntPtr dev)
{
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    CmtPropertiesPtr props = &cmt->props;
    EventStatePtr evstate = &cmt->evstate;
    struct HardwareProperties hwprops;
    EvdevPtr evdev = &cmt->evdev;

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
    hwprops.max_touch_cnt   = Event_Get_Touch_Count_Max(evdev);
    hwprops.supports_t5r2   = Event_Get_T5R2(evdev);
    hwprops.support_semi_mt = Event_Get_Semi_MT(evdev);
    /* buttonpad means a physical button under the touch surface */
    hwprops.is_button_pad   = Event_Get_Button_Pad(evdev);

    GestureInterpreterSetHardwareProperties(rec->interpreter, &hwprops);
    GestureInterpreterSetPropProvider(rec->interpreter, &prop_provider,
                                      rec->dev);
}

void
Gesture_Device_On(GesturePtr rec)
{
    GestureInterpreterSetTimerProvider(rec->interpreter,
                                       &Gesture_GesturesTimerProvider,
                                       rec->dev);
    GestureInterpreterSetCallback(rec->interpreter, &Gesture_Gesture_Ready,
                                  rec);
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
    GestureInterpreterSetTimerProvider(rec->interpreter, NULL, NULL);
}

void
Gesture_Process_Slots(void* vrec,
                      EventStatePtr evstate,
                      struct timeval* tv)
{
    GesturePtr rec = vrec;
    DeviceIntPtr dev = rec->dev;
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    EvdevPtr evdev = &cmt->evdev;
    int i;
    MtSlotPtr slot;
    struct HardwareState hwstate = { 0 };
    int current_finger;

    if (!rec->interpreter)
        return;

    /* zero initialize all FingerStates to clear out previous state. */
    memset(rec->fingers, 0,
           Event_Get_Slot_Count(evdev) * sizeof(struct FingerState));

    current_finger = 0;
    for (i = 0; i < evstate->slot_count; i++) {
        slot = &evstate->slots[i];
        if (slot->tracking_id == -1)
            continue;
        rec->fingers[current_finger].touch_major = (float)slot->touch_major;
        rec->fingers[current_finger].touch_minor = (float)slot->touch_minor;
        rec->fingers[current_finger].width_major = (float)slot->width_major;
        rec->fingers[current_finger].width_minor = (float)slot->width_minor;
        rec->fingers[current_finger].pressure    = (float)slot->pressure;
        rec->fingers[current_finger].orientation = (float)slot->orientation;
        rec->fingers[current_finger].position_x  = (float)slot->position_x;
        rec->fingers[current_finger].position_y  = (float)slot->position_y;
        rec->fingers[current_finger].tracking_id = slot->tracking_id;
        current_finger++;
    }
    hwstate.timestamp = StimeFromTimeval(tv);
    if (Event_Get_Button_Left(evdev))
        hwstate.buttons_down |= GESTURES_BUTTON_LEFT;
    if (Event_Get_Button_Middle(evdev))
        hwstate.buttons_down |= GESTURES_BUTTON_MIDDLE;
    if (Event_Get_Button_Right(evdev))
        hwstate.buttons_down |= GESTURES_BUTTON_RIGHT;
    hwstate.touch_cnt = Event_Get_Touch_Count(evdev);
    hwstate.finger_cnt = current_finger;
    hwstate.fingers = rec->fingers;
    GestureInterpreterPushHardwareState(rec->interpreter, &hwstate);
}

static void SetTimeValues(ValuatorMask* mask,
                          const struct Gesture* gesture,
                          DeviceIntPtr dev,
                          BOOL is_absolute)
{
    float start_float = gesture->start_time;
    float end_float = gesture->end_time;
    unsigned int start_int;
    unsigned int end_int;

    if (!is_absolute) {
        /*
         * We send the movement axes as relative values, which causes the
         * times to be sent as relative values too. This code computes the
         * right relative values.
         */
        start_float -= dev->last.valuators[CMT_AXIS_DBL_START_TIME];
        end_float -= dev->last.valuators[CMT_AXIS_DBL_END_TIME];
    }

    start_int = (unsigned long long)(1000.0L * start_float) & 0x0FFFFFFFFLL;
    end_int = (unsigned long long)(1000.0L * end_float) & 0x0FFFFFFFFLL;

    valuator_mask_set_double(mask, CMT_AXIS_DBL_START_TIME, start_float);
    valuator_mask_set_double(mask, CMT_AXIS_DBL_END_TIME, end_float);
    valuator_mask_set(mask, CMT_AXIS_START_TIME, start_int);
    valuator_mask_set(mask, CMT_AXIS_END_TIME, end_int);
}

static void SetFlingValues(ValuatorMask* mask,
                           const struct Gesture* gesture)
{
    float vx_float = gesture->details.fling.vx;
    float vy_float = gesture->details.fling.vy;

    unsigned int vx_int =
        (unsigned long long)(1000.0L * vx_float) & 0x0FFFFFFFFLL;
    unsigned int vy_int =
        (unsigned long long)(1000.0L * vy_float) & 0x0FFFFFFFFLL;

    valuator_mask_set_double(mask, CMT_AXIS_DBL_FLING_VX, vx_float);
    valuator_mask_set_double(mask, CMT_AXIS_DBL_FLING_VY, vy_float);
    valuator_mask_set(mask, CMT_AXIS_FLING_VX, vx_int);
    valuator_mask_set(mask, CMT_AXIS_FLING_VY, vy_int);
    valuator_mask_set(
        mask, CMT_AXIS_FLING_STATE, gesture->details.fling.fling_state);
}

static void Gesture_Gesture_Ready(void* client_data,
                                  const struct Gesture* gesture)
{
    GesturePtr rec = client_data;
    DeviceIntPtr dev = rec->dev;
    InputInfoPtr info = dev->public.devicePrivate;
    int button;

    DBG(info, "Gesture Start: %f End: %f \n",
        gesture->start_time, gesture->end_time);

    valuator_mask_zero(rec->mask);
    switch (gesture->type) {
        case kGestureTypeContactInitiated:
            /* TODO(adlr): handle contact initiated */
            break;
        case kGestureTypeMove:
            DBG(info, "Gesture Move: (%d, %d)\n",
                (int)gesture->details.move.dx, (int)gesture->details.move.dy);
            valuator_mask_set_double(
                rec->mask, CMT_AXIS_X, gesture->details.move.dx);
            valuator_mask_set_double(
                rec->mask, CMT_AXIS_Y, gesture->details.move.dy);
            SetTimeValues(rec->mask, gesture, dev, FALSE);
            xf86PostMotionEventM(dev, FALSE, rec->mask);
            break;
        case kGestureTypeScroll:
            DBG(info, "Gesture Scroll: (%f, %f)\n",
                gesture->details.scroll.dx, gesture->details.scroll.dy);
            valuator_mask_set_double(
                rec->mask, CMT_AXIS_SCROLL_X, gesture->details.scroll.dx);
            valuator_mask_set_double(
                rec->mask, CMT_AXIS_SCROLL_Y, gesture->details.scroll.dy);
            SetTimeValues(rec->mask, gesture, dev, TRUE);
            xf86PostMotionEventM(dev, TRUE, rec->mask);
            break;
        case kGestureTypeButtonsChange:
            DBG(info, "Gesture Button Change: down=0x%02x up=0x%02x\n",
                gesture->details.buttons.down, gesture->details.buttons.up);
            SetTimeValues(rec->mask, gesture, dev, TRUE);
            if (gesture->details.buttons.down & GESTURES_BUTTON_LEFT)
                xf86PostButtonEventM(dev, TRUE, CMT_BTN_LEFT, 1, rec->mask);
            if (gesture->details.buttons.down & GESTURES_BUTTON_MIDDLE)
                xf86PostButtonEventM(dev, TRUE, CMT_BTN_MIDDLE, 1, rec->mask);
            if (gesture->details.buttons.down & GESTURES_BUTTON_RIGHT)
                xf86PostButtonEventM(dev, TRUE, CMT_BTN_RIGHT, 1, rec->mask);
            if (gesture->details.buttons.up & GESTURES_BUTTON_LEFT)
                xf86PostButtonEventM(dev, TRUE, CMT_BTN_LEFT, 0, rec->mask);
            if (gesture->details.buttons.up & GESTURES_BUTTON_MIDDLE)
                xf86PostButtonEventM(dev, TRUE, CMT_BTN_MIDDLE, 0, rec->mask);
            if (gesture->details.buttons.up & GESTURES_BUTTON_RIGHT)
                xf86PostButtonEventM(dev, TRUE, CMT_BTN_RIGHT, 0, rec->mask);
            break;
        case kGestureTypeFling:
            DBG(info, "Gesture Fling: vx=%f vy=%f fling_state=%d\n",
                gesture->details.fling.vx, gesture->details.fling.vy,
                gesture->details.fling.fling_state);
            SetTimeValues(rec->mask, gesture, dev, TRUE);
            SetFlingValues(rec->mask, gesture);
            xf86PostMotionEventM(dev, TRUE, rec->mask);
            break;
        case kGestureTypeSwipe:
            DBG(info, "Gesture Swipe: dx=%f\n", gesture->details.swipe.dx);
            SetTimeValues(rec->mask, gesture, dev, TRUE);
            button = gesture->details.swipe.dx > 0.f ?
                CMT_BTN_FORWARD : CMT_BTN_BACK;
            xf86PostButtonEventM(dev, TRUE, button, 1, rec->mask);
            xf86PostButtonEventM(dev, TRUE, button, 0, rec->mask);
            break;
        default:
            ERR(info, "Unrecognized gesture type (%u)\n", gesture->type);
            break;
    }
}

static GesturesTimer*
Gesture_TimerCreate(void* provider_data)
{
    DeviceIntPtr dev = provider_data;
    InputInfoPtr info = dev->public.devicePrivate;
    CmtDevicePtr cmt = info->private;
    GesturesTimer* timer = (GesturesTimer*)calloc(1, sizeof(GesturesTimer));
    if (!timer)
        return NULL;
    timer->timer = TimerSet(NULL, 0, 0, NULL, 0);
    if (!timer->timer) {
        free(timer);
        return NULL;
    }
    timer->is_monotonic = cmt->evdev.info.is_monotonic;
    return timer;
}

static void
Gesture_TimerSet(void* provider_data,
                 GesturesTimer* timer,
                 stime_t delay,
                 GesturesTimerCallback callback,
                 void* callback_data)
{
    CARD32 ms = delay * 1000.0;

    if (!timer)
        return;
    timer->callback = callback;
    timer->callback_data = callback_data;
    if (ms == 0)
        ms = 1;
    TimerSet(timer->timer, 0, ms, Gesture_TimerCallback, timer);
}

static void
Gesture_TimerCancel(void* provider_data, GesturesTimer* timer)
{
    TimerCancel(timer->timer);
}

static void
Gesture_TimerFree(void* provider_data, GesturesTimer* timer)
{
    TimerFree(timer->timer);
    timer->timer = NULL;
    free(timer);
}

static CARD32
Gesture_TimerCallback(OsTimerPtr timer,
                      CARD32 millis,
                      pointer callback_data)
{
    int sigstate = xf86BlockSIGIO();
    GesturesTimer* tm = callback_data;
    stime_t now;
    stime_t rc;

    if (tm->is_monotonic) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      now = StimeFromTimespec(&ts);
    } else {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      now = StimeFromTimeval(&tv);
    }

    rc = tm->callback(now, tm->callback_data);
    if (rc >= 0.0) {
        CARD32 ms = rc * 1000.0;
        if (ms == 0)
            ms = 1;
        TimerSet(timer, 0, ms, Gesture_TimerCallback, tm);
    }

    xf86UnblockSIGIO(sigstate);
    return 0;
}

_X_EXPORT void gestures_log(int verb, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  if (verb > 0)
    xf86VDrvMsgVerb(-1, X_INFO, 7, fmt, args);
  else
    xf86VDrvMsgVerb(-1, X_ERROR, 0, fmt, args);
  va_end(args);
}
