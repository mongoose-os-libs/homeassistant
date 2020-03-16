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

#include "mgos_homeassistant_gpio.h"

#include <strings.h>
#include <math.h>

static void motion_stat(struct mgos_homeassistant_object *o,
                        struct json_out *json) {
  struct mgos_homeassistant_gpio_motion *m;
  if (!o || !json) return;

  m = (struct mgos_homeassistant_gpio_motion *) o->user_data;
  if (!m) return;

  json_printf(json, "occupancy:%B", m->state);
}

static void motion_timeout_cb(void *ud) {
  struct mgos_homeassistant_object *o = (struct mgos_homeassistant_object *) ud;
  struct mgos_homeassistant_gpio_motion *m;

  if (!o) return;

  m = (struct mgos_homeassistant_gpio_motion *) o->user_data;
  if (!m) return;

  m->timer = 0;
  m->state = false;
  LOG(LL_DEBUG, ("GPIO %d is %s", m->gpio, m->state ? "detected" : "clear"));
  mgos_homeassistant_object_send_status(o);
}

static void motion_button_cb(int gpio, void *ud) {
  struct mgos_homeassistant_object *o = (struct mgos_homeassistant_object *) ud;
  struct mgos_homeassistant_gpio_motion *m;
  int level;

  if (!o) return;

  m = (struct mgos_homeassistant_gpio_motion *) o->user_data;
  if (!m) return;
  if (m->gpio != gpio) return;

  level = mgos_gpio_read(m->gpio);
  LOG(LL_DEBUG, ("GPIO %d: state=%d level=%d timeout=%d", m->gpio, m->state,
                 level, m->timeout_secs));
  if (0 == level) {
    mgos_clear_timer(m->timer);
    m->timer =
        mgos_set_timer(m->timeout_secs * 1000, false, motion_timeout_cb, o);
  } else {
    if (!m->state) {
      m->state = true;
      LOG(LL_DEBUG,
          ("GPIO %d is %s", m->gpio, m->state ? "detected" : "clear"));
      mgos_homeassistant_object_send_status(o);
    }
  }
}

static bool mgos_homeassistant_gpio_motion_fromjson(
    struct mgos_homeassistant *ha, const char *object_name, int gpio,
    struct json_token val) {
  struct mgos_homeassistant_gpio_motion *user_data =
      calloc(1, sizeof(*user_data));
  struct mgos_homeassistant_object *o = NULL;
  int j_timeout = 90;
  int j_debounce = 10;

  if (!user_data || !ha) return false;

  json_scanf(val.ptr, val.len, "{timeout:%d,debounce:%d}", &j_timeout,
             &j_debounce);
  user_data->gpio = gpio;
  user_data->timeout_secs = j_timeout;
  user_data->debounce_ms = j_debounce;
  o = mgos_homeassistant_object_add(
      ha, object_name, COMPONENT_BINARY_SENSOR,
      "\"payload_on\":true,\"payload_off\":false,\"value_template\":\"{{ "
      "value_json.occupancy }}\",\"device_class\":\"motion\"",
      motion_stat, user_data);
  mgos_gpio_set_button_handler(user_data->gpio, MGOS_GPIO_PULL_UP,
                               MGOS_GPIO_INT_EDGE_ANY, user_data->debounce_ms,
                               motion_button_cb, o);

  return true;
}

static void momentary_stat(struct mgos_homeassistant_object *o,
                           struct json_out *json) {
  struct mgos_homeassistant_gpio_binary_sensor *d;

  if (!o || !json) return;
  d = (struct mgos_homeassistant_gpio_binary_sensor *) o->user_data;
  if (!d) return;

  if (d->click_count > 0)
    json_printf(json, "action:click,count:%u", d->click_count);
  else
    json_printf(json, "action:%Q", "");
}

