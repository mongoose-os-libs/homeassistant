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

static bool mgos_homeassistant_isvalid_name(const char *s) {
  if (!s) return false;
  if (strlen(s) > 20) return false;

  if (!isalnum((int) s[0])) return false;

  for (size_t i = 1; i < strlen(s); i++)
    if (!(isalnum((int) s[i]) || s[i] == '_')) return false;

  return true;
}

static bool mgos_homeassistant_exists_objectname(struct mgos_homeassistant *ha,
                                                 const char *s) {
  struct mgos_homeassistant_object *o;
  if (!ha || !s) return false;

  SLIST_FOREACH(o, &ha->objects, entry) {
    if (0 == strcasecmp(s, o->object_name)) return true;
  }

  return false;
}

static bool mgos_homeassistant_exists_classname(
    struct mgos_homeassistant_object *o, const char *s) {
  struct mgos_homeassistant_object_class *c;
  if (!o || !s) return false;

  SLIST_FOREACH(c, &o->classes, entry) {
    if (0 == strcasecmp(s, c->class_name)) return true;
  }

  return false;
}

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

  if (node_name) {
    if (!mgos_homeassistant_isvalid_name(node_name)) {
      LOG(LL_ERROR, ("Invalid node name '%s'", node_name));
      free(ha);
      return NULL;
    }
    ha->node_name = strdup(node_name);
  } else {
    if (!mgos_homeassistant_isvalid_name(mgos_sys_config_get_device_id())) {
      LOG(LL_ERROR, ("Invalid node name '%s' (from device.id)",
                     mgos_sys_config_get_device_id()));
      free(ha);
      return NULL;
    }
    ha->node_name = strdup(mgos_sys_config_get_device_id());
  }
  SLIST_INIT(&ha->objects);

  mgos_mqtt_add_global_handler(mgos_homeassistant_mqtt_ev, ha);
  LOG(LL_DEBUG, ("Created node '%s'", ha->node_name));
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

  LOG(LL_DEBUG, ("Destroying node '%s'", (*ha)->node_name));

  while (!SLIST_EMPTY(&(*ha)->objects)) {
    struct mgos_homeassistant_object *o;
    o = SLIST_FIRST(&(*ha)->objects);
    mgos_homeassistant_object_remove(&o);
  }
  if ((*ha)->node_name) free((*ha)->node_name);

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
  if (!mgos_homeassistant_isvalid_name(object_name)) {
    LOG(LL_ERROR, ("Invalid object name '%s'", object_name));
    free(o);
    return NULL;
  }
  if (mgos_homeassistant_exists_objectname(ha, object_name)) {
    LOG(LL_ERROR, ("Object name '%s' already exists in node '%s'", object_name,
                   ha->node_name));
    free(o);
    return NULL;
  }

  o->ha = ha;
  o->component = ha_component;
  o->object_name = strdup(object_name);
  if (json_config_additional_payload)
    o->json_config_additional_payload = strdup(json_config_additional_payload);
  o->user_data = user_data;
  o->status = status;
  o->cmd = cmd;
  SLIST_INIT(&o->classes);
  SLIST_INSERT_HEAD(&ha->objects, o, entry);

  LOG(LL_DEBUG,
      ("Created object '%s' on node '%s'", o->object_name, o->ha->node_name));
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
  if (!(*o) || !(*o)->ha) return false;

  LOG(LL_DEBUG, ("Removing object '%s' from node '%s'", (*o)->object_name,
                 (*o)->ha->node_name));

  while (!SLIST_EMPTY(&(*o)->classes)) {
    struct mgos_homeassistant_object_class *c;
    c = SLIST_FIRST(&(*o)->classes);
    mgos_homeassistant_object_class_remove(&c);
  }

  if ((*o)->object_name) free((*o)->object_name);
  if ((*o)->json_config_additional_payload)
    free((*o)->json_config_additional_payload);

  SLIST_REMOVE(&(*o)->ha->objects, (*o), mgos_homeassistant_object, entry);

  free(*o);
  *o = NULL;
  return true;
}

struct mgos_homeassistant_object_class *mgos_homeassistant_object_class_add(
    struct mgos_homeassistant_object *o, const char *class_name,
    const char *json_config_additional_payload, ha_status_cb status) {
  struct mgos_homeassistant_object_class *c = calloc(1, sizeof(*c));

  if (!c || !o || !class_name) return NULL;
  if (!mgos_homeassistant_isvalid_name(class_name)) {
    LOG(LL_ERROR, ("Invalid class name '%s'", class_name));
    free(c);
    return NULL;
  }
  if (mgos_homeassistant_exists_classname(o, class_name)) {
    LOG(LL_ERROR, ("Class name '%s' already exists in object '%s'", class_name,
                   o->object_name));
    free(c);
    return NULL;
  }

  c->object = o;
  c->class_name = strdup(class_name);
  if (json_config_additional_payload)
    c->json_config_additional_payload = strdup(json_config_additional_payload);
  c->status = status;
  SLIST_INSERT_HEAD(&o->classes, c, entry);

  LOG(LL_DEBUG, ("Created class '%s' on object '%s'", c->class_name,
                 c->object->object_name));
  return c;
}

bool mgos_homeassistant_object_class_remove(
    struct mgos_homeassistant_object_class **c) {
  if (!(*c) || !(*c)->object) return false;

  LOG(LL_DEBUG, ("Removing class '%s' from object '%s'", (*c)->class_name,
                 (*c)->object->object_name));

  if ((*c)->class_name) free((*c)->class_name);
  if ((*c)->json_config_additional_payload)
    free((*c)->json_config_additional_payload);

  SLIST_REMOVE(&(*c)->object->classes, (*c), mgos_homeassistant_object_class,
               entry);

  free(*c);
  *c = NULL;
  return true;
}

bool mgos_homeassistant_init(void) {
  mgos_mqtt_set_connect_fn(mgos_homeassistant_mqtt_connect, NULL);
  return true;
}
