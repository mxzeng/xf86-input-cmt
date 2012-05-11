/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "event.h"

#include <errno.h>
#include <linux/input.h>
#include <time.h>

#include "cmt.h"
#include "mt.h"

#ifndef BTN_TOOL_QUINTTAP
#define BTN_TOOL_QUINTTAP  0x148  /* Five fingers on trackpad */
#endif

/* Set clockid to be used for timestamps */
#ifndef EVIOCSCLOCKID
#define EVIOCSCLOCKID  _IOW('E', 0xa0, int)
#endif

#ifndef EVIOCGMTSLOTS
#define EVIOCGMTSLOTS(len)  _IOC(_IOC_READ, 'E', 0x0a, len)
#endif

/* SYN_DROPPED added in kernel v2.6.38-rc4 */
#ifndef SYN_DROPPED
#define SYN_DROPPED  3
#endif

static inline void AssignBit(unsigned long*, int, int);
static inline Bool TestBit(int, unsigned long*);

static void Absinfo_Print(InputInfoPtr, struct input_absinfo*);
static void Event_Print(InputInfoPtr, struct input_event*);

static void Event_Syn(InputInfoPtr, struct input_event*);
static void Event_Syn_Report(InputInfoPtr, struct input_event*);
static void Event_Syn_MT_Report(InputInfoPtr, struct input_event*);

static void Event_Key(InputInfoPtr, struct input_event*);

static void Event_Abs(InputInfoPtr, struct input_event*);
static void Event_Abs_MT(InputInfoPtr, struct input_event*);
static void SemiMtSetAbsPressure(InputInfoPtr, struct input_event*);
static void Event_Sync_Keys(InputInfoPtr);
static void Event_Get_Time(struct timeval*, Bool);

/**
 * Helper functions
 */
static inline Bool
TestBit(int bit, unsigned long* array)
{
    return !!(array[bit / LONG_BITS] & (1L << (bit % LONG_BITS)));
}

static inline void
AssignBit(unsigned long* array, int bit, int value)
{
    unsigned long mask = (1L << (bit % LONG_BITS));
    if (value)
        array[bit / LONG_BITS] |= mask;
    else
        array[bit / LONG_BITS] &= ~mask;
}

/**
 * Input Device Event Property accessors
 */
int
Event_Get_Left(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_X];
    return absinfo->minimum;
}

int
Event_Get_Right(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_X];
    return absinfo->maximum;
}

int
Event_Get_Top(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_Y];
    return absinfo->minimum;
}

int
Event_Get_Bottom(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_Y];
    return absinfo->maximum;
}

int
Event_Get_Res_Y(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_Y];
    return absinfo->resolution;
}

int
Event_Get_Res_X(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    struct input_absinfo* absinfo = &cmt->absinfo[ABS_X];
    return absinfo->resolution;
}

int
Event_Get_Button_Pad(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    return TestBit(INPUT_PROP_BUTTONPAD, cmt->prop_bitmask);
}

int
Event_Get_Semi_MT(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    return TestBit(INPUT_PROP_SEMI_MT, cmt->prop_bitmask);
}

int
Event_Get_T5R2(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    if (Event_Get_Semi_MT(info))
        return 0;
    return (Event_Get_Touch_Count_Max(info) > evstate->slot_count);
}

int
Event_Get_Touch_Count_Max(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;

    if (TestBit(BTN_TOOL_QUINTTAP, cmt->key_bitmask))
        return 5;
    if (TestBit(BTN_TOOL_QUADTAP, cmt->key_bitmask))
        return 4;
    if (TestBit(BTN_TOOL_TRIPLETAP, cmt->key_bitmask))
        return 3;
    if (TestBit(BTN_TOOL_DOUBLETAP, cmt->key_bitmask))
        return 2;
    return 1;
}

static void
Event_Sync_Keys(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    int len = sizeof(cmt->key_state_bitmask);

    memset(cmt->key_state_bitmask, 0, len);
    if (ioctl(info->fd, EVIOCGKEY(len), cmt->key_state_bitmask) < 0)
        ERR(info, "ioctl EVIOCGKEY failed: %s\n", strerror(errno));
}

