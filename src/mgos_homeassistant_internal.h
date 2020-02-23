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
#include "common/queue.h"
#include "frozen/frozen.h"
#include "mgos.h"
#include "mgos_config.h"
#include "mgos_homeassistant.h"
#include "mgos_mqtt.h"

struct mgos_homeassistant_object;
struct mgos_homeassistant_object_class;

struct mgos_homeassistant {
  char *name;
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