static void momentary_timer_cb(void *user_data) {
  struct mgos_homeassistant_object *o =
      (struct mgos_homeassistant_object *) user_data;
  struct mgos_homeassistant_gpio_binary_sensor *d;

  if (!o) return;
  d = (struct mgos_homeassistant_gpio_binary_sensor *) o->user_data;
  if (!d) return;

  if (d->click_count > 0) {
    mgos_homeassistant_object_send_status(o);
    d->click_count = 0;
    mgos_homeassistant_object_send_status(o);
  }

  // Reset state after timeout.
  d->click_timer = 0;
}

static void momentary_button_cb(int gpio, void *user_data) {
  struct mgos_homeassistant_object *o =
      (struct mgos_homeassistant_object *) user_data;
  struct mgos_homeassistant_gpio_binary_sensor *d;

  if (!o) return;
  d = (struct mgos_homeassistant_gpio_binary_sensor *) o->user_data;
  if (!d || d->gpio != gpio) return;

  bool level = mgos_gpio_read(d->gpio);

  if ((d->invert && level) || (!d->invert && !level)) {
    d->click_count++;
  }
  if (d->click_timer) mgos_clear_timer(d->click_timer);
  d->click_timer = mgos_set_timer(d->timeout_ms, 0, momentary_timer_cb, o);
}

static bool mgos_homeassistant_gpio_momentary_fromjson(
    struct mgos_homeassistant *ha, const char *object_name, int gpio,
    struct json_token val) {
  struct mgos_homeassistant_gpio_binary_sensor *user_data =
      calloc(1, sizeof(*user_data));
  struct mgos_homeassistant_object *o = NULL;
  bool ret = false;

  if (!user_data || !ha) return false;

  user_data->gpio = gpio;
  user_data->debounce_ms = 10;
  user_data->invert = false;
  user_data->timeout_ms = 350;
  json_scanf(val.ptr, val.len, "{invert:%B, debounce:%d, timeout:%d}",
             &user_data->invert, &user_data->debounce_ms,
             &user_data->timeout_ms);

  o = mgos_homeassistant_object_add(
      ha, object_name, COMPONENT_SENSOR,
      "\"value_template\": \"{{ value_json.action }}\"", momentary_stat,
      user_data);
  if (!o) goto exit;

  if (!mgos_gpio_set_button_handler(
          user_data->gpio,
          user_data->invert ? MGOS_GPIO_PULL_UP : MGOS_GPIO_PULL_DOWN,
          MGOS_GPIO_INT_EDGE_ANY, user_data->debounce_ms, momentary_button_cb,
          o)) {
    LOG(LL_ERROR, ("Failed to initialize GPIO momentary: gpio=%d invert=%d "
                   "debounce=%d timeout=%d",
                   user_data->gpio, user_data->invert, user_data->debounce_ms,
                   user_data->timeout_ms));
    goto exit;
  }
  LOG(LL_DEBUG, ("New GPIO momentary: gpio=%d invert=%d debounce=%d timeout=%d",
                 user_data->gpio, user_data->invert, user_data->debounce_ms,
                 user_data->timeout_ms));

  ret = true;
exit:
  return ret;
}

static void toggle_stat(struct mgos_homeassistant_object *o,
                        struct json_out *json) {
  struct mgos_homeassistant_gpio_binary_sensor *d;

  if (!o || !json) return;
  d = (struct mgos_homeassistant_gpio_binary_sensor *) o->user_data;
  if (!d) return;

  bool level = mgos_gpio_read(d->gpio);
  if ((d->invert && level) || (!d->invert && !level)) {
    json_printf(json, "state:OFF");
  } else {
    json_printf(json, "state:ON");
  }
}

static void toggle_button_cb(int gpio, void *user_data) {
  struct mgos_homeassistant_object *o =
      (struct mgos_homeassistant_object *) user_data;
  struct mgos_homeassistant_gpio_binary_sensor *d;

  if (!o) return;
  d = (struct mgos_homeassistant_gpio_binary_sensor *) o->user_data;
  if (!d || d->gpio != gpio) return;

  mgos_homeassistant_object_send_status(o);
}