int
Event_Get_Touch_Count(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;

    if (TestBit(BTN_TOOL_QUINTTAP, cmt->key_state_bitmask))
        return 5;
    if (TestBit(BTN_TOOL_QUADTAP, cmt->key_state_bitmask))
        return 4;
    if (TestBit(BTN_TOOL_TRIPLETAP, cmt->key_state_bitmask))
        return 3;
    if (TestBit(BTN_TOOL_DOUBLETAP, cmt->key_state_bitmask))
        return 2;
    if (TestBit(BTN_TOOL_FINGER, cmt->key_state_bitmask))
        return 1;
    return 0;
}

int
Event_Get_Slot_Count(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    return evstate->slot_count;
}

int
Event_Get_Button_Left(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    return TestBit(BTN_LEFT, cmt->key_state_bitmask);
}

int
Event_Get_Button_Middle(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    return TestBit(BTN_MIDDLE, cmt->key_state_bitmask);
}

int
Event_Get_Button_Right(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    return TestBit(BTN_RIGHT, cmt->key_state_bitmask);
}

static int
Event_Enable_Monotonic(InputInfoPtr info)
{
    unsigned int clk = CLOCK_MONOTONIC;
    return (ioctl(info->fd, EVIOCSCLOCKID, &clk) == 0) ? Success : !Success;
}

#define CASE_RETURN(s) \
    case (s):\
        return #s


static const char *
Event_To_String(int type, int code) {
    switch (type) {
    case EV_SYN:
        switch (code) {
        CASE_RETURN(SYN_REPORT);
        CASE_RETURN(SYN_MT_REPORT);
        default:
            break;
        }
        break;
    case EV_ABS:
        switch (code) {
        CASE_RETURN(ABS_X);
        CASE_RETURN(ABS_Y);
        CASE_RETURN(ABS_Z);
        CASE_RETURN(ABS_PRESSURE);
        CASE_RETURN(ABS_TOOL_WIDTH);
        CASE_RETURN(ABS_MT_TOUCH_MAJOR);
        CASE_RETURN(ABS_MT_TOUCH_MINOR);
        CASE_RETURN(ABS_MT_WIDTH_MAJOR);
        CASE_RETURN(ABS_MT_WIDTH_MINOR);
        CASE_RETURN(ABS_MT_ORIENTATION);
        CASE_RETURN(ABS_MT_POSITION_X);
        CASE_RETURN(ABS_MT_POSITION_Y);
        CASE_RETURN(ABS_MT_TOOL_TYPE);
        CASE_RETURN(ABS_MT_BLOB_ID);
        CASE_RETURN(ABS_MT_TRACKING_ID);
        CASE_RETURN(ABS_MT_PRESSURE);
        CASE_RETURN(ABS_MT_SLOT);
        default:
            break;
        }
        break;
    case EV_KEY:
        switch (code) {
        CASE_RETURN(BTN_LEFT);
        CASE_RETURN(BTN_RIGHT);
        CASE_RETURN(BTN_MIDDLE);
        CASE_RETURN(BTN_TOUCH);
        CASE_RETURN(BTN_TOOL_FINGER);
        CASE_RETURN(BTN_TOOL_DOUBLETAP);
        CASE_RETURN(BTN_TOOL_TRIPLETAP);
        CASE_RETURN(BTN_TOOL_QUADTAP);
        CASE_RETURN(BTN_TOOL_QUINTTAP);
        default:
            break;
        }
        break;
    default:
        break;
    }
    return "?";
}
#undef CASE_RETURN

static const char *
Event_Type_To_String(int type) {
    switch (type) {
    case EV_SYN: return "SYN";
    case EV_KEY: return "KEY";
    case EV_REL: return "REL";
    case EV_ABS: return "ABS";
    case EV_MSC: return "MSC";
    case EV_SW: return "SW";
    case EV_LED: return "LED";
    case EV_SND: return "SND";
    case EV_REP: return "REP";
    case EV_FF: return "FF";
    case EV_PWR: return "PWR";
    default: return "?";
    }
}

