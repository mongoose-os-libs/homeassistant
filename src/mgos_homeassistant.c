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

#include "mgos_homeassistant.h"

#include "mgos.h"
#include "mgos_homeassistant_automation.h"
#include "mgos_homeassistant_barometer.h"
#include "mgos_homeassistant_gpio.h"
#include "mgos_homeassistant_si7021.h"
#include "mgos_mqtt.h"

static struct mgos_homeassistant *s_homeassistant = NULL;

static void mgos_homeassistant_mqtt_connect(struct mg_connection *nc, const char *client_id, struct mg_send_mqtt_handshake_opts *opts, void *fn_arg) {
  LOG(LL_DEBUG, ("Setting will topic='%s' payload='offline', for when we disconnect", mgos_sys_config_get_device_id()));
  opts->will_topic = mgos_sys_config_get_device_id();
  opts->will_message = "offline";
  opts->flags |= MG_MQTT_WILL_RETAIN;
  mg_send_mqtt_handshake_opt(nc, client_id, *opts);
  (void) fn_arg;
}

static void mgos_homeassistant_mqtt_ev(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
  switch (ev) {
    case MG_EV_MQTT_CONNACK: {
      mgos_mqtt_pub(mgos_sys_config_get_device_id(), "online", 6, 0, true);
      if (user_data) {
        mgos_homeassistant_send_config((struct mgos_homeassistant *) user_data);
        mgos_homeassistant_send_status((struct mgos_homeassistant *) user_data);
      }
      break;
    }
  }
  (void) nc;
  (void) ev_data;
  (void) user_data;
}

static bool mgos_homeassistant_automation_run_status(struct mgos_homeassistant_object *o) {
  struct mgos_homeassistant_automation *a;
  struct mgos_homeassistant_automation_data_status d;
  if (!o || !o->ha) return false;

  d.object = o->object_name;
  o->status.buf[o->status.len] = 0;
  d.status = o->status.buf;
  LOG(LL_DEBUG, ("Running automations for trigger object=%s status=%s", d.object, d.status));

  SLIST_FOREACH(a, &o->ha->automations, entry) {
    mgos_homeassistant_automation_run(a, TRIGGER_STATUS, &d, o->ha);
  }
  return true;
}

static void mgos_homeassistant_handler(struct mgos_homeassistant *ha, const int ev, const void *ev_data, void *user_data) {
  if (!ha) return;
  LOG(LL_DEBUG, ("Node '%s' event: %d", ha->node_name, ev));

  if (ev == MGOS_HOMEASSISTANT_EV_OBJECT_STATUS) {
    mgos_homeassistant_automation_run_status((struct mgos_homeassistant_object *) ev_data);
    return;
  }
  (void) user_data;
}

bool mgos_homeassistant_fromfile(struct mgos_homeassistant *ha, const char *filename) {
  char *json = json_fread(filename);
  bool ret = false;
  if (!json) goto exit;
  if (!mgos_homeassistant_fromjson(ha, json)) goto exit;
  ret = true;
exit:
  if (json) free(json);
  return ret;
}

bool mgos_homeassistant_fromjson(struct mgos_homeassistant *ha, const char *json) {
  struct json_token val;
  void *h = NULL;
  int idx;

  char *name = NULL;

  if (!ha || !json) return false;

  // Set global config elements
  json_scanf(json, strlen(json), "{name:%Q}", &name);
  if (name) {
    if (ha->node_name) free(ha->node_name);
    ha->node_name = strdup(name);
  }

  // Read providers
  while ((h = json_next_elem(json, strlen(json), h, ".provider.gpio", &idx, &val)) != NULL) {
    if (!mgos_homeassistant_gpio_fromjson(ha, val)) {
      LOG(LL_WARN, ("Failed to add object from provider gpio, index %d, json "
                    "follows:%.*s",
                    idx, (int) val.len, val.ptr));
    }
  }

  while ((h = json_next_elem(json, strlen(json), h, ".provider.si7021", &idx, &val)) != NULL) {
#ifdef MGOS_HAVE_SI7021_I2C
    if (!mgos_homeassistant_si7021_fromjson(ha, val)) {
      LOG(LL_WARN, ("Failed to add object from provider si7021, index %d, json "
                    "follows:%.*s",
                    idx, (int) val.len, val.ptr));
    }
#else
    LOG(LL_ERROR, ("provider.si7021 config found: Add si7021-i2c to mos.yml, "
                   "skipping .. "));
#endif
  }

  while ((h = json_next_elem(json, strlen(json), h, ".provider.barometer", &idx, &val)) != NULL) {
#ifdef MGOS_HAVE_BAROMETER
    if (!mgos_homeassistant_barometer_fromjson(ha, val)) {
      LOG(LL_WARN, ("Failed to add object from provider barometer, index %d, json "
                    "follows:%.*s",
                    idx, (int) val.len, val.ptr));
    }
#else
    LOG(LL_ERROR, ("provider.barometer config found: Add barometer to mos.yml, "
                   "skipping .. "));
#endif
  }

  // Read automations
  while ((h = json_next_elem(json, strlen(json), h, ".automation", &idx, &val)) != NULL) {
    struct mgos_homeassistant_automation *a;

    if (!(a = mgos_homeassistant_automation_create(val))) {
      LOG(LL_WARN, ("Failed to add automation, index %d, json follows:%.*s", idx, (int) val.len, val.ptr));
      continue;
    }
    SLIST_INSERT_HEAD(&ha->automations, a, entry);
  }

  if (name) free(name);
  return true;
}

struct mgos_homeassistant *mgos_homeassistant_get_global() {
  return s_homeassistant;
}

bool mgos_homeassistant_init(void) {
  s_homeassistant = calloc(1, sizeof(struct mgos_homeassistant));
  if (!s_homeassistant) return false;

  s_homeassistant->node_name = strdup(mgos_sys_config_get_device_id());
  SLIST_INIT(&s_homeassistant->objects);
  SLIST_INIT(&s_homeassistant->automations);
  SLIST_INIT(&s_homeassistant->handlers);
  mgos_homeassistant_add_handler(s_homeassistant, mgos_homeassistant_handler, NULL);

  mgos_mqtt_add_global_handler(mgos_homeassistant_mqtt_ev, s_homeassistant);
  mgos_mqtt_set_connect_fn(mgos_homeassistant_mqtt_connect, NULL);
  LOG(LL_DEBUG, ("Created homeassistant node '%s'", s_homeassistant->node_name));
  return true;
}