static bool mgos_homeassistant_gpio_toggle_fromjson(
    struct mgos_homeassistant *ha, const char *object_name, int gpio,
    struct json_token val) {
  struct mgos_homeassistant_gpio_binary_sensor *user_data =
      calloc(1, sizeof(*user_data));
  struct mgos_homeassistant_object *o = NULL;
  bool ret = false;

  if (!user_data || !ha) return false;

  user_data->gpio = gpio;
  user_data->debounce_ms = 10;
  user_data->invert = false;
  user_data->timeout_ms = -1;
  json_scanf(val.ptr, val.len, "{invert:%B, debounce:%d}", &user_data->invert,
             &user_data->debounce_ms);

  o = mgos_homeassistant_object_add(
      ha, object_name, COMPONENT_BINARY_SENSOR,
      "\"value_template\": \"{{ value_json.state }}\"", toggle_stat,
      user_data);
  if (!o) goto exit;

  if (!mgos_gpio_set_button_handler(
          user_data->gpio,
          user_data->invert ? MGOS_GPIO_PULL_UP : MGOS_GPIO_PULL_DOWN,
          MGOS_GPIO_INT_EDGE_ANY, user_data->debounce_ms, toggle_button_cb,
          o)) {
    LOG(LL_ERROR, ("Failed to initialize GPIO toggle: gpio=%d invert=%d "
                   "debounce=%d",
                   user_data->gpio, user_data->invert, user_data->debounce_ms));
    goto exit;
  }
  LOG(LL_DEBUG, ("New GPIO toggle: gpio=%d invert=%d debounce=%d",
                 user_data->gpio, user_data->invert, user_data->debounce_ms));

  ret = true;
exit:
  return ret;
}

static void switch_stat(struct mgos_homeassistant_object *o,
                        struct json_out *json) {
  struct mgos_homeassistant_gpio_switch *d;

  if (!o || !json) return;
  d = (struct mgos_homeassistant_gpio_switch *) o->user_data;
  if (!d) return;

  bool level = mgos_gpio_read_out(d->gpio);
  if (d->invert) level = !level;
  LOG(LL_DEBUG, ("gpio=%d level=%d invert=%d", d->gpio, level, d->invert));
  json_printf(json, "state:%Q", level ? "ON" : "OFF");
}

static void switch_timer_cb(void *user_data) {
  struct mgos_homeassistant_object *o =
      (struct mgos_homeassistant_object *) user_data;
  struct mgos_homeassistant_gpio_switch *d;

  if (!o) return;
  d = (struct mgos_homeassistant_gpio_switch *) o->user_data;
  if (!d) return;

  mgos_gpio_toggle(d->gpio);
  mgos_homeassistant_object_send_status(o);
  d->timer = 0;
}

static void switch_cmd_cb(struct mgos_homeassistant_object *o,
                          const char *payload, const int payload_len) {
  struct mgos_homeassistant_gpio_switch *d;

  if (!o) return;
  d = (struct mgos_homeassistant_gpio_switch *) o->user_data;
  if (!d) return;

  // Handle both literals (ON, 1, OFF, 0, TOGGLE) and JSON
  if (((payload_len == 2) && (0 == strncasecmp(payload, "ON", 2))) ||
      ((payload_len == 1) && (0 == strncmp(payload, "1", 1)))) {
    mgos_gpio_write(d->gpio, d->invert ? 0 : 1);
  } else if (((payload_len == 3) && (0 == strncasecmp(payload, "OFF", 3))) ||
             ((payload_len == 1) && (0 == strncmp(payload, "0", 1)))) {
    mgos_gpio_write(d->gpio, d->invert ? 1 : 0);
  } else if ((payload_len == 6) && (0 == strncasecmp(payload, "TOGGLE", 6))) {
    mgos_gpio_toggle(d->gpio);
  } else {
    // JSON variant
    char *j_state = NULL;
    float j_duration = NAN;
    json_scanf(payload, payload_len, "{state:%Q,duration:%f}", &j_state,
               &j_duration);
    if (!j_state) goto exit;
    if (0 == strcasecmp(j_state, "ON"))
      mgos_gpio_write(d->gpio, d->invert ? 0 : 1);
    else if (0 == strcasecmp(j_state, "OFF"))
      mgos_gpio_write(d->gpio, d->invert ? 1 : 0);
    else if (0 == strcasecmp(j_state, "TOGGLE"))
      mgos_gpio_toggle(d->gpio);
    if (j_duration > 0)
      d->timer = mgos_set_timer(1000 * j_duration, false, switch_timer_cb, o);
    free(j_state);
  }

exit:
  mgos_homeassistant_object_send_status(o);
  return;
}

