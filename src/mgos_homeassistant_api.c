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

static const char *ha_component_str(enum mgos_homeassistant_component ha_component) {
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

static bool mgos_homeassistant_exists_objectname(struct mgos_homeassistant *ha, const char *s) {
  struct mgos_homeassistant_object *o;
  if (!ha || !s) return false;

  SLIST_FOREACH(o, &ha->objects, entry) {
    if (0 == strcasecmp(s, o->object_name)) return true;
  }

  return false;
}

bool mgos_homeassistant_object_generate_name(struct mgos_homeassistant *ha, const char *prefix, char *name, int namelen) {
  int idx = 0;
  if (!ha || !name || !prefix || namelen == 0) return false;
  for (idx = 0; idx < 256; idx++) {
    snprintf(name, namelen, "%s%d", prefix, idx);
    if (!mgos_homeassistant_exists_objectname(ha, name)) return true;
  }
  return false;
}

static bool mgos_homeassistant_exists_classname(struct mgos_homeassistant_object *o, const char *s) {
  struct mgos_homeassistant_object_class *c;
  if (!o || !s) return false;

  SLIST_FOREACH(c, &o->classes, entry) {
    if (0 == strcasecmp(s, c->class_name)) return true;
  }

  return false;
}

static struct mgos_homeassistant_object_cmd *mgos_homeassistant_object_get_cmd(struct mgos_homeassistant_object *o, const char *s) {
  struct mgos_homeassistant_object_cmd *c;
  if (!o) return NULL;

  SLIST_FOREACH(c, &o->cmds, entry) {
    if (c->cmd_name == NULL && s == NULL) return c;
    if (c->cmd_name == NULL || s == NULL) continue;
    if (0 == strcasecmp(s, c->cmd_name)) return c;
  }
  return NULL;
}

static struct mgos_homeassistant_object_attr *mgos_homeassistant_object_get_attr(struct mgos_homeassistant_object *o, const char *s) {
  struct mgos_homeassistant_object_attr *a;
  if (!o) return NULL;

  SLIST_FOREACH(a, &o->attrs, entry) {
    if (a->attr_name == NULL && s == NULL) return a;
    if (a->attr_name == NULL || s == NULL) continue;
    if (0 == strcasecmp(s, a->attr_name)) return a;
  }
  return NULL;
}

bool mgos_homeassistant_call_handlers(struct mgos_homeassistant *ha, int ev, void *ev_data) {
  struct mgos_homeassistant_handler *h;
  if (!ha) return false;

  LOG(LL_DEBUG, ("Node '%s' event: %d", ha->node_name, ev));
  SLIST_FOREACH(h, &ha->handlers, entry) {
    h->ev_handler(ha, ev, ev_data, h->user_data);
  }
  return true;
}

// Note: gen_topicprefix is not NULL terminated.
static char *gen_topicprefix(struct mbuf *m, const struct mgos_homeassistant_object *o) {
  mbuf_append(m, o->ha->node_name, strlen(o->ha->node_name));
  mbuf_append(m, "/", 1);
  mbuf_append(m, ha_component_str(o->component), strlen(ha_component_str(o->component)));
  mbuf_append(m, "/", 1);
  mbuf_append(m, o->object_name, strlen(o->object_name));
  return m->buf;
}

// Note: gen_configtopic is not NULL terminated.
static char *gen_configtopic(struct mbuf *m, const struct mgos_homeassistant_object *o, const struct mgos_homeassistant_object_class *c) {
  mbuf_append(m, mgos_sys_config_get_homeassistant_discovery_prefix(), strlen(mgos_sys_config_get_homeassistant_discovery_prefix()));
  mbuf_append(m, "/", 1);
  mbuf_append(m, ha_component_str(o->component), strlen(ha_component_str(o->component)));
  mbuf_append(m, "/", 1);
  mbuf_append(m, o->ha->node_name, strlen(o->ha->node_name));
  mbuf_append(m, "/", 1);
  mbuf_append(m, o->object_name, strlen(o->object_name));
  if (c) {
    mbuf_append(m, "_", 1);
    mbuf_append(m, c->class_name, strlen(c->class_name));
  }
  mbuf_append(m, "/config", 7);
  return m->buf;
}

// Note: gen_friendlyname is not NULL terminated.
static char *gen_friendlyname(struct mbuf *m, const struct mgos_homeassistant_object *o, const struct mgos_homeassistant_object_class *c) {
  mbuf_append(m, o->ha->node_name, strlen(o->ha->node_name));
  mbuf_append(m, "_", 1);
  mbuf_append(m, o->object_name, strlen(o->object_name));
  if (c) {
    mbuf_append(m, "_", 1);
    mbuf_append(m, c->class_name, strlen(c->class_name));
  }
  return m->buf;
}

static bool endswith(const char *str, size_t str_len, const char *suffix) {
  if (!str || !suffix) return false;
  size_t suffix_len = strlen(suffix);
  if (suffix_len > str_len) return false;
  return strncmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

static void mgos_homeassistant_mqtt_cb(struct mg_connection *nc, const char *topic, int topic_len, const char *msg, int msg_len, void *ud) {
  struct mgos_homeassistant_object *o = (struct mgos_homeassistant_object *) ud;
  if (!o) return;

  LOG(LL_DEBUG, ("Received MQTT for object '%s': topic='%.*s' payload='%.*s'", o->object_name, topic_len, topic, msg_len, msg));
  if (endswith(topic, (size_t) topic_len, "/stat")) {
    mgos_homeassistant_object_send_status(o);
    return;
  }

  struct mbuf mbuf_topic;
  mbuf_init(&mbuf_topic, 100);
  gen_topicprefix(&mbuf_topic, o);

  mbuf_append(&mbuf_topic, "/cmd", 4);
  if ((topic_len >= (int) mbuf_topic.len) && (0 == strncasecmp(topic, mbuf_topic.buf, mbuf_topic.len))) {
    int cmd_len = topic_len - mbuf_topic.len;
    const char *cmdp = topic + mbuf_topic.len;
    mbuf_free(&mbuf_topic);

    if (!cmdp || (cmd_len == 0)) {
      LOG(LL_DEBUG, ("Issuing command '(default)' on object '%s'", o->object_name));
      mgos_homeassistant_object_cmd(o, NULL, msg, msg_len);
      return;
    }
    if (*cmdp != '/') {
      LOG(LL_ERROR, ("Malformed command path, expecting '/'"));
      return;
    }
    cmd_len--;
    cmdp++;  // chop of '/'
    if (cmd_len == 0) {
      LOG(LL_DEBUG, ("Issuing command '(default)' on object '%s'", o->object_name));
      mgos_homeassistant_object_cmd(o, NULL, msg, msg_len);
      return;
    }
    LOG(LL_DEBUG, ("Issuing command '%.*s' on object '%s'", (int) cmd_len, cmdp, o->object_name));

    // mgos_homeassistant_object_cmd() expects NULL terminated string.
    char *cmd = malloc(cmd_len + 1);
    memcpy(cmd, cmdp, cmd_len);
    cmd[cmd_len] = 0;
    mgos_homeassistant_object_cmd(o, cmd, msg, msg_len);
    free(cmd);
    return;
  }

  mbuf_topic.len -= 4;
  mbuf_append(&mbuf_topic, "/attr", 5);
  if ((topic_len >= (int) mbuf_topic.len) && (0 == strncasecmp(topic, mbuf_topic.buf, mbuf_topic.len))) {
    int attr_len = topic_len - mbuf_topic.len;
    const char *attrp = topic + mbuf_topic.len;
    mbuf_free(&mbuf_topic);

    if (!attrp || (attr_len == 0)) {
      LOG(LL_DEBUG, ("Issuing attribute '(default)' on object '%s'", o->object_name));
      mgos_homeassistant_object_attr(o, NULL, msg, msg_len);
      return;
    }
    if (*attrp != '/') {
      LOG(LL_ERROR, ("Malformed attribute path, expecting '/'"));
      return;
    }
    attr_len--;
    attrp++;  // chop of '/'
    if (attr_len == 0) {
      LOG(LL_DEBUG, ("Issuing attribute '(default)' on object '%s'", o->object_name));
      mgos_homeassistant_object_attr(o, NULL, msg, msg_len);
      return;
    }
    LOG(LL_DEBUG, ("Issuing attribute '%.*s' on object '%s'", (int) attr_len, attrp, o->object_name));

    // mgos_homeassistant_object_attr() expects NULL terminated string.
    char *attr = malloc(attr_len + 1);
    memcpy(attr, attrp, attr_len);
    attr[attr_len] = 0;
    mgos_homeassistant_object_attr(o, attr, msg, msg_len);
    free(attr);
    return;
  }
  mbuf_free(&mbuf_topic);
  (void) nc;
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
  LOG((done == success) ? LL_DEBUG : LL_WARN, ("Sent %u configs (%u successfully) for node '%s'", done, success, ha->node_name));
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
  mgos_homeassistant_call_handlers(ha, MGOS_HOMEASSISTANT_EV_CLEAR, NULL);

  return true;
}

struct mgos_homeassistant_object *mgos_homeassistant_object_add(struct mgos_homeassistant *ha, const char *object_name,
                                                                enum mgos_homeassistant_component ha_component,
                                                                const char *json_config_additional_payload, ha_status_cb status_cb, void *user_data) {
  struct mgos_homeassistant_object *o = calloc(1, sizeof(*o));

  if (!o || !ha || !object_name) return NULL;
  if (!mgos_homeassistant_isvalid_name(object_name)) {
    LOG(LL_ERROR, ("Invalid object name '%s'", object_name));
    free(o);
    return NULL;
  }
  if (mgos_homeassistant_exists_objectname(ha, object_name)) {
    LOG(LL_ERROR, ("Object name '%s' already exists in node '%s'", object_name, ha->node_name));
    free(o);
    return NULL;
  }

  o->ha = ha;
  o->component = ha_component;
  o->object_name = strdup(object_name);
  if (json_config_additional_payload) o->json_config_additional_payload = strdup(json_config_additional_payload);
  o->user_data = user_data;
  o->status_cb = status_cb;
  mbuf_init(&o->status, 20);
  SLIST_INIT(&o->classes);
  SLIST_INIT(&o->cmds);
  SLIST_INIT(&o->attrs);
  SLIST_INSERT_HEAD(&ha->objects, o, entry);

  // Add a wildcard MQTT subscription for this object.
  struct mbuf mbuf_topic;
  mbuf_init(&mbuf_topic, 100);
  gen_topicprefix(&mbuf_topic, o);
  mbuf_append(&mbuf_topic, "/#\0", 3);
  if (!mgos_mqtt_global_is_connected()) {
    LOG(LL_DEBUG, ("MQTT not connected, skipping subscription for %s", o->object_name));
  } else {
    mgos_mqtt_sub(mbuf_topic.buf, mgos_homeassistant_mqtt_cb, o);
  }
  mbuf_free(&mbuf_topic);

  mgos_homeassistant_call_handlers(ha, MGOS_HOMEASSISTANT_EV_OBJECT_ADD, o);
  LOG(LL_DEBUG, ("Created object '%s' on node '%s'", o->object_name, o->ha->node_name));
  return o;
}

bool mgos_homeassistant_object_cmd(struct mgos_homeassistant_object *o, const char *name, const char *payload, const int payload_len) {
  struct mgos_homeassistant_object_cmd *c;
  if (!o) return false;
  if (!(c = mgos_homeassistant_object_get_cmd(o, name))) {
    LOG(LL_WARN, ("No command '%s' on object '%s'", name ? name : "(default)", o->object_name));
    return false;
  }
  if (!c->cmd_cb) {
    LOG(LL_WARN, ("No callback function on command '%s' of object '%s'", name ? name : "(default)", o->object_name));
    return false;
  }
  LOG(LL_DEBUG, ("Calling command '%s' of object '%s'", name ? name : "(default)", o->object_name));
  c->cmd_cb(o, payload, payload_len);
  mgos_homeassistant_call_handlers(o->ha, MGOS_HOMEASSISTANT_EV_OBJECT_CMD, c);
  return true;
}

bool mgos_homeassistant_object_attr(struct mgos_homeassistant_object *o, const char *name, const char *payload, const int payload_len) {
  struct mgos_homeassistant_object_attr *a;
  if (!o) return false;
  if (!(a = mgos_homeassistant_object_get_attr(o, name))) {
    LOG(LL_WARN, ("No attribute '%s' on object '%s'", name ? name : "(default)", o->object_name));
    return false;
  }
  if (!a->attr_cb) {
    LOG(LL_WARN, ("No callback function on attribute '%s' of object '%s'", name ? name : "(default)", o->object_name));
    return false;
  }
  LOG(LL_DEBUG, ("Calling attribute '%s' of object '%s'", name ? name : "(default)", o->object_name));
  a->attr_cb(o, payload, payload_len);
  mgos_homeassistant_call_handlers(o->ha, MGOS_HOMEASSISTANT_EV_OBJECT_ATTR, a);
  return true;
}

bool mgos_homeassistant_object_remove_cmd(struct mgos_homeassistant_object_cmd **c) {
  if (!(*c)) return false;

  LOG(LL_DEBUG, ("Removing command '%s' from object '%s'", (*c)->cmd_name ? (*c)->cmd_name : "(default)", (*c)->object->object_name));
  if ((*c)->cmd_name) free((*c)->cmd_name);
  free(*c);
  *c = NULL;
  return true;
}

bool mgos_homeassistant_object_remove_attr(struct mgos_homeassistant_object_attr **a) {
  if (!(*a)) return false;

  LOG(LL_DEBUG, ("Removing attribute '%s' from object '%s'", (*a)->attr_name ? (*a)->attr_name : "(default)", (*a)->object->object_name));
  if ((*a)->attr_name) free((*a)->attr_name);
  free(*a);
  *a = NULL;
  return true;
}

bool mgos_homeassistant_object_add_cmd_cb(struct mgos_homeassistant_object *o, const char *name, ha_cmd_cb cmd_cb) {
  struct mgos_homeassistant_object_cmd *c;
  if (!o) return false;

  if (!(c = mgos_homeassistant_object_get_cmd(o, name))) {
    if (!(c = calloc(1, sizeof(*c)))) return false;
    LOG(LL_DEBUG, ("Creating command '%s' on object '%s'", name ? name : "(default)", o->object_name));
    if (name) c->cmd_name = strdup(name);
    SLIST_INSERT_HEAD(&o->cmds, c, entry);
  } else {
    LOG(LL_DEBUG, ("Replacing command '%s' on object '%s'", name ? name : "(default)", o->object_name));
  }
  c->cmd_cb = cmd_cb;
  c->object = o;
  return true;
}

bool mgos_homeassistant_object_add_attr_cb(struct mgos_homeassistant_object *o, const char *name, ha_attr_cb attr_cb) {
  struct mgos_homeassistant_object_attr *a;
  if (!o) return false;

  if (!(a = mgos_homeassistant_object_get_attr(o, name))) {
    if (!(a = calloc(1, sizeof(*a)))) return false;
    LOG(LL_DEBUG, ("Creating attribute '%s' on object '%s'", name ? name : "(default)", o->object_name));
    if (name) a->attr_name = strdup(name);
    SLIST_INSERT_HEAD(&o->attrs, a, entry);
  } else {
    LOG(LL_DEBUG, ("Replacing attribute '%s' on object '%s'", name ? name : "(default)", o->object_name));
  }
  a->attr_cb = attr_cb;
  a->object = o;
  return true;
}

struct mgos_homeassistant_object *mgos_homeassistant_object_get(struct mgos_homeassistant *ha, const char *suffix) {
  struct mgos_homeassistant_object *o = NULL;
  if (!ha || !suffix) return NULL;

  SLIST_FOREACH(o, &ha->objects, entry) {
    if (endswith(o->object_name, strlen(o->object_name), suffix)) return o;
  }
  return NULL;
}

bool mgos_homeassistant_object_get_status(struct mgos_homeassistant_object *o) {
  struct mgos_homeassistant_object_class *c = NULL;
  int i;
  size_t len;

  if (!o) return false;
  struct json_out payload = JSON_OUT_MBUF(&o->status);
  o->status.len = 0;

  json_printf(&payload, "{");
  len = o->status.len;
  if (o->status_cb) o->status_cb(o, &payload);

  i = 0;
  SLIST_FOREACH(c, &o->classes, entry) {
    if (i == 0 && len != o->status.len)
      json_printf(&payload, ",");
    else if (i > 0)
      json_printf(&payload, ",");
    json_printf(&payload, "%Q:", c->class_name);
    if (!c->status_cb) {
      json_printf(&payload, "%Q", NULL);
    } else {
      len = o->status.len;
      c->status_cb(o, &payload);
      if (o->status.len == len) json_printf(&payload, "%Q", NULL);
    }
    i++;
  }
  json_printf(&payload, "}");
  return true;
}

bool mgos_homeassistant_object_send_status(struct mgos_homeassistant_object *o) {
  struct mbuf mbuf_topic;

  if (!o) return false;
  if (!o->config_sent) mgos_homeassistant_object_send_config(o);

  mbuf_init(&mbuf_topic, 100);
  gen_topicprefix(&mbuf_topic, o);
  mbuf_append(&mbuf_topic, "\0", 1);

  mgos_homeassistant_object_get_status(o);
  mgos_homeassistant_call_handlers(o->ha, MGOS_HOMEASSISTANT_EV_OBJECT_STATUS, o);

  LOG(LL_DEBUG, ("Status topic='%.*s' payload='%.*s'", (int) mbuf_topic.len, mbuf_topic.buf, (int) o->status.len, o->status.buf));
  if (!mgos_mqtt_global_is_connected()) {
    LOG(LL_DEBUG, ("MQTT not connected, skipping status for %s", o->object_name));
  } else {
    mgos_mqtt_pub((char *) mbuf_topic.buf, o->status.buf, o->status.len, 0, false);
  }

  if (mbuf_topic.size > 0) mbuf_free(&mbuf_topic);
  return true;
}

bool mgos_homeassistant_object_log(struct mgos_homeassistant_object *o, const char *json_fmt, ...) {
  va_list ap;
  struct mbuf mbuf_topic;

  if (!o) return false;
  mbuf_init(&mbuf_topic, 100);
  gen_topicprefix(&mbuf_topic, o);
  mbuf_append(&mbuf_topic, "/log\0", 5);

  va_start(ap, json_fmt);
  if (!mgos_mqtt_global_is_connected()) {
    LOG(LL_DEBUG, ("MQTT not connected, skipping log for %s", o->object_name));
  } else {
    mgos_mqtt_pubv(mbuf_topic.buf, 0, false, json_fmt, ap);
  }
  va_end(ap);
  mbuf_free(&mbuf_topic);
  return true;
}

static bool mgos_homeassistant_object_send_config_mqtt(struct mgos_homeassistant *ha, struct mgos_homeassistant_object *o,
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

  json_printf(&payload, "{\"~\":%.*Q,name:%.*Q", (int) mbuf_topicprefix.len, mbuf_topicprefix.buf, (int) mbuf_friendlyname.len,
              mbuf_friendlyname.buf);
  json_printf(&payload, ",unique_id:\"%s:%.*s\"", mgos_sys_ro_vars_get_mac_address(), (int) mbuf_friendlyname.len, mbuf_friendlyname.buf);
  json_printf(&payload, ",avty_t:\"%s\"", mgos_sys_config_get_device_id());
  json_printf(&payload, ",stat_t:%Q", "~");
  if (mgos_homeassistant_object_get_cmd(o, NULL)) json_printf(&payload, ",cmd_t:%Q", "~/cmd");
  if (mgos_homeassistant_object_get_attr(o, NULL)) json_printf(&payload, ",attr_t:%Q", "~/attr");
  if (c) {
    json_printf(&payload, ",device_class:%Q,value_template:\"{{%s%s}}\"", c->class_name, "value_json.", c->class_name);
    if (c->json_config_additional_payload) json_printf(&payload, ",%s", c->json_config_additional_payload);
  }
  if (o->json_config_additional_payload) json_printf(&payload, ",%s", o->json_config_additional_payload);

  json_printf(&payload, ",device:{");
  json_printf(&payload, "name:%Q", mgos_sys_config_get_device_id());
  json_printf(&payload, ",identifiers:[%Q]", o->ha->node_name);
  json_printf(&payload, ",connections:[[mac,%Q]]", mgos_sys_ro_vars_get_mac_address());
  json_printf(&payload, ",model:%Q", mgos_sys_ro_vars_get_app());
  json_printf(&payload, ",sw_version:\"%s (%s)\"", mgos_sys_ro_vars_get_fw_version(), mgos_sys_ro_vars_get_fw_id());
  json_printf(&payload, ",manufacturer:%Q", "Mongoose OS");
  json_printf(&payload, "}");

  json_printf(&payload, "}");

  LOG(LL_DEBUG, ("Config: topic='%.*s' payload='%.*s'", (int) mbuf_topic.len, mbuf_topic.buf, (int) mbuf_payload.len, mbuf_payload.buf));
  mbuf_append(&mbuf_topic, "\0", 1);
  if (!mgos_mqtt_global_is_connected()) {
    LOG(LL_DEBUG, ("MQTT not connected, skipping config for %s", o->object_name));
  } else {
    mgos_mqtt_pub((char *) mbuf_topic.buf, mbuf_payload.buf, mbuf_payload.len, 0, true);
  }

  ret = true;
exit:
  mbuf_free(&mbuf_topic);
  mbuf_free(&mbuf_topicprefix);
  mbuf_free(&mbuf_friendlyname);
  mbuf_free(&mbuf_payload);
  return ret;
}

bool mgos_homeassistant_object_send_config(struct mgos_homeassistant_object *o) {
  struct mgos_homeassistant_object_class *c;
  int done = 0, success = 0;
  bool ret = false;

  if (!o || !o->ha) goto exit;
  if (o->config_sent) goto exit;

  if (o->status_cb || mgos_homeassistant_object_get_cmd(o, NULL) || mgos_homeassistant_object_get_attr(o, NULL)) {
    done++;
    if (mgos_homeassistant_object_send_config_mqtt(o->ha, o, NULL)) success++;
  }

  SLIST_FOREACH(c, &o->classes, entry) {
    done++;
    if (mgos_homeassistant_object_send_config_mqtt(c->object->ha, c->object, c)) success++;
  }

  ret = (done == success);
  LOG(ret ? LL_DEBUG : LL_WARN, ("Sent %u configs (%u successfully) for object '%s'", done, success, o->object_name));
  if (ret) o->config_sent = true;
exit:
  return ret;
}

bool mgos_homeassistant_object_remove(struct mgos_homeassistant_object **o) {
  if (!(*o) || !(*o)->ha) return false;

  LOG(LL_DEBUG, ("Removing object '%s' from node '%s'", (*o)->object_name, (*o)->ha->node_name));
  mgos_homeassistant_call_handlers((*o)->ha, MGOS_HOMEASSISTANT_EV_OBJECT_REMOVE, *o);
  if ((*o)->pre_remove_cb) (*o)->pre_remove_cb(*o);
  if ((*o)->user_data) LOG(LL_WARN, ("Object '%s' still has user_data, pre_remove_cb() should clean that up!", (*o)->object_name));

  struct mbuf mbuf_topic;
  mbuf_init(&mbuf_topic, 100);
  gen_topicprefix(&mbuf_topic, *o);
  mbuf_append(&mbuf_topic, "/#\0", 3);
  if (!mgos_mqtt_global_is_connected()) {
    LOG(LL_DEBUG, ("MQTT not connected, skipping unsubscribe for %s", (*o)->object_name));
  } else {
    mgos_mqtt_unsub(mbuf_topic.buf);
  }
  mbuf_free(&mbuf_topic);

  while (!SLIST_EMPTY(&(*o)->classes)) {
    struct mgos_homeassistant_object_class *c;
    c = SLIST_FIRST(&(*o)->classes);
    mgos_homeassistant_object_class_remove(&c);
  }

  while (!SLIST_EMPTY(&(*o)->cmds)) {
    struct mgos_homeassistant_object_cmd *c;
    c = SLIST_FIRST(&(*o)->cmds);
    SLIST_REMOVE(&(*o)->cmds, c, mgos_homeassistant_object_cmd, entry);
    mgos_homeassistant_object_remove_cmd(&c);
  }

  while (!SLIST_EMPTY(&(*o)->attrs)) {
    struct mgos_homeassistant_object_attr *a;
    a = SLIST_FIRST(&(*o)->attrs);
    SLIST_REMOVE(&(*o)->attrs, a, mgos_homeassistant_object_attr, entry);
    mgos_homeassistant_object_remove_attr(&a);
  }

  if ((*o)->object_name) free((*o)->object_name);
  if ((*o)->json_config_additional_payload) free((*o)->json_config_additional_payload);
  if ((*o)->status.size > 0) mbuf_free(&(*o)->status);

  SLIST_REMOVE(&(*o)->ha->objects, (*o), mgos_homeassistant_object, entry);

  free(*o);
  *o = NULL;
  return true;
}

struct mgos_homeassistant_object_class *mgos_homeassistant_object_class_add(struct mgos_homeassistant_object *o, const char *class_name,
                                                                            const char *json_config_additional_payload, ha_status_cb status_cb) {
  struct mgos_homeassistant_object_class *c = calloc(1, sizeof(*c));

  if (!c || !o || !class_name) return NULL;
  if (!mgos_homeassistant_isvalid_name(class_name)) {
    LOG(LL_ERROR, ("Invalid class name '%s'", class_name));
    free(c);
    return NULL;
  }
  if (mgos_homeassistant_exists_classname(o, class_name)) {
    LOG(LL_ERROR, ("Class name '%s' already exists in object '%s'", class_name, o->object_name));
    free(c);
    return NULL;
  }

  c->object = o;
  c->class_name = strdup(class_name);
  if (json_config_additional_payload) c->json_config_additional_payload = strdup(json_config_additional_payload);
  c->status_cb = status_cb;
  SLIST_INSERT_HEAD(&o->classes, c, entry);

  // Force a config update to be sent upon next status
  o->config_sent = false;
  mgos_homeassistant_call_handlers(o->ha, MGOS_HOMEASSISTANT_EV_CLASS_ADD, c);
  LOG(LL_DEBUG, ("Created class '%s' on object '%s'", c->class_name, c->object->object_name));
  return c;
}

struct mgos_homeassistant_object_class *mgos_homeassistant_object_class_get(struct mgos_homeassistant_object *o, const char *suffix) {
  struct mgos_homeassistant_object_class *c = NULL;
  if (!o || !suffix) return NULL;

  SLIST_FOREACH(c, &o->classes, entry) {
    if (endswith(c->class_name, strlen(c->class_name), suffix)) return c;
  }
  return NULL;
}

bool mgos_homeassistant_object_class_send_config(struct mgos_homeassistant_object_class *c) {
  if (!c) return false;
  return mgos_homeassistant_object_send_config(c->object);
}

bool mgos_homeassistant_object_class_send_status(struct mgos_homeassistant_object_class *c) {
  if (!c) return false;
  return mgos_homeassistant_object_send_status(c->object);
}

bool mgos_homeassistant_object_class_remove(struct mgos_homeassistant_object_class **c) {
  if (!(*c) || !(*c)->object) return false;

  LOG(LL_DEBUG, ("Removing class '%s' from object '%s'", (*c)->class_name, (*c)->object->object_name));
  mgos_homeassistant_call_handlers((*c)->object->ha, MGOS_HOMEASSISTANT_EV_CLASS_REMOVE, c);

  if ((*c)->class_name) free((*c)->class_name);
  if ((*c)->json_config_additional_payload) free((*c)->json_config_additional_payload);

  SLIST_REMOVE(&(*c)->object->classes, (*c), mgos_homeassistant_object_class, entry);

  free(*c);
  *c = NULL;
  return true;
}

bool mgos_homeassistant_add_handler(struct mgos_homeassistant *ha, ha_ev_handler ev_handler, void *user_data) {
  struct mgos_homeassistant_handler *h;

  if (!ha || !ev_handler) return false;
  if (!(h = calloc(1, sizeof(*h)))) return false;
  h->ev_handler = ev_handler;
  h->user_data = user_data;

  SLIST_INSERT_HEAD(&ha->handlers, h, entry);
  mgos_homeassistant_call_handlers(ha, MGOS_HOMEASSISTANT_EV_ADD_HANDLER, NULL);
  return true;
}
