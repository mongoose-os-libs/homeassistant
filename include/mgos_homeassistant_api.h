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

#include <stdbool.h>

#include "common/mbuf.h"
#include "common/queue.h"
#include "frozen/frozen.h"

struct mgos_homeassistant;
struct mgos_homeassistant_handler;
struct mgos_homeassistant_object;
struct mgos_homeassistant_object_class;

enum mgos_homeassistant_component {
  COMPONENT_NONE = 0,
  COMPONENT_ALARM_CONTROL_PANEL,
  COMPONENT_BINARY_SENSOR,
  COMPONENT_CAMERA,
  COMPONENT_CLIMATE,
  COMPONENT_COVER,
  COMPONENT_DEVICE_TRIGGER,
  COMPONENT_FAN,
  COMPONENT_LIGHT,
  COMPONENT_LOCK,
  COMPONENT_SENSOR,
  COMPONENT_SWITCH,
  COMPONENT_VACUUM
};

// Events for ha->ev_handler
#define MGOS_HOMEASSISTANT_EV_ADD_HANDLER 10     // ev_data: NULL
#define MGOS_HOMEASSISTANT_EV_CLEAR 11           // ev_data: NULL
#define MGOS_HOMEASSISTANT_EV_OBJECT_ADD 20      // ev_data: struct mgos_homeassistant_object *
#define MGOS_HOMEASSISTANT_EV_OBJECT_STATUS 21   // ev_data: struct mgos_homeassistant_object *
#define MGOS_HOMEASSISTANT_EV_OBJECT_CMD 22      // ev_data: struct mgos_homeassistant_object_cmd *
#define MGOS_HOMEASSISTANT_EV_OBJECT_ATTR 23     // ev_data: struct mgos_homeassistant_object_attr *
#define MGOS_HOMEASSISTANT_EV_OBJECT_REMOVE 24   // ev_data: struct mgos_homeassistant_object *
#define MGOS_HOMEASSISTANT_EV_CLASS_ADD 30       // ev_data: struct mgos_homeassistant_object_class *
#define MGOS_HOMEASSISTANT_EV_CLASS_REMOVE 31    // ev_data: struct mgos_homeassistant_object_class *
#define MGOS_HOMEASSISTANT_EV_AUTOMATION_RUN 40  // ev_data: struct mgos_homeassistant_automation *

typedef void (*ha_object_cb)(struct mgos_homeassistant_object *o);
typedef void (*ha_status_cb)(struct mgos_homeassistant_object *o, struct json_out *json);
typedef void (*ha_cmd_cb)(struct mgos_homeassistant_object *o, const char *payload, const int payload_len);
typedef void (*ha_attr_cb)(struct mgos_homeassistant_object *o, const char *payload, const int payload_len);
typedef void (*ha_ev_handler)(struct mgos_homeassistant *ha, const int ev, const void *ev_data, void *user_data);

struct mgos_homeassistant {
  char *node_name;

  SLIST_HEAD(objects, mgos_homeassistant_object) objects;
  SLIST_HEAD(automations, mgos_homeassistant_automation) automations;
  SLIST_HEAD(handlers, mgos_homeassistant_handler) handlers;
};

struct mgos_homeassistant_object_cmd {
  char *cmd_name;
  ha_cmd_cb cmd_cb;
  struct mgos_homeassistant_object *object;

  SLIST_ENTRY(mgos_homeassistant_object_cmd) entry;
};

struct mgos_homeassistant_object_attr {
  char *attr_name;
  ha_attr_cb attr_cb;
  struct mgos_homeassistant_object *object;

  SLIST_ENTRY(mgos_homeassistant_object_attr) entry;
};

struct mgos_homeassistant_object {
  struct mgos_homeassistant *ha;
  enum mgos_homeassistant_component component;
  char *object_name;

  bool config_sent;
  char *json_config_additional_payload;

  ha_status_cb status_cb;
  ha_object_cb pre_remove_cb;
  SLIST_HEAD(cmds, mgos_homeassistant_object_cmd) cmds;
  SLIST_HEAD(attrs, mgos_homeassistant_object_attr) attrs;
  void *user_data;

  struct mbuf status;

  SLIST_HEAD(classes, mgos_homeassistant_object_class) classes;
  SLIST_ENTRY(mgos_homeassistant_object) entry;
};

struct mgos_homeassistant_object_class {
  struct mgos_homeassistant_object *object;
  enum mgos_homeassistant_component component;
  char *class_name;
  char *json_config_additional_payload;

  ha_status_cb status_cb;

  SLIST_ENTRY(mgos_homeassistant_object_class) entry;
};

struct mgos_homeassistant_handler {
  ha_ev_handler ev_handler;
  void *user_data;

  SLIST_ENTRY(mgos_homeassistant_handler) entry;
};

#ifdef __cplusplus
extern "C" {
#endif

bool mgos_homeassistant_send_config(struct mgos_homeassistant *ha, bool force);
bool mgos_homeassistant_send_status(struct mgos_homeassistant *ha);
bool mgos_homeassistant_add_handler(struct mgos_homeassistant *ha, ha_ev_handler ev_handler, void *user_data);
bool mgos_homeassistant_call_handlers(struct mgos_homeassistant *ha, int ev, void *ev_data);

struct mgos_homeassistant_object *mgos_homeassistant_object_add(struct mgos_homeassistant *ha, const char *object_name,
                                                                enum mgos_homeassistant_component ha_component,
                                                                const char *json_config_additional_payload, ha_status_cb status, void *user_data);
struct mgos_homeassistant_object *mgos_homeassistant_object_get(struct mgos_homeassistant *ha, const char *suffix);
bool mgos_homeassistant_object_generate_name(struct mgos_homeassistant *ha, const char *prefix, char *name, int namelen);
bool mgos_homeassistant_object_cmd(struct mgos_homeassistant_object *o, const char *name, const char *payload, const int payload_len);
bool mgos_homeassistant_object_attr(struct mgos_homeassistant_object *o, const char *name, const char *payload, const int payload_len);
bool mgos_homeassistant_object_log(struct mgos_homeassistant_object *o, const char *json_fmt, ...);
bool mgos_homeassistant_object_add_cmd_cb(struct mgos_homeassistant_object *o, const char *name, ha_cmd_cb cmd);
bool mgos_homeassistant_object_add_attr_cb(struct mgos_homeassistant_object *o, const char *name, ha_attr_cb attr);
bool mgos_homeassistant_object_get_status(struct mgos_homeassistant_object *o);
bool mgos_homeassistant_object_send_status(struct mgos_homeassistant_object *o);
bool mgos_homeassistant_object_send_config(struct mgos_homeassistant_object *o);
bool mgos_homeassistant_object_remove(struct mgos_homeassistant_object **o);

struct mgos_homeassistant_object_class *mgos_homeassistant_object_class_add(struct mgos_homeassistant_object *o, const char *class_name,
                                                                            const char *json_config_additional_payload, ha_status_cb cb);
struct mgos_homeassistant_object_class *mgos_homeassistant_object_class_get(struct mgos_homeassistant_object *o, const char *suffix);
bool mgos_homeassistant_object_class_send_status(struct mgos_homeassistant_object_class *c);
bool mgos_homeassistant_object_class_send_config(struct mgos_homeassistant_object_class *c);
bool mgos_homeassistant_object_class_remove(struct mgos_homeassistant_object_class **c);

#ifdef __cplusplus
}
#endif