static const char *
Event_Property_To_String(int type) {
    switch (type) {
    case INPUT_PROP_POINTER: return "POINTER";      /* needs a pointer */
    case INPUT_PROP_DIRECT: return "DIRECT";        /* direct input devices */
    case INPUT_PROP_BUTTONPAD: return "BUTTONPAD";  /* has button under pad */
    case INPUT_PROP_SEMI_MT: return "SEMI_MT";      /* touch rectangle only */
    default: return "?";
    }
}

/**
 * Probe Device Input Event Support
 */
int
Event_Init(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    int i;
    int len;

    if (ioctl(info->fd, EVIOCGID, &cmt->id) < 0) {
         ERR(info, "ioctl EVIOCGID failed: %s\n", strerror(errno));
         return !Success;
    }
    xf86IDrvMsg(info, X_PROBED,
        "vendor: %02X, product: %02X\n", cmt->id.vendor, cmt->id.product);

    if (ioctl(info->fd, EVIOCGNAME(sizeof(cmt->name) - 1), cmt->name) < 0) {
        ERR(info, "ioctl EVIOCGNAME failed: %s\n", strerror(errno));
        return !Success;
    }
    xf86IDrvMsg(info, X_PROBED, "name: %s\n", cmt->name);

    len = ioctl(info->fd, EVIOCGPROP(sizeof(cmt->prop_bitmask)),
                cmt->prop_bitmask);
    if (len < 0) {
        ERR(info, "ioctl EVIOCGPROP failed: %s\n", strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->prop_bitmask))
            PROBE_DBG(info, "Has Property: %d (%s)\n", i,
                      Event_Property_To_String(i));
    }

    len = ioctl(info->fd, EVIOCGBIT(0, sizeof(cmt->bitmask)), cmt->bitmask);
    if (len < 0) {
        ERR(info, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->bitmask))
            PROBE_DBG(info, "Has Event Type %d = %s\n", i,
                      Event_Type_To_String(i));
    }

    len = ioctl(info->fd, EVIOCGBIT(EV_KEY, sizeof(cmt->key_bitmask)),
                cmt->key_bitmask);
    if (len < 0) {
        ERR(info, "ioctl EVIOCGBIT(EV_KEY) failed: %s\n", strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->key_bitmask))
            PROBE_DBG(info, "Has KEY[%d] = %s\n", i,
                      Event_To_String(EV_KEY, i));
    }

    len = ioctl(info->fd, EVIOCGBIT(EV_LED, sizeof(cmt->led_bitmask)),
                cmt->led_bitmask);
    if (len < 0) {
        ERR(info, "ioctl EVIOCGBIT(EV_LED) failed: %s\n", strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->led_bitmask))
            PROBE_DBG(info, "Has LED[%d] = %s\n", i,
                      Event_To_String(EV_LED, i));
    }

    len = ioctl(info->fd, EVIOCGBIT(EV_REL, sizeof(cmt->rel_bitmask)),
                cmt->rel_bitmask);
    if (len < 0) {
        ERR(info, "ioctl EVIOCGBIT(EV_REL) failed: %s\n", strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, cmt->rel_bitmask))
            PROBE_DBG(info, "Has REL[%d] = %s\n", i,
                      Event_To_String(EV_REL, i));
    }

    /*
     * TODO(djkurtz): Solve the race condition between MT slot initialization
     *    from absinfo, and incoming/lost input events.
     *    Specifically, if kernel driver sends MT_SLOT event between absinfo
     *    probe and when we start listening for input events.
     */

    len = ioctl(info->fd, EVIOCGBIT(EV_ABS, sizeof(cmt->abs_bitmask)),
                cmt->abs_bitmask);
    if (len < 0) {
        ERR(info, "ioctl EVIOCGBIT(EV_ABS) failed: %s\n", strerror(errno));
        return !Success;
    }

    for (i = ABS_X; i <= ABS_MAX; i++) {
        if (TestBit(i, cmt->abs_bitmask)) {
            struct input_absinfo* absinfo = &cmt->absinfo[i];
            PROBE_DBG(info, "Has ABS[%d] = %s\n", i,
                      Event_To_String(EV_ABS, i));
            len = ioctl(info->fd, EVIOCGABS(i), absinfo);
            if (len < 0) {
                ERR(info, "ioctl EVIOCGABS(%d) failed: %s\n", i,
                    strerror(errno));
                /*
                 * Clean up in case where error happens after MTB_Init() has
                 * already allocated slots.
                 */
                goto error_MT_Free;
            }

            Absinfo_Print(info, absinfo);

            if (i == ABS_MT_SLOT) {
                int rc;
                rc = MTB_Init(info, absinfo->minimum, absinfo->maximum,
                              absinfo->value);
                if (rc != Success)
                    return rc;
            } else if (IS_ABS_MT(i)) {
                evstate->mt_axes[MT_CODE(i)] = absinfo;
            }
        }
    }

    /* Synchronize all MT slots with kernel evdev driver */
    Event_Sync_State(info);
    return Success;

