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

#include "libevdev_log.h"

/* Number of events to attempt to read from kernel on each SIGIO */
#define NUM_EVENTS          16

int EvdevOpen(EvDevicePtr evdev, const char* device) {
  evdev->fd = open(device, O_RDWR | O_NONBLOCK, 0);
  return evdev->fd;
}

int EvdevClose(EvDevicePtr evdev) {
  close(evdev->fd);
  evdev->fd = -1;
  return evdev->fd;
}

int EvdevRead(EvDevicePtr evdev) {
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

