/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
 * Copyright (C) 2012-2021 BitCraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * eventtrigger.c - Event triggers to mark important system events with payloads
 */
#include <string.h>

#include "eventtrigger.h"

#include "debug.h"

static eventtriggerCallback callbacks[eventtriggerHandler_Count] = {0};

#ifdef CONFIG_PLATFORM_MACOS_SITL
#include <mach-o/getsect.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
// For macOS SITL, we'll get section boundaries at runtime
extern const struct mach_header_64 _mh_execute_header;
static eventtrigger *_eventtrigger_start = NULL;
static eventtrigger *_eventtrigger_stop = NULL;
#else
/* Symbols set by the linker script */
extern eventtrigger _eventtrigger_start;
extern eventtrigger _eventtrigger_stop;
#endif

void initalizeSectionBoundaries() {
    if (_eventtrigger_start == NULL) {
        unsigned long size;
        _eventtrigger_start = (eventtrigger*)getsectiondata(&_mh_execute_header, "__DATA", ".eventtrigger", &size);
        if (_eventtrigger_start) {
            _eventtrigger_stop = (eventtrigger*)((char*)_eventtrigger_start + size);
        }
    }
}

uint16_t eventtriggerGetId(const eventtrigger *event)
{
#ifdef CONFIG_PLATFORM_MACOS_SITL
    // Initialize section boundaries if not already done
    initalizeSectionBoundaries();
    if (_eventtrigger_start == NULL) return 0;
    return event - _eventtrigger_start;
#else
    // const eventtrigger* start = &_eventtrigger_start;
    return event - &_eventtrigger_start;
    return 0;
#endif
}

const eventtrigger *eventtriggerGetById(uint16_t id)
{
#ifdef CONFIG_PLATFORM_MACOS_SITL
    // Initialize section boundaries if not already done
    initalizeSectionBoundaries();
    const eventtrigger *result = _eventtrigger_start;
    int numEventtriggers = _eventtrigger_stop - _eventtrigger_start;
#else
    const eventtrigger *result = &_eventtrigger_start;
    int numEventtriggers = &_eventtrigger_stop - &_eventtrigger_start;
#endif
    if (id < numEventtriggers) {
        return &result[id];
    }
    return 0;
}

const eventtrigger* eventtriggerGetByName(const char *name)
{
#ifdef CONFIG_PLATFORM_MACOS_SITL
    // Initialize section boundaries if not already done
    initalizeSectionBoundaries();
    const eventtrigger* result = _eventtrigger_start;
    int numEventtriggers = _eventtrigger_stop - _eventtrigger_start;
#else
    const eventtrigger* result = &_eventtrigger_start;
    int numEventtriggers = &_eventtrigger_stop - &_eventtrigger_start;
#endif
    for (int i = 0; i < numEventtriggers; ++i) {
        if (strcmp(result[i].name, name) == 0) {
            return &result[i];
        }
    }
    return 0;
}

void eventTrigger(const eventtrigger *event)
{
    for (int i = 0; i < eventtriggerHandler_Count; ++i) {
        if (callbacks[i]) {
            callbacks[i](event);
        }
    }
}

void eventtriggerRegisterCallback(enum eventtriggerHandler_e handler, eventtriggerCallback cb)
{
    callbacks[handler] = cb;
}
