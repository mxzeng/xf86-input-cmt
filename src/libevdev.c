/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "libevdev.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "libevdev_event.h"
#include "libevdev_log.h"
#include "libevdev_util.h"

/* Number of events to attempt to read from kernel on each SIGIO */
#define NUM_EVENTS          16

#ifndef EVIOCGMTSLOTS
#define EVIOCGMTSLOTS(len)  _IOC(_IOC_READ, 'E', 0x0a, len)
#endif

/* Set clockid to be used for timestamps */
#ifndef EVIOCSCLOCKID
#define EVIOCSCLOCKID  _IOW('E', 0xa0, int)
#endif

static void Absinfo_Print(EvdevPtr device, struct input_absinfo*);
static const char* Event_Property_To_String(int type);

int EvdevOpen(EvdevPtr evdev, const char* device) {
  evdev->fd = open(device, O_RDWR | O_NONBLOCK, 0);
  return evdev->fd;
}

int EvdevClose(EvdevPtr evdev) {
  close(evdev->fd);
  evdev->fd = -1;
  return evdev->fd;
}

int EvdevRead(EvdevPtr evdev) {
  struct input_event ev[NUM_EVENTS];
  int i;
  int len;
  bool sync_evdev_state = false;

  do {
    len = read(evdev->fd, &ev, sizeof(ev));
    if (len <= 0)
      return errno;

    /* kernel always delivers complete events, so len must be sizeof *ev */
    if (len % sizeof(*ev))
      return errno;

    /* Process events ... */
    for (i = 0; i < len / sizeof(ev[0]); i++) {
      if (sync_evdev_state)
        break;
      if (timercmp(&ev[i].time, &evdev->before_sync_time, <)) {
        /* Ignore events before last sync time */
        continue;
      } else if (timercmp(&ev[i].time, &evdev->after_sync_time, >)) {
        /* Event_Process returns TRUE if SYN_DROPPED detected */
        sync_evdev_state = Event_Process(evdev, &ev[i]);
      } else {
        /* If the event occurred during sync, then sync again */
        sync_evdev_state = true;
      }
    }

  } while (len == sizeof(ev));
  /* Keep reading if kernel supplied NUM_EVENTS events. */

  if (sync_evdev_state)
    Event_Sync_State(evdev);

  return Success;
}

int EvdevProbe(EvdevPtr device) {
  int len, i;
  int fd;
  EvdevInfoPtr info;

  fd = device->fd;
  info = &device->info;
  if (ioctl(fd, EVIOCGID, &info->id) < 0) {
       LOG_ERROR(device, "ioctl EVIOCGID failed: %s\n", strerror(errno));
       return !Success;
  }

  if (ioctl(fd, EVIOCGNAME(sizeof(info->name) - 1),
            info->name) < 0) {
      LOG_ERROR(device, "ioctl EVIOCGNAME failed: %s\n", strerror(errno));
      return !Success;
  }

  len = ioctl(fd, EVIOCGPROP(sizeof(info->prop_bitmask)),
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

  len = ioctl(fd, EVIOCGBIT(0, sizeof(info->bitmask)),
              info->bitmask);
  if (len < 0) {
      LOG_ERROR(device, "ioctl EVIOCGBIT failed: %s\n",
                strerror(errno));
      return !Success;
  }
  for (i = 0; i < len*8; i++) {
      if (TestBit(i, info->bitmask))
          LOG_DEBUG(device, "Has Event Type %d = %s\n", i,
                    Event_Type_To_String(i));
  }

  len = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(info->key_bitmask)),
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

  len = ioctl(fd, EVIOCGBIT(EV_LED, sizeof(info->led_bitmask)),
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

  len = ioctl(fd, EVIOCGBIT(EV_REL, sizeof(info->rel_bitmask)),
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

  len = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(info->abs_bitmask)),
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
          len = ioctl(fd, EVIOCGABS(i), absinfo);
          if (len < 0) {
              LOG_ERROR(device, "ioctl EVIOCGABS(%d) failed: %s\n", i,
                  strerror(errno));
              return !Success;
          }

          Absinfo_Print(device, absinfo);
      }
  }
  return Success;
}

int EvdevProbeAbsinfo(EvdevPtr device, size_t key) {
  struct input_absinfo* absinfo;

  absinfo = &device->info.absinfo[key];
  if (ioctl(device->fd, EVIOCGABS(key), absinfo) < 0) {
      LOG_ERROR(device, "ioctl EVIOCGABS(%d) failed: %s\n", key,
                strerror(errno));
      return !Success;
  } else {
      return Success;
  }
}

int EvdevProbeMTSlot(EvdevPtr device, MTSlotInfoPtr req) {
  if (ioctl(device->fd, EVIOCGMTSLOTS((sizeof(req))), &req) < 0) {
      LOG_ERROR(device, "ioctl EVIOCGMTSLOTS(req.code=%d) failed: %s\n",
          req->code, strerror(errno));
      return !Success;
  } else {
      return Success;
  }
}

int EvdevProbeKeyState(EvdevPtr device) {
  int len = sizeof(device->key_state_bitmask);

  memset(device->key_state_bitmask, 0, len);
  if (ioctl(device->fd, EVIOCGKEY(len), device->key_state_bitmask) < 0) {
      LOG_ERROR(device, "ioctl EVIOCGKEY failed: %s\n", strerror(errno));
      return !Success;
  } else {
      return Success;
  }
}

int EvdevEnableMonotonic(EvdevPtr device) {
  unsigned int clk = CLOCK_MONOTONIC;
  return (ioctl(device->fd, EVIOCSCLOCKID, &clk) == 0) ? Success : !Success;
}


static const char*
Event_Property_To_String(int type) {
    switch (type) {
    case INPUT_PROP_POINTER: return "POINTER";      /* needs a pointer */
    case INPUT_PROP_DIRECT: return "DIRECT";        /* direct input devices */
    case INPUT_PROP_BUTTONPAD: return "BUTTONPAD";  /* has button under pad */
    case INPUT_PROP_SEMI_MT: return "SEMI_MT";      /* touch rectangle only */
    default: return "?";
    }
}

static void
Absinfo_Print(EvdevPtr device, struct input_absinfo* absinfo)
{
    LOG_DEBUG(device, "    min = %d\n", absinfo->minimum);
    LOG_DEBUG(device, "    max = %d\n", absinfo->maximum);
    if (absinfo->fuzz)
        LOG_DEBUG(device, "    fuzz = %d\n", absinfo->fuzz);
    if (absinfo->resolution)
        LOG_DEBUG(device, "    res = %d\n", absinfo->resolution);
}

