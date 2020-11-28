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
#include "timespec.h"

/* Private prototypes and declarations */
struct mgos_timespec_spec {
  uint8_t start_h, start_m, start_s;
  uint8_t stop_h, stop_m, stop_s;
  SLIST_ENTRY(mgos_timespec_spec) entries;
};

struct mgos_timespec {
  SLIST_HEAD(mgos_timespec_specs, mgos_timespec_spec) specs;
};

static bool timespec_spec_parse(const char *spec, struct mgos_timespec_spec *out) {
  uint8_t start_h = 0, start_m = 0, start_s = 0;
  uint8_t stop_h = 0, stop_m = 0, stop_s = 0;
  char *p;
  int i;

  if (!spec || strlen(spec) == 0) {
    LOG(LL_ERROR, ("spec cannot be NULL or empty"));
    return false;
  }
  for (i = 1; i < (int) strlen(spec); i++) {
    if (spec[i] != ':' && spec[i] != '-' && !(spec[i] >= '0' && spec[i] <= '9')) {
      LOG(LL_ERROR, ("spec='%s': illegal chars, only want [:\\-0-9]", spec));
      return false;
    }
  }

  p = (char *) spec;
  if (!isdigit((int) *p)) {
    LOG(LL_ERROR, ("spec='%s': start time must begin with a digit", spec));
    return false;
  }
  // start_h
  i = strtol(p, &p, 10);
  if (i < 0 || i > 23) {
    LOG(LL_ERROR, ("spec='%s': start hour must be [0..23]", spec));
    return false;
  }
  start_h = i;
  if (*p == ':') {
    // start_m
    i = strtol(p + 1, &p, 10);
    if (i < 0 || i > 59) {
      LOG(LL_ERROR, ("spec='%s': start minute must be [0..59]", spec));
      return false;
    }
    start_m = i;
  }
  if (*p == ':') {
    // start_s
    i = strtol(p + 1, &p, 10);
    if (i < 0 || i > 59) {
      LOG(LL_ERROR, ("spec='%s': start second must be [0..59]", spec));
      return false;
    }
    start_s = i;
  }
  if (*p != '-') {
    LOG(LL_ERROR, ("spec='%s': No separator '-' found after start", spec));
    return false;
  }

  // stop_h
  if (!isdigit((int) *(p + 1))) {
    LOG(LL_ERROR, ("spec='%s': stop time must begin with a digit", spec));
    return false;
  }
  i = strtol(p + 1, &p, 10);
  if (i < 0 || i > 23) {
    LOG(LL_ERROR, ("spec='%s': stop hour must be [0..23]", spec));
    return false;
  }
  stop_h = i;
  if (*p == ':') {
    // start_m
    i = strtol(p + 1, &p, 10);
    if (i < 0 || i > 59) {
      LOG(LL_ERROR, ("spec='%s': stop minute must be [0..59]", spec));
      return false;
    }
    stop_m = i;
  }
  if (*p == ':') {
    // stop_s
    i = strtol(p + 1, &p, 10);
    if (i < 0 || i > 59) {
      LOG(LL_ERROR, ("spec='%s': stop second must be [0..59]", spec));
      return false;
    }
    stop_s = i;
  }
  if (*p) {
    LOG(LL_ERROR, ("spec='%s': dangling characters after stop time", spec));
    return false;
  }

  //  LOG(LL_DEBUG, ("start=%d stop=%d", start, stop));
  if (out) {
    out->start_h = start_h;
    out->start_m = start_m;
    out->start_s = start_s;
    out->stop_h = stop_h;
    out->stop_m = stop_m;
    out->stop_s = stop_s;
  }
  return true;
}

struct mgos_timespec *timespec_create() {
  struct mgos_timespec *ts = calloc(1, sizeof(struct mgos_timespec));

  if (!ts) {
    return NULL;
  }

  return ts;
}

bool timespec_destroy(struct mgos_timespec **ts) {
  if (!ts) {
    return false;
  }
  timespec_clear_spec(*ts);
  free(*ts);
  *ts = NULL;
  return true;
}

bool timespec_add_spec(struct mgos_timespec *ts, const char *spec) {
  struct mgos_timespec_spec *t_spec = calloc(1, sizeof(struct mgos_timespec_spec));

  if (!ts) {
    return false;
  }

  if (!t_spec) {
    LOG(LL_ERROR, ("Could not alloc memory for struct mgos_timespec_spec"));
    return false;
  }
  if (!timespec_spec_parse(spec, t_spec)) {
    LOG(LL_ERROR, ("spec='%s' is malformed, refusing to add", spec));
    return false;
  }
  SLIST_INSERT_HEAD(&ts->specs, t_spec, entries);
  return true;
}