error_MT_Free:
    MT_Free(info);
    return !Success;
}

void
Event_Free(InputInfoPtr info)
{
    MT_Free(info);
}

void
Event_Open(InputInfoPtr info)
{
    CmtDevicePtr cmt = info->private;

    /* Select monotonic input event timestamps, if supported by kernel */
    cmt->is_monotonic = (Event_Enable_Monotonic(info) == Success);
    /* Reset the sync time variables */
    Event_Get_Time(&cmt->before_sync_time, cmt->is_monotonic);
    Event_Get_Time(&cmt->after_sync_time, cmt->is_monotonic);
    xf86IDrvMsg(info, X_PROBED, "Using %s input event time stamps\n",
                cmt->is_monotonic ? "monotonic" : "realtime");
}

/**
 * Debug Print Helper Functions
 */
static void
Absinfo_Print(InputInfoPtr info, struct input_absinfo* absinfo)
{
    PROBE_DBG(info, "    min = %d\n", absinfo->minimum);
    PROBE_DBG(info, "    max = %d\n", absinfo->maximum);
    if (absinfo->fuzz)
        PROBE_DBG(info, "    fuzz = %d\n", absinfo->fuzz);
    if (absinfo->resolution)
        PROBE_DBG(info, "    res = %d\n", absinfo->resolution);
}

static void
Event_Get_Time(struct timeval *t, Bool use_monotonic) {
    struct timespec now;
    clockid_t clockid = (use_monotonic) ? CLOCK_MONOTONIC : CLOCK_REALTIME;

    clock_gettime(clockid, &now);
    t->tv_sec = now.tv_sec;
    t->tv_usec = now.tv_nsec / 1000;
}

/**
 * Synchronize the current state with kernel evdev driver. For cmt, there are
 * only four components required to be synced: current touch count, the MT
 * slots information, current slot id and physical button states. However, as
 * pressure readings are missing in ABS_MT_PRESSURE field of MT slots for
 * semi_mt touchpad device (e.g. Cr48), we also need need to extract it with
 * extra EVIOCGABS query.
 */
