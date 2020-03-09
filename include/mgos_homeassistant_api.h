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

#include "common/queue.h"
#include "frozen/frozen.h"

struct mgos_homeassistant;
struct mgos_homeassistant_object;
struct mgos_homeassistant_object_class;

enum mgos_homeassistant_component {
  COMPONENT_NONE = 0,
  COMPONENT_ALARM_CONTROL_PANEL,
  COMPONENT_BINARY_SENSOR,
  COMPONENT_CAMERA,
  COMPONENT_CLIMATE,
  COMPONENT_COVER,
  COMPONENT_FAN,
  COMPONENT_LIGHT,
  COMPONENT_LOCK,
  COMPONENT_SENSOR,
  COMPONENT_SWITCH,
  COMPONENT_VACUUM
};

typedef void (*ha_status_cb)(struct mgos_homeassistant_object *o,
                             struct json_out *json);
typedef void (*ha_cmd_cb)(struct mgos_homeassistant_object *o,
                          const char *payload, const int payload_len);
typedef void (*ha_attr_cb)(struct mgos_homeassistant_object *o,
                           const char *payload, const int payload_len);

struct mgos_homeassistant {
  char *node_name;
  SLIST_HEAD(objects, mgos_homeassistant_object) objects;
};

struct mgos_homeassistant_object {
  struct mgos_homeassistant *ha;
  enum mgos_homeassistant_component component;
  char *object_name;

  bool config_sent;
  char *json_config_additional_payload;

  ha_status_cb status;
  ha_cmd_cb cmd;
  ha_attr_cb attr;
  void *user_data;

  SLIST_HEAD(classes, mgos_homeassistant_object_class) classes;
  SLIST_ENTRY(mgos_homeassistant_object) entry;
};

struct mgos_homeassistant_object_class {
  struct mgos_homeassistant_object *object;
  char *class_name;
  char *json_config_additional_payload;

  ha_status_cb status;

  SLIST_ENTRY(mgos_homeassistant_object_class) entry;
};

bool mgos_homeassistant_send_config(struct mgos_homeassistant *ha);
bool mgos_homeassistant_send_status(struct mgos_homeassistant *ha);

struct mgos_homeassistant_object *mgos_homeassistant_object_add(
    struct mgos_homeassistant *ha, const char *object_name,
    enum mgos_homeassistant_component ha_component,
    const char *json_config_additional_payload, ha_status_cb status,
    void *user_data);
struct mgos_homeassistant_object *mgos_homeassistant_object_get(
    struct mgos_homeassistant *ha, const char *suffix);
bool mgos_homeassistant_object_set_cmd_cb(struct mgos_homeassistant_object *o,
                                          ha_cmd_cb cmd);
bool mgos_homeassistant_object_set_attr_cb(struct mgos_homeassistant_object *o,
                                           ha_attr_cb attr);
bool mgos_homeassistant_object_send_status(struct mgos_homeassistant_object *o);
bool mgos_homeassistant_object_send_config(struct mgos_homeassistant_object *o);
bool mgos_homeassistant_object_remove(struct mgos_homeassistant_object **o);

struct mgos_homeassistant_object_class *mgos_homeassistant_object_class_add(
    struct mgos_homeassistant_object *o, const char *class_name,
    const char *json_config_additional_payload, ha_status_cb cb);
struct mgos_homeassistant_object_class *mgos_homeassistant_object_class_get(
    struct mgos_homeassistant_object *o, const char *suffix);
bool mgos_homeassistant_object_class_send_status(
    struct mgos_homeassistant_object_class *c);
bool mgos_homeassistant_object_class_send_config(
    struct mgos_homeassistant_object_class *c);
bool mgos_homeassistant_object_class_remove(
    struct mgos_homeassistant_object_class **c);
