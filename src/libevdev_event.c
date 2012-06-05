/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "libevdev_event.h"

#include <errno.h>
#include <linux/input.h>
#include <stdbool.h>
#include <time.h>

#include "libevdev.h"

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
static inline bool TestBit(int, unsigned long*);

static void Absinfo_Print(EvDevicePtr device, struct input_absinfo*);
static void Event_Print(EvDevicePtr, struct input_event*);

static void Event_Syn(EvDevicePtr, struct input_event*);
static void Event_Syn_Report(EvDevicePtr, struct input_event*);
static void Event_Syn_MT_Report(EvDevicePtr, struct input_event*);

static void Event_Key(EvDevicePtr, struct input_event*);

static void Event_Abs(EvDevicePtr, struct input_event*);
static void Event_Abs_MT(EvDevicePtr, struct input_event*);
static void SemiMtSetAbsPressure(EvDevicePtr, struct input_event*);
static void Event_Sync_Keys(EvDevicePtr);
static void Event_Get_Time(struct timeval*, bool);

/**
 * Helper functions
 */
static inline bool
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
Event_Get_Left(EvDevicePtr device)
{
    struct input_absinfo* absinfo = &device->info.absinfo[ABS_X];
    return absinfo->minimum;
}

int
Event_Get_Right(EvDevicePtr device)
{
    struct input_absinfo* absinfo = &device->info.absinfo[ABS_X];
    return absinfo->maximum;
}

int
Event_Get_Top(EvDevicePtr device)
{
    struct input_absinfo* absinfo = &device->info.absinfo[ABS_Y];
    return absinfo->minimum;
}

int
Event_Get_Bottom(EvDevicePtr device)
{
    struct input_absinfo* absinfo = &device->info.absinfo[ABS_Y];
    return absinfo->maximum;
}

int
Event_Get_Res_Y(EvDevicePtr device)
{
    struct input_absinfo* absinfo = &device->info.absinfo[ABS_Y];
    return absinfo->resolution;
}

int
Event_Get_Res_X(EvDevicePtr device)
{
    struct input_absinfo* absinfo = &device->info.absinfo[ABS_X];
    return absinfo->resolution;
}

int
Event_Get_Button_Pad(EvDevicePtr device)
{
    return TestBit(INPUT_PROP_BUTTONPAD, device->info.prop_bitmask);
}

int
Event_Get_Semi_MT(EvDevicePtr device)
{
    return TestBit(INPUT_PROP_SEMI_MT, device->info.prop_bitmask);
}

int
Event_Get_T5R2(EvDevicePtr device)
{
    EventStatePtr evstate = device->evstate;
    if (Event_Get_Semi_MT(device))
        return 0;
    return (Event_Get_Touch_Count_Max(device) > evstate->slot_count);
}

int
Event_Get_Touch_Count_Max(EvDevicePtr device)
{

    if (TestBit(BTN_TOOL_QUINTTAP, device->info.key_bitmask))
        return 5;
    if (TestBit(BTN_TOOL_QUADTAP, device->info.key_bitmask))
        return 4;
    if (TestBit(BTN_TOOL_TRIPLETAP, device->info.key_bitmask))
        return 3;
    if (TestBit(BTN_TOOL_DOUBLETAP, device->info.key_bitmask))
        return 2;
    return 1;
}

static void
Event_Sync_Keys(EvDevicePtr device)
{
    int len = sizeof(device->key_state_bitmask);

    memset(device->key_state_bitmask, 0, len);
    if (ioctl(device->fd, EVIOCGKEY(len), device->key_state_bitmask) < 0)
        LOG_ERROR(device, "ioctl EVIOCGKEY failed: %s\n", strerror(errno));
}