void
Event_Sync_State(InputInfoPtr info)
{
    int i;
    struct input_absinfo* absinfo;
    CmtDevicePtr cmt = info->private;

    Event_Get_Time(&cmt->before_sync_time, cmt->is_monotonic);

    Event_Sync_Keys(info);

    /* Get current pressure information for semi_mt device */
    if (Event_Get_Semi_MT(info)) {
        absinfo = &cmt->absinfo[ABS_PRESSURE];
        if (ioctl(info->fd, EVIOCGABS(ABS_PRESSURE), absinfo) < 0) {
            ERR(info, "ioctl EVIOCGABS(ABS_PRESSURE) failed: %s\n",
                strerror(errno));
        } else {
            struct input_event ev;
            ev.code = ABS_PRESSURE;
            ev.value = absinfo->value;
            SemiMtSetAbsPressure(info, &ev);
        }
    }

    /* TODO(cywang): Sync all ABS_ states for completeness */

    /* Get current MT information for each slot */
    for (i = _ABS_MT_FIRST; i <= _ABS_MT_LAST; i++) {
        MTSlotInfo req;

        if (!TestBit(i, cmt->abs_bitmask))
            continue;
        /*
         * TODO(cywang): Scale the size of slots in MTSlotInfo based on the
         *    evstate->slot_count.
         */

        req.code = i;
        if (ioctl(info->fd, EVIOCGMTSLOTS((sizeof(req))), &req) < 0) {
            ERR(info, "ioctl EVIOCGMTSLOTS(req.code=%d) failed: %s\n", i,
                strerror(errno));
            continue;
        }
        MT_Slot_Sync(info, &req);
    }

    /* Get current slot id */
    absinfo = &cmt->absinfo[ABS_MT_SLOT];
    if (ioctl(info->fd, EVIOCGABS(ABS_MT_SLOT), absinfo) < 0)
        ERR(info, "ioctl EVIOCGABS(ABS_MT_SLOT) failed: %s\n", strerror(errno));
    else
        MT_Slot_Set(info, absinfo->value);

    Event_Get_Time(&cmt->after_sync_time, cmt->is_monotonic);
    xf86IDrvMsg(info, X_PROBED,
                "Event_Sync_State: before %ld.%ld after %ld.%ld\n",
                cmt->before_sync_time.tv_sec, cmt->before_sync_time.tv_usec,
                cmt->after_sync_time.tv_sec, cmt->after_sync_time.tv_usec);
}

static void
Event_Print(InputInfoPtr info, struct input_event* ev)
{
    switch (ev->type) {
    case EV_SYN:
        switch (ev->code) {
        case SYN_REPORT:
            DBG(info, "@ %ld.%06ld  ---------- SYN_REPORT -------\n",
                ev->time.tv_sec, ev->time.tv_usec);
            return;
        case SYN_MT_REPORT:
            DBG(info, "@ %ld.%06ld  ........ SYN_MT_REPORT ......\n",
                ev->time.tv_sec, ev->time.tv_usec);
            return;
        case SYN_DROPPED:
            ERR(info, "@ %ld.%06ld  ++++++++ SYN_DROPPED ++++++++\n",
                ev->time.tv_sec, ev->time.tv_usec);
            return;
        default:
            ERR(info, "@ %ld.%06ld  ?????? SYN_UNKNOWN (%d) ?????\n",
                ev->time.tv_sec, ev->time.tv_usec, ev->code);
            return;
        }
        break;
    case EV_ABS:
        if (ev->code == ABS_MT_SLOT) {
            DBG(info, "@ %ld.%06ld  .......... MT SLOT %d ........\n",
                ev->time.tv_sec, ev->time.tv_usec, ev->value);
            return;
        }
        break;
    default:
        break;
    }

    DBG(info, "@ %ld.%06ld %s[%d] (%s) = %d\n",
        ev->time.tv_sec, ev->time.tv_usec, Event_Type_To_String(ev->type),
        ev->code, Event_To_String(ev->type, ev->code), ev->value);
}

/**
 * Process Input Events
 */
Bool
Event_Process(InputInfoPtr info, struct input_event* ev)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;

    Event_Print(info, ev);
    if (evstate->debug_buf) {
        evstate->debug_buf[evstate->debug_buf_tail] = *ev;
        evstate->debug_buf_tail =
            (evstate->debug_buf_tail + 1) % DEBUG_BUF_SIZE;
    }

    switch (ev->type) {
    case EV_SYN:
        if (ev->code == SYN_DROPPED)
            return TRUE;
        Event_Syn(info, ev);
        break;

    case EV_KEY:
        Event_Key(info, ev);
        break;

    case EV_ABS:
        Event_Abs(info, ev);
        break;

    default:
        break;
    }
    return FALSE;
}

/**
 * Dump the log of input events to disk
 */
