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
#include "mgos_homeassistant_bh1750.h"
#include "mgos_homeassistant_gpio.h"
#include "mgos_homeassistant_si7021.h"
#include "mgos_mqtt.h"

struct provider {
  ha_provider_cfg_handler cfg_handler;
  const char *module;
  const char *provider;
  SLIST_ENTRY(provider) entry;
};
static SLIST_HEAD(, provider) providers;

static struct mgos_homeassistant *s_homeassistant = NULL;

static struct provider *mgos_homeassistant_get_provider(const char *provider, size_t len) {
  struct provider *p;
  if (!len) {
    SLIST_FOREACH(p, &providers, entry) if (!strcmp(p->provider, provider)) return p;
  } else {
    SLIST_FOREACH(p, &providers, entry) if (!strncmp(p->provider, provider, len) && !p->provider[len]) return p;
  }
  return NULL;
}

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
        mgos_homeassistant_send_config((struct mgos_homeassistant *) user_data, true);
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
  mbuf_append(&o->status, "\0", 1);
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

static void mgos_homeassistant_fromjson_arr(struct mgos_homeassistant *ha, struct provider *p, struct json_token *arr) {
  struct json_token *h = NULL;
  int idx;
  struct json_token val;
  while ((h = json_next_elem(arr->ptr, arr->len, h, "", &idx, &val)) != NULL) {
    if (p->cfg_handler(ha, val)) continue;
    LOG(LL_WARN, ("Failed to add object (provider %s, index %d), JSON: %.*s", p->provider, idx, val.len, val.ptr));
  }
}

bool mgos_homeassistant_fromjson(struct mgos_homeassistant *ha, const char *json) {
  struct json_token key, val;
  void *h = NULL;
  int idx;

  char *name = NULL;

  if (!ha || !json) return false;
  size_t json_sz = strlen(json);

  // Set global config elements
  json_scanf(json, json_sz, "{name:%Q}", &name);
  if (name) {
    if (ha->node_name) free(ha->node_name);
    ha->node_name = strdup(name);
  }

  // Read providers
  while ((h = json_next_key(json, json_sz, h, ".provider", &key, &val)) != NULL) {
    struct provider *p = mgos_homeassistant_get_provider(key.ptr, key.len);
    if (p && p->cfg_handler)
      mgos_homeassistant_fromjson_arr(ha, p, &val);
    else
      LOG(LL_ERROR, ("provider.%.*s config found: add %s to mos.yml, skipping...", key.len, key.ptr, p ? p->module : "the module implementing it"));
  }

  // Read automations
  while ((h = json_next_elem(json, json_sz, h, ".automation", &idx, &val)) != NULL) {
    struct mgos_homeassistant_automation *a;

    if (!(a = mgos_homeassistant_automation_create(val))) {
      LOG(LL_WARN, ("Failed to add automation, index %d, json follows:%.*s", idx, (int) val.len, val.ptr));
      continue;
    }
    SLIST_INSERT_HEAD(&ha->automations, a, entry);
  }

  mgos_homeassistant_send_config(ha, false);
  if (name) free(name);
  return true;
}

struct mgos_homeassistant *mgos_homeassistant_get_global() {
  return s_homeassistant;
}

bool mgos_homeassistant_register_provider(const char *provider, ha_provider_cfg_handler cfg_handler, const char *module) {
  struct provider *p;
  if (!provider || !(!cfg_handler ^ !module)) return false;
  if (mgos_homeassistant_get_provider(provider, 0)) return false;
  if (!(p = malloc(sizeof(*p)))) return false;

  p->cfg_handler = cfg_handler;
  p->module = module;
  p->provider = provider;
  SLIST_INSERT_HEAD(&providers, p, entry);
  return true;
}

bool mgos_homeassistant_init(void) {
  SLIST_INIT(&providers);
  mgos_homeassistant_register_provider("barometer",
#ifdef MGOS_HAVE_BAROMETER
                                       mgos_homeassistant_barometer_fromjson, NULL
#else
                                       NULL, "barometer"
#endif
  );
  mgos_homeassistant_register_provider("bh1750",
#ifdef MGOS_HAVE_BH1750
                                       mgos_homeassistant_bh1750_fromjson, NULL
#else
                                       NULL, "bh1750-i2c"
#endif
  );
  mgos_homeassistant_register_provider("gpio", mgos_homeassistant_gpio_fromjson, NULL);
  mgos_homeassistant_register_provider("si7021",
#ifdef MGOS_HAVE_SI7021_I2C
                                       mgos_homeassistant_si7021_fromjson, NULL
#else
                                       NULL, "si7021-i2c"
#endif
  );

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
