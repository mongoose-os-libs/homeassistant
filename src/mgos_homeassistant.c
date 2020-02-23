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

#include "mgos_homeassistant_internal.h"

static void mgos_homeassistant_mqtt_connect(
    struct mg_connection *nc, const char *client_id,
    struct mg_send_mqtt_handshake_opts *opts, void *fn_arg) {
  char topic[100];
  char payload[100];
  snprintf(topic, sizeof(topic), "%s/stat", mgos_sys_config_get_device_id());
  snprintf(payload, sizeof(payload), "offline");
  LOG(LL_INFO, ("Setting will topic='%s' payload='%s', for when we disconnect",
                topic, payload));
  opts->will_topic = strdup(topic);
  opts->will_message = strdup(payload);
  opts->flags |= MG_MQTT_WILL_RETAIN;
  mg_send_mqtt_handshake_opt(nc, client_id, *opts);
  (void) fn_arg;
}

static void mgos_homeassistant_mqtt_ev(struct mg_connection *nc, int ev,
                                       void *ev_data, void *user_data) {
  switch (ev) {
    case MG_EV_MQTT_CONNACK: {
      char topic[100];
      snprintf(topic, sizeof(topic), "%s/stat",
               mgos_sys_config_get_device_id());
      mgos_mqtt_pub((char *) topic, "online", 6, 0, true);
      mgos_homeassistant_send_config((struct mgos_homeassistant *) user_data);
      break;
    }
  }
  (void) nc;
  (void) ev_data;
}

struct mgos_homeassistant *mgos_homeassistant_create(const char *node_name) {
  struct mgos_homeassistant *ha = calloc(1, sizeof(*ha));
  if (!ha) return NULL;

  if (node_name)
    ha->node_name = strdup(node_name);
  else
    ha->node_name = strdup(mgos_sys_config_get_device_id());
  SLIST_INIT(&ha->objects);

  mgos_mqtt_add_global_handler(mgos_homeassistant_mqtt_ev, ha);
  return ha;
}

bool mgos_homeassistant_fromfile(struct mgos_homeassistant *ha,
                                 const char *filename) {
  return false;
  (void) ha;
  (void) filename;
}

bool mgos_homeassistant_fromjson(struct mgos_homeassistant *ha,
                                 const char *json) {
  return false;
  (void) ha;
  (void) json;
}

bool mgos_homeassistant_send_config(struct mgos_homeassistant *ha) {
  return false;
  (void) ha;
}

bool mgos_homeassistant_send_status(struct mgos_homeassistant *ha) {
  return false;
  (void) ha;
}

bool mgos_homeassistant_destroy(struct mgos_homeassistant **ha) {
  if (!(*ha)) return false;

  if ((*ha)->node_name) free((*ha)->node_name);

  while (!SLIST_EMPTY(&(*ha)->objects)) {
    struct mgos_homeassistant_object *o;
    o = SLIST_FIRST(&(*ha)->objects);
    SLIST_REMOVE_HEAD(&(*ha)->objects, entry);
    mgos_homeassistant_object_remove(&o);
  }
  free(*ha);
  *ha = NULL;

  return true;
}

struct mgos_homeassistant_object *mgos_homeassistant_object_add(
    struct mgos_homeassistant *ha, const char *object_name,
    enum mgos_homeassistant_component ha_component,
    const char *json_config_additional_payload, ha_status_cb status,
    ha_cmd_cb cmd, void *user_data) {
  struct mgos_homeassistant_object *o = calloc(1, sizeof(*o));

  if (!o || !ha || !object_name) return NULL;
  o->ha = ha;
  o->component = ha_component;
  o->object_name = strdup(object_name);
  if (json_config_additional_payload)
    o->json_config_additional_payload = strdup(json_config_additional_payload);
  o->user_data = user_data;
  o->status = status;
  o->cmd = cmd;
  SLIST_INSERT_HEAD(&ha->objects, o, entry);

  return o;
}

struct mgos_homeassistant_object *mgos_homeassistant_object_search(
    struct mgos_homeassistant *ha, const char *query) {
  return NULL;
  (void) ha;
  (void) query;
}

void *mgos_homeassistant_object_get_userdata(
    struct mgos_homeassistant_object *o) {
  return NULL;
  (void) o;
}

bool mgos_homeassistant_object_send_status(
    struct mgos_homeassistant_object *o) {
  return false;
  (void) o;
}

bool mgos_homeassistant_object_send_config(
    struct mgos_homeassistant_object *o) {
  return false;
  (void) o;
}

bool mgos_homeassistant_object_remove(struct mgos_homeassistant_object **o) {
  return false;
  (void) o;
}

bool mgos_homeassistant_object_class_add(
    struct mgos_homeassistant_object *o, const char *class_name,
    const char *json_config_additional_payload, ha_status_cb cb) {
  return false;
  (void) o;
  (void) class_name;
  (void) json_config_additional_payload;
  (void) cb;
}

bool mgos_homeassistant_object_class_remove(struct mgos_homeassistant_object *o,
                                            const char *class_name) {
  return false;
  (void) o;
  (void) class_name;
}

bool mgos_homeassistant_init(void) {
  mgos_mqtt_set_connect_fn(mgos_homeassistant_mqtt_connect, NULL);
  return true;
}