void
Event_Dump_Debug_Log(void* vinfo)
{
    InputInfoPtr info = (InputInfoPtr) vinfo;
    size_t i;
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;

    FILE* fp = fopen("/var/log/cmt_input_events.dat", "wb");
    if (!fp) {
        ERR(info, "fopen() failed for debug log");
        return;
    }
    for (i = 0; i < DEBUG_BUF_SIZE; i++) {
        size_t rc;
        struct input_event *ev =
            &evstate->debug_buf[(evstate->debug_buf_tail + i) % DEBUG_BUF_SIZE];
        if (ev->time.tv_sec == 0 && ev->time.tv_usec == 0)
            continue;
        rc = fprintf(fp, "E: %ld.%06ld %04x %04x %d\n",
                            ev->time.tv_sec,
                            ev->time.tv_usec,
                            ev->type,
                            ev->code,
                            ev->value);
        if (rc == 0) {
            ERR(info, "fprintf() failed for debug log. Log is short");
            break;
        }
    }
    fclose(fp);
}

static void
Event_Syn(InputInfoPtr info, struct input_event* ev)
{
    switch (ev->code) {
    case SYN_REPORT:
        Event_Syn_Report(info, ev);
        break;
    case SYN_MT_REPORT:
        Event_Syn_MT_Report(info, ev);
        break;
    }
}

static void
Event_Syn_Report(InputInfoPtr info, struct input_event* ev)
{
    CmtDevicePtr cmt = info->private;
    GesturePtr gesture = &cmt->gesture;
    EventStatePtr evstate = &cmt->evstate;

    Gesture_Process_Slots(gesture, evstate, &ev->time);
    MT_Print_Slots(info);
}

static void
Event_Syn_MT_Report(InputInfoPtr info, struct input_event* ev)
{
    /* TODO(djkurtz): Handle MT-A */
}

static void
Event_Key(InputInfoPtr info, struct input_event* ev)
{
    CmtDevicePtr cmt = info->private;
    AssignBit(cmt->key_state_bitmask, ev->code, ev->value);
}

static void
SemiMtSetAbsPressure(InputInfoPtr info, struct input_event* ev)
{
    /*
     * Update all active slots with the same ABS_PRESSURE value if it is a
     * semi-mt device.
     */
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;

    for (int i = 0; i < evstate->slot_count; i++) {
        MtSlotPtr slot = &evstate->slots[i];
        slot->pressure = ev->value;
    }
}

static void
Event_Abs(InputInfoPtr info, struct input_event* ev)
{
    if (ev->code == ABS_MT_SLOT)
        MT_Slot_Set(info, ev->value);
    else if (IS_ABS_MT(ev->code))
        Event_Abs_MT(info, ev);
    else if ((ev->code == ABS_PRESSURE) && Event_Get_Semi_MT(info))
        SemiMtSetAbsPressure(info, ev);
}

static void
Event_Abs_MT(InputInfoPtr info, struct input_event* ev)
{
    CmtDevicePtr cmt = info->private;
    EventStatePtr evstate = &cmt->evstate;
    struct input_absinfo* axis = evstate->mt_axes[MT_CODE(ev->code)];
    MtSlotPtr slot = evstate->slot_current;

    if (axis == NULL) {
        ERR(info, "ABS_MT[%02x] was not reported by this device\n", ev->code);
        return;
    }

    /* Warn about out of range data, but don't ignore */
    if ((ev->code != ABS_MT_TRACKING_ID)
                    && ((ev->value < axis->minimum)
                        || (ev->value > axis->maximum))) {
        xf86IDrvMsgVerb(info, X_WARNING, DBG_VERB,
            "ABS_MT[%02x] = %d : value out of range [%d .. %d]\n",
            ev->code, ev->value, axis->minimum, axis->maximum);
    }

    if (slot == NULL) {
        ERR(info, "MT slot not set. Ignoring ABS_MT event\n");
        return;
    }

    MT_Slot_Value_Set(slot, ev->code, ev->value);
}
