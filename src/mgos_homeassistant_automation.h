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

struct mgos_homeassistant_automation;
struct mgos_homeassistant_automation_data;

enum mgos_homeassistant_automation_datatype {
  TRIGGER_NONE = 0,
  TRIGGER_STATUS = 1,

  CONDITION_NONE = 100,
  CONDITION_STATUS = 101,

  ACTION_NONE = 200,
  ACTION_MQTT = 201,
  ACTION_COMMAND = 202
};

typedef bool (*mgos_homeassistant_automation_cb)(
    enum mgos_homeassistant_automation_datatype type, void *data);

struct mgos_homeassistant_automation {
  SLIST_HEAD(triggers, mgos_homeassistant_automation_data) triggers;
  SLIST_HEAD(conditions, mgos_homeassistant_automation_data) conditions;
  SLIST_HEAD(actions, mgos_homeassistant_automation_data) actions;

  mgos_homeassistant_automation_cb trigger_cb;
  mgos_homeassistant_automation_cb condition_cb;
  mgos_homeassistant_automation_cb action_cb;

  void *user_data;
};

struct mgos_homeassistant_automation_data {
  enum mgos_homeassistant_automation_datatype type;
  void *data;

  SLIST_ENTRY(mgos_homeassistant_automation_data) entry;
};

struct mgos_homeassistant_automation_data_status {
  char *object;
  char *status;
};

struct mgos_homeassistant_automation_data_action_mqtt {
  char *topic;
  char *payload;
};

struct mgos_homeassistant_automation_data_action_command {
  char *object;
  char *payload;
};

struct mgos_homeassistant_automation *mgos_homeassistant_automation_create(
    void *user_data);
bool mgos_homeassistant_automation_set_trigger_cb(
    struct mgos_homeassistant_automation *e,
    mgos_homeassistant_automation_cb trigger_cb);
bool mgos_homeassistant_automation_set_condition_cb(
    struct mgos_homeassistant_automation *e,
    mgos_homeassistant_automation_cb condition_cb);
bool mgos_homeassistant_automation_set_action_cb(
    struct mgos_homeassistant_automation *e,
    mgos_homeassistant_automation_cb action_cb);

bool mgos_homeassistant_automation_add_trigger(
    struct mgos_homeassistant_automation *e,
    enum mgos_homeassistant_automation_datatype type, void *data);
bool mgos_homeassistant_automation_add_condition(
    struct mgos_homeassistant_automation *e,
    enum mgos_homeassistant_automation_datatype type, void *data);
bool mgos_homeassistant_automation_add_action(
    struct mgos_homeassistant_automation *e,
    enum mgos_homeassistant_automation_datatype type, void *data);

struct mgos_homeassistant_automation_data *
mgos_homeassistant_automation_data_create(
    enum mgos_homeassistant_automation_datatype type, void *data);
bool mgos_homeassistant_automation_data_destroy(
    struct mgos_homeassistant_automation_data **d);

bool mgos_homeassistant_automation_fromfile(
    struct mgos_homeassistant_automation *e, const char *filename);
bool mgos_homeassistant_automation_fromjson(
    struct mgos_homeassistant_automation *e, const char *json);

// Note: automation_type is the automation that should trigger (MUST be
// TRIGGER_*) and its accompanying data. Usually, this will be a struct
// mgos_homeassistant_automation_data_status *; although later trigger
// automations such as TRIGGER_CRON Typical call site:
//
// mgos_homeassistant_automation_data_status s;
// s.object = "button";
// s.status = "ON";
// mgos_homeassistant_automation_run (e, TRIGGER_STATUS, &s);
//
bool mgos_homeassistant_automation_run(
    struct mgos_homeassistant_automation *e,
    enum mgos_homeassistant_automation_datatype automation_type,
    void *automation_data);
bool mgos_homeassistant_automation_destroy(
    struct mgos_homeassistant_automation **e);