int
Event_Get_Touch_Count(EvDevicePtr device)
{

    if (TestBit(BTN_TOOL_QUINTTAP, device->key_state_bitmask))
        return 5;
    if (TestBit(BTN_TOOL_QUADTAP, device->key_state_bitmask))
        return 4;
    if (TestBit(BTN_TOOL_TRIPLETAP, device->key_state_bitmask))
        return 3;
    if (TestBit(BTN_TOOL_DOUBLETAP, device->key_state_bitmask))
        return 2;
    if (TestBit(BTN_TOOL_FINGER, device->key_state_bitmask))
        return 1;
    return 0;
}

int
Event_Get_Slot_Count(EvDevicePtr device)
{
    EventStatePtr evstate = device->evstate;
    return evstate->slot_count;
}

int
Event_Get_Button_Left(EvDevicePtr device)
{
    return TestBit(BTN_LEFT, device->key_state_bitmask);
}

int
Event_Get_Button_Middle(EvDevicePtr device)
{
    return TestBit(BTN_MIDDLE, device->key_state_bitmask);
}

int
Event_Get_Button_Right(EvDevicePtr device)
{
    return TestBit(BTN_RIGHT, device->key_state_bitmask);
}

static int
Event_Enable_Monotonic(EvDevicePtr device)
{
    unsigned int clk = CLOCK_MONOTONIC;
    return (ioctl(device->fd, EVIOCSCLOCKID, &clk) == 0) ? Success : !Success;
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
Event_Init(EvDevicePtr device)
{
    EventStatePtr evstate = device->evstate;
    int i;
    int len;
    EvDeviceInfoPtr info = &device->info;

    if (ioctl(device->fd, EVIOCGID, &info->id) < 0) {
         LOG_ERROR(device, "ioctl EVIOCGID failed: %s\n", strerror(errno));
         return !Success;
    }
    LOG_DEBUG(device, "vendor: %02X, product: %02X\n", info->id.vendor,
              info->id.product);

    if (ioctl(device->fd, EVIOCGNAME(sizeof(info->name) - 1),
              info->name) < 0) {
        LOG_ERROR(device, "ioctl EVIOCGNAME failed: %s\n", strerror(errno));
        return !Success;
    }
    LOG_DEBUG(device, "name: %s\n", info->name);

    len = ioctl(device->fd, EVIOCGPROP(sizeof(info->prop_bitmask)),
                info->prop_bitmask);
    if (len < 0) {
        LOG_ERROR(device, "ioctl EVIOCGPROP failed: %s\n", strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, info->prop_bitmask))
            LOG_DEBUG(device, "Has Property: %d (%s)\n", i,
                      Event_Property_To_String(i));
    }

    len = ioctl(device->fd, EVIOCGBIT(0, sizeof(info->bitmask)),
                info->bitmask);
    if (len < 0) {
        LOG_ERROR(device, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, info->bitmask))
            LOG_DEBUG(device, "Has Event Type %d = %s\n", i,
                      Event_Type_To_String(i));
    }

    len = ioctl(device->fd, EVIOCGBIT(EV_KEY, sizeof(info->key_bitmask)),
                info->key_bitmask);
    if (len < 0) {
        LOG_ERROR(device, "ioctl EVIOCGBIT(EV_KEY) failed: %s\n",
                  strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, info->key_bitmask))
            LOG_DEBUG(device, "Has KEY[%d] = %s\n", i,
                      Event_To_String(EV_KEY, i));
    }

    len = ioctl(device->fd, EVIOCGBIT(EV_LED, sizeof(info->led_bitmask)),
                info->led_bitmask);
    if (len < 0) {
        LOG_ERROR(device, "ioctl EVIOCGBIT(EV_LED) failed: %s\n",
                  strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, info->led_bitmask))
            LOG_DEBUG(device, "Has LED[%d] = %s\n", i,
                      Event_To_String(EV_LED, i));
    }

    len = ioctl(device->fd, EVIOCGBIT(EV_REL, sizeof(info->rel_bitmask)),
                info->rel_bitmask);
    if (len < 0) {
        LOG_ERROR(device, "ioctl EVIOCGBIT(EV_REL) failed: %s\n",
                  strerror(errno));
        return !Success;
    }
    for (i = 0; i < len*8; i++) {
        if (TestBit(i, info->rel_bitmask))
            LOG_DEBUG(device, "Has REL[%d] = %s\n", i,
                      Event_To_String(EV_REL, i));
    }

    /*
     * TODO(djkurtz): Solve the race condition between MT slot initialization
     *    from absinfo, and incoming/lost input events.
     *    Specifically, if kernel driver sends MT_SLOT event between absinfo
     *    probe and when we start listening for input events.
     */

    len = ioctl(device->fd, EVIOCGBIT(EV_ABS, sizeof(info->abs_bitmask)),
                info->abs_bitmask);
    if (len < 0) {
        LOG_ERROR(device, "ioctl EVIOCGBIT(EV_ABS) failed: %s\n",
                  strerror(errno));
        return !Success;
    }

    for (i = ABS_X; i <= ABS_MAX; i++) {
        if (TestBit(i, info->abs_bitmask)) {
            struct input_absinfo* absinfo = &info->absinfo[i];
            LOG_DEBUG(device, "Has ABS[%d] = %s\n", i,
                      Event_To_String(EV_ABS, i));
            len = ioctl(device->fd, EVIOCGABS(i), absinfo);
            if (len < 0) {
                LOG_ERROR(device, "ioctl EVIOCGABS(%d) failed: %s\n", i,
                    strerror(errno));
                /*
                 * Clean up in case where error happens after MTB_Init() has
                 * already allocated slots.
                 */
                goto error_MT_Free;
            }

            Absinfo_Print(device, absinfo);

            if (i == ABS_MT_SLOT) {
                int rc;
                rc = MTB_Init(device, absinfo->minimum, absinfo->maximum,
                              absinfo->value);
                if (rc != Success)
                    return rc;
            } else if (IS_ABS_MT(i)) {
                evstate->mt_axes[MT_CODE(i)] = absinfo;
            }
        }
    }

    /* Synchronize all MT slots with kernel evdev driver */
    Event_Sync_State(device);
    return Success;