static bool mgos_homeassistant_gpio_switch_fromjson(
    struct mgos_homeassistant *ha, const char *object_name, int gpio,
    struct json_token val) {
  struct mgos_homeassistant_gpio_switch *user_data =
      calloc(1, sizeof(*user_data));
  struct mgos_homeassistant_object *o = NULL;
  bool ret = false;

  if (!user_data || !ha) return false;

  user_data->gpio = gpio;
  user_data->invert = false;
  json_scanf(val.ptr, val.len, "{invert:%B}", &user_data->invert);

  o = mgos_homeassistant_object_add(
      ha, object_name, COMPONENT_SWITCH,
      "\"value_template\":\"{{ value_json.state }}\"", switch_stat, user_data);
  if (!o) goto exit;
  mgos_homeassistant_object_set_cmd_cb(o, switch_cmd_cb);

  if (!mgos_gpio_setup_output(user_data->gpio, user_data->invert ? 1 : 0)) {
    LOG(LL_ERROR, ("Failed to initialize GPIO switch: gpio=%d invert=%d",
                   user_data->gpio, user_data->invert));
    goto exit;
  } else {
    LOG(LL_DEBUG, ("New GPIO switch: gpio=%d invert=%d", user_data->gpio,
                   user_data->invert));
  }

  ret = true;
exit:
  return ret;
}

bool mgos_homeassistant_gpio_fromjson(struct mgos_homeassistant *ha,
                                      struct json_token val) {
  bool ret = false;
  char object_name[20];
  char *name = NULL;
  char *j_name = NULL;
  char *j_type = NULL;
  int j_gpio = -1;

  if (!ha) goto exit;

  json_scanf(val.ptr, val.len, "{name:%Q,type:%Q,gpio:%d}", &j_name, &j_type,
             &j_gpio);
  if (!j_type) {
    LOG(LL_ERROR, ("Missing mandatory field: type"));
    goto exit;
  }
  if (j_gpio < 0) {
    LOG(LL_ERROR, ("Missing mandatory field: gpio"));
    goto exit;
  }

  if (j_name) {
    name = j_name;
  } else {
    mgos_homeassistant_object_generate_name(ha, "gpio_", object_name,
                                            sizeof(object_name));
    name = object_name;
  }

  if (0 == strcasecmp("motion", j_type)) {
    if (!mgos_homeassistant_gpio_motion_fromjson(ha, name, j_gpio, val)) {
      LOG(LL_WARN,
          ("Failed to add motion object for provider gpio, skipping .."));
      goto exit;
    }
  } else if (0 == strcasecmp("momentary", j_type)) {
    if (!mgos_homeassistant_gpio_momentary_fromjson(ha, name, j_gpio, val)) {
      LOG(LL_WARN,
          ("Failed to add momentary object for provider gpio, skipping .."));
    }
  } else if (0 == strcasecmp("toggle", j_type)) {
    if (!mgos_homeassistant_gpio_toggle_fromjson(ha, name, j_gpio, val)) {
      LOG(LL_WARN,
          ("Failed to add toggle object for provider gpio, skipping .."));
    }
  } else if (0 == strcasecmp("switch", j_type)) {
    if (!mgos_homeassistant_gpio_switch_fromjson(ha, name, j_gpio, val)) {
      LOG(LL_WARN,
          ("Failed to add switch object for provider gpio, skipping .."));
    }
  }

  ret = true;
  LOG(LL_DEBUG, ("Successfully created object %s", name));
exit:
  if (j_name) free(j_name);
  if (j_type) free(j_type);
  return ret;
}
