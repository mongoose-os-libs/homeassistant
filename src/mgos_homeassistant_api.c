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

#include "mgos_homeassistant_api.h"

#include "mgos.h"
#include "mgos_config.h"
#include "mgos_mqtt.h"
#include "mgos_ro_vars.h"

extern const char *mg_build_id;
extern const char *mg_build_version;

static struct mgos_homeassistant *s_homeassistant = NULL;

static const char *ha_component_str(
    enum mgos_homeassistant_component ha_component) {
  switch (ha_component) {
    case COMPONENT_ALARM_CONTROL_PANEL:
      return "alarm_control_panel";
      break;
    case COMPONENT_BINARY_SENSOR:
      return "binary_sensor";
      break;
    case COMPONENT_CAMERA:
      return "camera";
      break;
    case COMPONENT_CLIMATE:
      return "climate";
      break;
    case COMPONENT_COVER:
      return "cover";
      break;
    case COMPONENT_FAN:
      return "fan";
      break;
    case COMPONENT_LIGHT:
      return "light";
      break;
    case COMPONENT_LOCK:
      return "lock";
      break;
    case COMPONENT_SENSOR:
      return "sensor";
      break;
    case COMPONENT_SWITCH:
      return "switch";
      break;
    case COMPONENT_VACUUM:
      return "vacuum";
      break;
    default:
      break;
  }
  return "none";
}

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
  LOG(LL_DEBUG, ("Setting will topic='%s' payload='%s', for when we disconnect",
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

static bool endswith(const char *str, size_t str_len, const char *suffix) {
  if (!str || !suffix) return false;
  size_t suffix_len = strlen(suffix);
  if (suffix_len > str_len) return false;
  return strncmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

static void mgos_homeassistant_mqtt_cb(struct mg_connection *nc,
                                       const char *topic, int topic_len,
                                       const char *msg, int msg_len, void *ud) {
  struct mgos_homeassistant_object *o = (struct mgos_homeassistant_object *) ud;
  if (!o) return;

  if (endswith(topic, (size_t) topic_len, "/cmd") && o->cmd) {
    LOG(LL_DEBUG, ("Received CMD object='%s' topic='%.*s' payload='%.*s'",
                   o->object_name, topic_len, topic, msg_len, msg));
    o->cmd(o, msg, msg_len);
  }
  if (endswith(topic, (size_t) topic_len, "/attr") && o->attr) {
    LOG(LL_DEBUG, ("Received ATTR object='%s' topic='%.*s' payload='%.*s'",
                   o->object_name, topic_len, topic, msg_len, msg));
    o->attr(o, msg, msg_len);
  }
  (void) nc;
}

static char *gen_configtopic(struct mbuf *m,
                             const struct mgos_homeassistant_object *o,
                             const struct mgos_homeassistant_object_class *c) {
  mbuf_append(m, mgos_sys_config_get_homeassistant_discovery_prefix(),
              strlen(mgos_sys_config_get_homeassistant_discovery_prefix()));
  mbuf_append(m, "/", 1);
  mbuf_append(m, ha_component_str(o->component),
              strlen(ha_component_str(o->component)));
  mbuf_append(m, "/", 1);
  mbuf_append(m, o->ha->node_name, strlen(o->ha->node_name));
  mbuf_append(m, "/", 1);
  mbuf_append(m, o->object_name, strlen(o->object_name));
  if (c) {
    mbuf_append(m, "_", 1);
    mbuf_append(m, c->class_name, strlen(c->class_name));
  }
  mbuf_append(m, "/config", 7);
  m->buf[m->len] = 0;
  return m->buf;
}

static char *gen_topicprefix(struct mbuf *m,
                             const struct mgos_homeassistant_object *o) {
  mbuf_append(m, o->ha->node_name, strlen(o->ha->node_name));
  mbuf_append(m, "/", 1);
  mbuf_append(m, ha_component_str(o->component),
              strlen(ha_component_str(o->component)));
  mbuf_append(m, "/", 1);
  mbuf_append(m, o->object_name, strlen(o->object_name));
  return m->buf;
}

static char *gen_friendlyname(struct mbuf *m,
                              const struct mgos_homeassistant_object *o,
                              const struct mgos_homeassistant_object_class *c) {
  mbuf_append(m, o->ha->node_name, strlen(o->ha->node_name));
  mbuf_append(m, "_", 1);
  mbuf_append(m, o->object_name, strlen(o->object_name));
  if (c) {
    mbuf_append(m, "_", 1);
    mbuf_append(m, c->class_name, strlen(c->class_name));
  }
  return m->buf;
}

bool mgos_homeassistant_send_config(struct mgos_homeassistant *ha) {
  struct mgos_homeassistant_object *o;
  if (!ha) return false;
  int done = 0, success = 0;

  if (!ha) return false;

  SLIST_FOREACH(o, &ha->objects, entry) {
    done++;
    if (mgos_homeassistant_object_send_config(o)) success++;
  }
  LOG((done == success) ? LL_DEBUG : LL_WARN,
      ("Sent %u configs (%u successfully) for node '%s'", done, success,
       ha->node_name));
  return (done == success);
}

bool mgos_homeassistant_send_status(struct mgos_homeassistant *ha) {
  struct mgos_homeassistant_object *o;
  if (!ha) return false;
  SLIST_FOREACH(o, &ha->objects, entry) {
    mgos_homeassistant_object_send_status(o);
  }
  return true;
}

bool mgos_homeassistant_clear(struct mgos_homeassistant *ha) {
  if (!ha) return false;

  LOG(LL_DEBUG, ("Clearing node '%s'", ha->node_name));

  while (!SLIST_EMPTY(&ha->objects)) {
    struct mgos_homeassistant_object *o;
    o = SLIST_FIRST(&ha->objects);
    mgos_homeassistant_object_remove(&o);
  }

  return true;
}

struct mgos_homeassistant_object *mgos_homeassistant_object_add(
    struct mgos_homeassistant *ha, const char *object_name,
    enum mgos_homeassistant_component ha_component,
    const char *json_config_additional_payload, ha_status_cb status,
    void *user_data) {
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
  SLIST_INIT(&o->classes);
  SLIST_INSERT_HEAD(&ha->objects, o, entry);

  LOG(LL_DEBUG,
      ("Created object '%s' on node '%s'", o->object_name, o->ha->node_name));
  return o;
}

bool mgos_homeassistant_object_set_cmd_cb(struct mgos_homeassistant_object *o,
                                          ha_cmd_cb cmd) {
  struct mbuf mbuf_topic;

  if (!o) return false;
  o->cmd = cmd;

  mbuf_init(&mbuf_topic, 100);
  gen_topicprefix(&mbuf_topic, o);
  mbuf_append(&mbuf_topic, "/cmd", 4);
  mbuf_topic.buf[mbuf_topic.len] = 0;
  mgos_mqtt_sub(mbuf_topic.buf, mgos_homeassistant_mqtt_cb, o);
  mbuf_free(&mbuf_topic);

  return true;
}

bool mgos_homeassistant_object_set_attr_cb(struct mgos_homeassistant_object *o,
                                           ha_attr_cb attr) {
  struct mbuf mbuf_topic;
  if (!o) return false;
  o->attr = attr;

  mbuf_init(&mbuf_topic, 100);
  gen_topicprefix(&mbuf_topic, o);
  mbuf_append(&mbuf_topic, "/attr", 5);
  mbuf_topic.buf[mbuf_topic.len] = 0;
  mgos_mqtt_sub(mbuf_topic.buf, mgos_homeassistant_mqtt_cb, o);
  mbuf_free(&mbuf_topic);

  return true;
}

struct mgos_homeassistant_object *mgos_homeassistant_object_search(
    struct mgos_homeassistant *ha, const char *query) {
  return NULL;
  (void) ha;
  (void) query;
}

bool mgos_homeassistant_object_send_status(
    struct mgos_homeassistant_object *o) {
  struct mgos_homeassistant_object_class *c = NULL;
  struct mbuf mbuf_topic;
  struct mbuf mbuf_payload;
  struct json_out payload = JSON_OUT_MBUF(&mbuf_payload);
  bool ret = false;
  int i;
  size_t len;

  if (!o) goto exit;

  mbuf_init(&mbuf_topic, 100);
  gen_topicprefix(&mbuf_topic, o);
  mbuf_append(&mbuf_topic, "/stat", 5);
  mbuf_topic.buf[mbuf_topic.len] = 0;

  mbuf_init(&mbuf_payload, 100);
  json_printf(&payload, "{");
  len = mbuf_payload.len;
  o->status(o, &payload);

  i = 0;
  SLIST_FOREACH(c, &o->classes, entry) {
    if (i == 0 && len != mbuf_payload.len)
      json_printf(&payload, ",");
    else if (i > 0)
      json_printf(&payload, ",");
    json_printf(&payload, "%Q:", c->class_name);
    if (!c->status) {
      json_printf(&payload, "%Q", NULL);
    } else {
      len = mbuf_payload.len;
      c->status(o, &payload);
      if (mbuf_payload.len == len) json_printf(&payload, "%Q", NULL);
    }
    i++;
  }
  json_printf(&payload, "}");

  LOG(LL_DEBUG, ("Status topic='%.*s' payload='%.*s'", (int) mbuf_topic.len,
                 mbuf_topic.buf, (int) mbuf_payload.len, mbuf_payload.buf));
  mgos_mqtt_pub((char *) mbuf_topic.buf, mbuf_payload.buf, mbuf_payload.len, 0,
                false);

  ret = true;
exit:
  mbuf_free(&mbuf_payload);
  mbuf_free(&mbuf_topic);
  return ret;
}

static bool mgos_homeassistant_object_send_config_mqtt(
    struct mgos_homeassistant *ha, struct mgos_homeassistant_object *o,
    struct mgos_homeassistant_object_class *c) {
  struct mbuf mbuf_topic;
  struct mbuf mbuf_topicprefix;
  struct mbuf mbuf_friendlyname;
  struct mbuf mbuf_payload;
  struct json_out payload = JSON_OUT_MBUF(&mbuf_payload);
  bool ret = false;

  mbuf_init(&mbuf_topic, 100);
  mbuf_init(&mbuf_topicprefix, 100);
  mbuf_init(&mbuf_friendlyname, 50);
  mbuf_init(&mbuf_payload, 200);

  if (!ha || !o) goto exit;

  gen_configtopic(&mbuf_topic, o, c);
  gen_topicprefix(&mbuf_topicprefix, o);
  gen_friendlyname(&mbuf_friendlyname, o, c);

  json_printf(&payload, "{\"~\":%.*Q,name:%.*Q", (int) mbuf_topicprefix.len,
              mbuf_topicprefix.buf, (int) mbuf_friendlyname.len,
              mbuf_friendlyname.buf);
  json_printf(&payload, ",unique_id:\"%s:%s:%s\"",
              mgos_sys_ro_vars_get_mac_address(), mgos_sys_ro_vars_get_arch(),
              mgos_sys_ro_vars_get_app());
  json_printf(&payload, ",avty_t:\"%s%s\"", mgos_sys_config_get_device_id(),
              "/stat");
  if (o->status) json_printf(&payload, ",stat_t:%Q", "/stat");
  if (o->cmd) json_printf(&payload, ",cmd_t:%Q", "/cmd");
  if (o->attr) json_printf(&payload, ",attr_t:%Q", "/attr");
  if (c) {
    json_printf(&payload, ",device_class:%Q,value_template:\"{{%s%s}}\"",
                c->class_name, "value_json.", c->class_name);
    if (c->json_config_additional_payload)
      json_printf(&payload, ",%s", c->json_config_additional_payload);
  }
  if (o->json_config_additional_payload)
    json_printf(&payload, ",%s", o->json_config_additional_payload);

  json_printf(&payload, ",device:{");
  json_printf(&payload, "name:%Q", mgos_sys_config_get_device_id());
  json_printf(&payload, ",identifiers:[%Q]", o->ha->node_name);
  json_printf(&payload, ",connections:[[mac,%Q]]",
              mgos_sys_ro_vars_get_mac_address());
  json_printf(&payload, ",model:%Q", mgos_sys_ro_vars_get_app());
  json_printf(&payload, ",sw_version:\"%s (%s)\"",
              mgos_sys_ro_vars_get_fw_version(), mgos_sys_ro_vars_get_fw_id());
  json_printf(&payload, ",mg_version:\"%s (%s)\"", mg_build_version,
              mg_build_id);
  json_printf(&payload, ",manufacturer:%Q", "Mongoose OS");
  json_printf(&payload, "}");

  json_printf(&payload, "}");

  LOG(LL_DEBUG, ("Config: topic='%.*s' payload='%.*s'", (int) mbuf_topic.len,
                 mbuf_topic.buf, (int) mbuf_payload.len, mbuf_payload.buf));
  mgos_mqtt_pub((char *) mbuf_topic.buf, mbuf_payload.buf, mbuf_payload.len, 0,
                true);

  ret = true;
exit:
  mbuf_free(&mbuf_topic);
  mbuf_free(&mbuf_topicprefix);
  mbuf_free(&mbuf_friendlyname);
  mbuf_free(&mbuf_payload);
  return ret;
}

bool mgos_homeassistant_object_send_config(
    struct mgos_homeassistant_object *o) {
  struct mgos_homeassistant_object_class *c;
  int done = 0, success = 0;
  bool ret = false;

  if (!o || !o->ha) goto exit;

  if (o->status || o->cmd || o->attr) {
    done++;
    if (mgos_homeassistant_object_send_config_mqtt(o->ha, o, NULL)) success++;
  }

  SLIST_FOREACH(c, &o->classes, entry) {
    done++;
    if (mgos_homeassistant_object_send_config_mqtt(c->object->ha, c->object, c))
      success++;
  }

  ret = (done == success);
  LOG(ret ? LL_DEBUG : LL_WARN,
      ("Sent %u configs (%u successfully) for object '%s'", done, success,
       o->object_name));

exit:
  return ret;
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

bool mgos_homeassistant_object_class_send_config(
    struct mgos_homeassistant_object_class *c) {
  if (!c) return false;
  return mgos_homeassistant_object_send_config(c->object);
}

bool mgos_homeassistant_object_class_send_status(
    struct mgos_homeassistant_object_class *c) {
  if (!c) return false;
  return mgos_homeassistant_object_send_status(c->object);
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

struct mgos_homeassistant *mgos_homeassistant_get_global() {
  return s_homeassistant;
}

bool mgos_homeassistant_init(void) {
  s_homeassistant = calloc(1, sizeof(struct mgos_homeassistant));
  if (!s_homeassistant) return false;

  s_homeassistant->node_name = strdup(mgos_sys_config_get_device_id());
  SLIST_INIT(&s_homeassistant->objects);

  mgos_mqtt_add_global_handler(mgos_homeassistant_mqtt_ev, s_homeassistant);
  mgos_mqtt_set_connect_fn(mgos_homeassistant_mqtt_connect, NULL);
  LOG(LL_DEBUG,
      ("Created homeassistant node '%s'", s_homeassistant->node_name));
  return true;
}