error_MT_Free:
    MT_Free(device);
    return !Success;
}

void
Event_Free(EvDevicePtr device)
{
    MT_Free(device);
}

void
Event_Open(EvDevicePtr device)
{
    /* Select monotonic input event timestamps, if supported by kernel */
    device->info.is_monotonic = (Event_Enable_Monotonic(device) == Success);
    /* Reset the sync time variables */
    Event_Get_Time(&device->before_sync_time, device->info.is_monotonic);
    Event_Get_Time(&device->after_sync_time, device->info.is_monotonic);
    LOG_DEBUG(device, "Using %s input event time stamps\n",
              device->info.is_monotonic ? "monotonic" : "realtime");
}

/**
 * Debug Print Helper Functions
 */
static void
Absinfo_Print(EvDevicePtr device, struct input_absinfo* absinfo)
{
    LOG_DEBUG(device, "    min = %d\n", absinfo->minimum);
    LOG_DEBUG(device, "    max = %d\n", absinfo->maximum);
    if (absinfo->fuzz)
        LOG_DEBUG(device, "    fuzz = %d\n", absinfo->fuzz);
    if (absinfo->resolution)
        LOG_DEBUG(device, "    res = %d\n", absinfo->resolution);
}

static void
Event_Get_Time(struct timeval *t, bool use_monotonic) {
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
Event_Sync_State(EvDevicePtr device)
{
    int i;
    struct input_absinfo* absinfo;

    Event_Get_Time(&device->before_sync_time, device->info.is_monotonic);

    Event_Sync_Keys(device);

    /* Get current pressure information for semi_mt device */
    if (Event_Get_Semi_MT(device)) {
        absinfo = &device->info.absinfo[ABS_PRESSURE];
        if (ioctl(device->fd, EVIOCGABS(ABS_PRESSURE), absinfo) < 0) {
            LOG_ERROR(device, "ioctl EVIOCGABS(ABS_PRESSURE) failed: %s\n",
                strerror(errno));
        } else {
            struct input_event ev;
            ev.code = ABS_PRESSURE;
            ev.value = absinfo->value;
            SemiMtSetAbsPressure(device, &ev);
        }
    }

    /* TODO(cywang): Sync all ABS_ states for completeness */

    /* Get current MT information for each slot */
    for (i = _ABS_MT_FIRST; i <= _ABS_MT_LAST; i++) {
        MTSlotInfo req;

        if (!TestBit(i, device->info.abs_bitmask))
            continue;
        /*
         * TODO(cywang): Scale the size of slots in MTSlotInfo based on the
         *    evstate->slot_count.
         */

        req.code = i;
        if (ioctl(device->fd, EVIOCGMTSLOTS((sizeof(req))), &req) < 0) {
            LOG_ERROR(device, "ioctl EVIOCGMTSLOTS(req.code=%d) failed: %s\n",
                      i, strerror(errno));
            continue;
        }
        MT_Slot_Sync(device, &req);
    }

    /* Get current slot id */
    absinfo = &device->info.absinfo[ABS_MT_SLOT];
    if (ioctl(device->fd, EVIOCGABS(ABS_MT_SLOT), absinfo) < 0)
        LOG_ERROR(device, "ioctl EVIOCGABS(ABS_MT_SLOT) failed: %s\n",
                  strerror(errno));
    else
        MT_Slot_Set(device, absinfo->value);

    Event_Get_Time(&device->after_sync_time, device->info.is_monotonic);

    LOG_DEBUG(device, "Event_Sync_State: before %ld.%ld after %ld.%ld\n",
              device->before_sync_time.tv_sec, device->before_sync_time.tv_usec,
              device->after_sync_time.tv_sec, device->after_sync_time.tv_usec);
}

static void
Event_Print(EvDevicePtr device, struct input_event* ev)
{
    switch (ev->type) {
    case EV_SYN:
        switch (ev->code) {
        case SYN_REPORT:
            LOG_ERROR(device, "@ %ld.%06ld  ---------- SYN_REPORT -------\n",
                ev->time.tv_sec, ev->time.tv_usec);
            return;
        case SYN_MT_REPORT:
            LOG_ERROR(device, "@ %ld.%06ld  ........ SYN_MT_REPORT ......\n",
                ev->time.tv_sec, ev->time.tv_usec);
            return;
        case SYN_DROPPED:
            LOG_ERROR(device, "@ %ld.%06ld  ++++++++ SYN_DROPPED ++++++++\n",
                ev->time.tv_sec, ev->time.tv_usec);
            return;
        default:
            LOG_ERROR(device, "@ %ld.%06ld  ?????? SYN_UNKNOWN (%d) ?????\n",
                ev->time.tv_sec, ev->time.tv_usec, ev->code);
            return;
        }
        break;
    case EV_ABS:
        if (ev->code == ABS_MT_SLOT) {
            LOG_ERROR(device, "@ %ld.%06ld  .......... MT SLOT %d ........\n",
                ev->time.tv_sec, ev->time.tv_usec, ev->value);
            return;
        }
        break;
    default:
        break;
    }

    LOG_ERROR(device, "@ %ld.%06ld %s[%d] (%s) = %d\n",
        ev->time.tv_sec, ev->time.tv_usec, Event_Type_To_String(ev->type),
        ev->code, Event_To_String(ev->type, ev->code), ev->value);
}

/**
 * Process Input Events
 */
bool
Event_Process(EvDevicePtr device, struct input_event* ev)
{
    EventStatePtr evstate = device->evstate;

    Event_Print(device, ev);
    if (evstate->debug_buf) {
        evstate->debug_buf[evstate->debug_buf_tail] = *ev;
        evstate->debug_buf_tail =
            (evstate->debug_buf_tail + 1) % DEBUG_BUF_SIZE;
    }

    switch (ev->type) {
    case EV_SYN:
        if (ev->code == SYN_DROPPED)
            return true;
        Event_Syn(device, ev);
        break;

    case EV_KEY:
        Event_Key(device, ev);
        break;

    case EV_ABS:
        Event_Abs(device, ev);
        break;

    default:
        break;
    }
    return false;
}

/**
 * Dump the log of input events to disk
 */
void
Event_Dump_Debug_Log(void* vinfo)
{
    EvDevicePtr device = (EvDevicePtr) vinfo;
    size_t i;
    EventStatePtr evstate = device->evstate;

    FILE* fp = fopen("/var/log/cmt_input_events.dat", "wb");
    if (!fp) {
        LOG_ERROR(device, "fopen() failed for debug log");
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
            LOG_ERROR(device, "fprintf() failed for debug log. Log is short");
            break;
        }
    }
    fclose(fp);
}

