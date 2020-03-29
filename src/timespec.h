/*
 * Copyright 2020 Pim van Pelt <pim@ipng.nl>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once
#include <unistd.h>

#include "mgos.h"

/* A simple time specifier for windows of time throughout a 24 hour day.
 *
 * Callers construct a struct mgos_timespec and then add time range
 * specifications to the object. Ranges consist of ':', '-' and [0-9]
 * characters and describe a [start-stop> time range where 'start' is
 * included but 'stop' is not. Start and stop can have hours, minutes
 * and seconds separated by ':'. Example specs:
 *    "8:00-10:00"   (from 8am to 10am -- 7200 seconds)
 *    "23-01"        (from 11pm to 1am -- 7200 seconds)
 *    "01:02:03-02"  (from 01:02:03 to 2am -- 3477 seconds)
 *
 * Example to demonstrate the usage:
 *
 * struct tm target;
 * struct mgos_timespec *ts = timespec_create();
 * timespec_add_spec(ts, "8:00-10:00");       // 7200 seconds, [08:00:00 -
 * 10:00:00> timespec_add_spec(ts, "23-01");             // 7200 seconds,
 * [23:00:00 - 01:00:00> timespec_add_spec(ts, "01:02:03-02:03:04"); // 3661
 * seconds, [01:02:03 - 02:03:04>
 *
 * target.tm_hour=8; target.tm_min=0; target.tm_sec=0;  // 08:00:00
 * timespec_match(ts, &target); // TRUE; start of first spec
 *
 * target.tm_hour=10; target.tm_min=0; target.tm_sec=0; // 10:00:00
 * timespec_match(ts, &target); // FALSE; end of first spec; end times are
 * excluded!
 *
 * target.tm_hour=0; target.tm_min=0; target.tm_sec=0; // 00:00:00
 * timespec_match(ts, &target); // TRUE; in the middle of the second spec
 *
 * target.tm_hour=1; target.tm_min=2; target.tm_sec=3; // 01:02:03
 * timespec_match(ts, &target); // TRUE; the first second of the third spec
 *
 * target.tm_hour=2; target.tm_min=3; target.tm_sec=3; // 02:03:03
 * timespec_match(ts, &target); // TRUE; the last second of the third spec
 *
 * timespec_destroy(&ts);
 */

struct mgos_timespec;

struct mgos_timespec *timespec_create();
bool timespec_destroy(struct mgos_timespec **ts);
bool timespec_empty(struct mgos_timespec *ts);
bool timespec_add_spec(struct mgos_timespec *ts, const char *spec);
bool timespec_clear_spec(struct mgos_timespec *ts);
bool timespec_get_spec(struct mgos_timespec *ts, char *ret, int retlen);
bool timespec_match(const struct mgos_timespec *ts, const struct tm *tm);
bool timespec_match_now(const struct mgos_timespec *ts);

// File IO -- read or write the current timespec to a file
bool timespec_write_file(struct mgos_timespec *ts, const char *fn);
bool timespec_read_file(struct mgos_timespec *ts, const char *fn);
