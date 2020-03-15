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
  LOG(LL_INFO, ("GPIO %d is %s", m->gpio, m->state ? "detected" : "clear"));
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
      LOG(LL_INFO, ("GPIO %d is %s", m->gpio, m->state ? "detected" : "clear"));
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
  }

  ret = true;
  LOG(LL_INFO, ("Successfully created object %s", name));
exit:
  if (j_name) free(j_name);
  if (j_type) free(j_type);
  return ret;
}