static void
Event_Syn(EvDevicePtr device, struct input_event* ev)
{
    switch (ev->code) {
    case SYN_REPORT:
        Event_Syn_Report(device, ev);
        break;
    case SYN_MT_REPORT:
        Event_Syn_MT_Report(device, ev);
        break;
    }
}

static void
Event_Syn_Report(EvDevicePtr device, struct input_event* ev)
{
    EventStatePtr evstate = device->evstate;
    device->syn_report(device->syn_report_udata, evstate, &ev->time);

    MT_Print_Slots(device);
}

static void
Event_Syn_MT_Report(EvDevicePtr device, struct input_event* ev)
{
    /* TODO(djkurtz): Handle MT-A */
}

static void
Event_Key(EvDevicePtr device, struct input_event* ev)
{
    AssignBit(device->key_state_bitmask, ev->code, ev->value);
}

static void
SemiMtSetAbsPressure(EvDevicePtr device, struct input_event* ev)
{
    /*
     * Update all active slots with the same ABS_PRESSURE value if it is a
     * semi-mt device.
     */
    EventStatePtr evstate = device->evstate;

    for (int i = 0; i < evstate->slot_count; i++) {
        MtSlotPtr slot = &evstate->slots[i];
        slot->pressure = ev->value;
    }
}