bool timespec_match(const struct mgos_timespec *ts, const struct tm *tm) {
  struct mgos_timespec_spec *t_spec;

  if (!ts) {
    return false;
  }

  SLIST_FOREACH(t_spec, &ts->specs, entries) {
    uint32_t start, stop, target;

    start = t_spec->start_h * 3600 + t_spec->start_m * 60 + t_spec->start_s;
    stop = t_spec->stop_h * 3600 + t_spec->stop_m * 60 + t_spec->stop_s;
    target = tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
    if (start == stop) {
      continue;
    }
    if (stop >= start) {
      if (target >= start && target < stop) {
        // LOG(LL_DEBUG, ("start=%d stop=%d target=%d matched", start, stop, target));
        return true;
      }
    } else {
      if (target >= start || target < stop) {
        // LOG(LL_DEBUG, ("start=%d stop=%d target=%d matched", start, stop, target));
        return true;
      }
    }
    // LOG(LL_DEBUG, ("start=%d stop=%d target=%d did not match", start, stop, target));
  }
  return false;
}

// Uses current time to match the timespec
bool timespec_match_now(const struct mgos_timespec *ts) {
  time_t now;
  struct tm *tm;

  if (!ts) {
    return false;
  }
  now = time(NULL);
  tm = localtime(&now);
  if (!tm) {
    return false;
  }
  return timespec_match(ts, tm);
}

// Clear the timespec linked list
// Returns true on success, false otherwise.
bool timespec_clear_spec(struct mgos_timespec *ts) {
  if (!ts) {
    return false;
  }
  while (!SLIST_EMPTY(&ts->specs)) {
    struct mgos_timespec_spec *t_spec;

    t_spec = SLIST_FIRST(&ts->specs);
    SLIST_REMOVE_HEAD(&ts->specs, entries);
    if (t_spec) {
      free(t_spec);
    }
  }

  return true;
}

// Return true if the timespec is empty
bool timespec_empty(struct mgos_timespec *ts) {
  if (!ts) {
    return true;
  }
  return SLIST_EMPTY(&ts->specs);
}

// Return a null terminated string in 'ret' of max retlen-1 which is a
// comma separated set of timespec elements from the linked list.
// Returns true on success, false otherwise.
bool timespec_get_spec(struct mgos_timespec *ts, char *ret, int retlen) {
  struct mgos_timespec_spec *t_spec;

  if (!ts) {
    return false;
  }

  if (!ts) {
    return false;
  }

  if (!ret) {
    return false;
  }
  *ret = '\0';

  SLIST_FOREACH(t_spec, &ts->specs, entries) {
    char spec_str[25];

    snprintf(spec_str, sizeof(spec_str) - 1, "%02d:%02d:%02d-%02d:%02d:%02d", t_spec->start_h, t_spec->start_m, t_spec->start_s, t_spec->stop_h,
             t_spec->stop_m, t_spec->stop_s);
    if ((int) (strlen(spec_str) + strlen(ret)) > retlen - 1) {
      return false;
    }
    if (strlen(ret) > 0) {
      strncat(ret, ",", retlen);
    }
    strncat(ret, spec_str, retlen);
  }
  return true;
}

bool timespec_write_file(struct mgos_timespec *ts, const char *fn) {
  char buf[500];
  int fd;

  if (!timespec_get_spec(ts, buf, sizeof(buf))) {
    LOG(LL_ERROR, ("Could not convert timespec to string"));
    return false;
  }

  if (!fn) {
    return false;
  }

  if (!(fd = open(fn, O_RDWR | O_CREAT | O_TRUNC))) {
    LOG(LL_ERROR, ("Could not open %s for writing", fn));
    return false;
  }
  if ((uint32_t) strlen(buf) != (uint32_t) write(fd, buf, strlen(buf))) {
    LOG(LL_ERROR, ("Short write on %s for data '%s'", fn, buf));
    return false;
  }
  close(fd);

  LOG(LL_INFO, ("Wrote timespec to %s", fn));
  return true;
}

bool timespec_read_file(struct mgos_timespec *ts, const char *fn) {
  int fd;
  char *buf;
  char *spec;
  char *buf_ptr;
  struct stat fp_stat;

  if (!ts) {
    return false;
  }

  if (!fn) {
    return false;
  }

  if (0 != stat(fn, &fp_stat)) {
    LOG(LL_ERROR, ("Could not stat %s", fn));
    return false;
  }

  if (fp_stat.st_size > 1024) {
    LOG(LL_ERROR, ("File size of %s is larger than 1024 bytes (%lu)", fn, fp_stat.st_size));
    return false;
  }

  buf = malloc(fp_stat.st_size + 1);
  if (!buf) {
    LOG(LL_ERROR, ("Could not malloc %lu bytes for file %s", fp_stat.st_size, fn));
    return false;
  }

  if (!(fd = open(fn, O_RDONLY))) {
    LOG(LL_ERROR, ("Could not open %s for reading", fn));
    free(buf);
    return false;
  }
  if (fp_stat.st_size != read(fd, buf, fp_stat.st_size)) {
    LOG(LL_ERROR, ("Could not read %lu bytes from %s", fp_stat.st_size, fn));
    close(fd);
    free(buf);
    return false;
  }
  buf[fp_stat.st_size] = '\0';
  close(fd);

  // Wipe the timespec and parse back
  timespec_clear_spec(ts);

  buf_ptr = buf;
  while ((spec = strtok_r(buf_ptr, ",", &buf_ptr))) {
    if (!timespec_add_spec(ts, spec)) {
      LOG(LL_WARN, ("Could not add spec '%s'", spec));
    }
  }
  free(buf);
  LOG(LL_INFO, ("Read timespec from %s", fn));
  return true;
}