static void
Event_Abs(EvDevicePtr device, struct input_event* ev)
{
    if (ev->code == ABS_MT_SLOT)
        MT_Slot_Set(device, ev->value);
    else if (IS_ABS_MT(ev->code))
        Event_Abs_MT(device, ev);
    else if ((ev->code == ABS_PRESSURE) && Event_Get_Semi_MT(device))
        SemiMtSetAbsPressure(device, ev);
}

static void
Event_Abs_MT(EvDevicePtr device, struct input_event* ev)
{
    EventStatePtr evstate = device->evstate;
    struct input_absinfo* axis = evstate->mt_axes[MT_CODE(ev->code)];
    MtSlotPtr slot = evstate->slot_current;

    if (axis == NULL) {
        LOG_ERROR(device, "ABS_MT[%02x] was not reported by this device\n",
                  ev->code);
        return;
    }

    /* Warn about out of range data, but don't ignore */
    if ((ev->code != ABS_MT_TRACKING_ID)
                    && ((ev->value < axis->minimum)
                        || (ev->value > axis->maximum))) {
      LOG_WARNING(device, "ABS_MT[%02x] = %d : value out of range [%d .. %d]\n",
                  ev->code, ev->value, axis->minimum, axis->maximum);
    }

    if (slot == NULL) {
        LOG_ERROR(device, "MT slot not set. Ignoring ABS_MT event\n");
        return;
    }

    MT_Slot_Value_Set(slot, ev->code, ev->value);
}
