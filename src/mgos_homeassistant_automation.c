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

#include "mgos_homeassistant_automation.h"

#include <string.h>
#include <strings.h>

#include "mgos.h"
#include "mgos_homeassistant_api.h"
#include "mgos_mqtt.h"

static bool trigger_status(void *trigger_data, void *data, void *user_data) {
  struct mgos_homeassistant_automation_data_status *td = (struct mgos_homeassistant_automation_data_status *) trigger_data;
  struct mgos_homeassistant_automation_data_status *d = (struct mgos_homeassistant_automation_data_status *) data;
  struct mgos_homeassistant *ha;
  struct mgos_homeassistant_object *o;
  int ret;
  char *p;

  ha = (struct mgos_homeassistant *) user_data;
  if (!ha) return false;

  ret = strcasecmp(td->object, d->object);
  LOG(LL_DEBUG, ("Trigger: trigger('%s') %s= data('%s')", td->object, ret == 0 ? "" : "!", d->object));
  if (0 != ret) return false;

  if (!(o = mgos_homeassistant_object_get(ha, td->object))) return false;

  p = strstr(o->status.buf, d->status);
  LOG(LL_DEBUG, ("Trigger: Object('%s').status='%.*s' %s= data('%s')", o->object_name, (int) o->status.len, o->status.buf, p ? "" : "!", d->status));

  return p != NULL;
}

static bool condition_status(void *data, void *user_data) {
  struct mgos_homeassistant_automation_data_status *d = (struct mgos_homeassistant_automation_data_status *) data;
  struct mgos_homeassistant *ha;
  struct mgos_homeassistant_object *o;
  char *p;

  if (!(ha = (struct mgos_homeassistant *) user_data)) return true;
  if (!(o = mgos_homeassistant_object_get(ha, d->object))) return true;

  p = strstr(o->status.buf, d->status);
  LOG(LL_DEBUG,
      ("Condition: Object('%s').status='%.*s' %s= data('%s')", o->object_name, (int) o->status.len, o->status.buf, p ? "" : "!", d->status));

  return p != NULL;
}

static void action_mqtt(struct mgos_homeassistant_automation_data_action_mqtt *data) {
  if (!data) return;

  LOG(LL_INFO, ("Action: MQTT topic='%s' payload='%s'", data->topic, data->payload));
  mgos_mqtt_pub(data->topic, data->payload, strlen(data->payload), 0, false);
  return;
}

static bool action_command(void *data, void *user_data) {
  struct mgos_homeassistant_automation_data_action_command *d = (struct mgos_homeassistant_automation_data_action_command *) data;
  struct mgos_homeassistant *ha;
  struct mgos_homeassistant_object *o;
  if (!d) return false;

  if (!(ha = (struct mgos_homeassistant *) user_data)) return false;
  if (!(o = mgos_homeassistant_object_get(ha, d->object))) return false;

  if (!o->cmd_cb) return false;

  LOG(LL_INFO, ("Action: Command object='%s' payload='%s'", d->object, d->payload));
  o->cmd_cb(o, d->payload, strlen(d->payload));

  return true;
}

bool mgos_homeassistant_automation_add_trigger(struct mgos_homeassistant_automation *a, enum mgos_homeassistant_automation_datatype type,
                                               void *data) {
  if (!a) return false;
  struct mgos_homeassistant_automation_data *d = mgos_homeassistant_automation_data_create(type, data);
  if (!d) return false;
  SLIST_INSERT_HEAD(&a->triggers, d, entry);
  LOG(LL_DEBUG, ("Inserted automation trigger data type %d", type));
  return true;
}

bool mgos_homeassistant_automation_add_condition(struct mgos_homeassistant_automation *a, enum mgos_homeassistant_automation_datatype type,
                                                 void *data) {
  if (!a) return false;
  struct mgos_homeassistant_automation_data *d = mgos_homeassistant_automation_data_create(type, data);
  if (!d) return false;
  SLIST_INSERT_HEAD(&a->conditions, d, entry);
  LOG(LL_DEBUG, ("Inserted automation condition data type %d", type));
  return true;
}

bool mgos_homeassistant_automation_add_action(struct mgos_homeassistant_automation *a, enum mgos_homeassistant_automation_datatype type, void *data) {
  if (!a) return false;
  struct mgos_homeassistant_automation_data *d = mgos_homeassistant_automation_data_create(type, data);
  if (!d) return false;
  SLIST_INSERT_HEAD(&a->actions, d, entry);
  LOG(LL_DEBUG, ("Inserted automation action data type %d", type));
  return true;
}

struct mgos_homeassistant_automation *mgos_homeassistant_automation_create(struct json_token json) {
  struct json_token val;
  struct mgos_homeassistant_automation *a = calloc(1, sizeof(*a));
  void *h = NULL;
  int idx;

  if (!a) return NULL;

  SLIST_INIT(&a->triggers);
  SLIST_INIT(&a->conditions);
  SLIST_INIT(&a->actions);

  while ((h = json_next_elem(json.ptr, json.len, h, ".trigger", &idx, &val)) != NULL) {
    char *j_type = NULL;
    json_scanf(val.ptr, val.len, "{type:%Q}", &j_type);
    if (!j_type || 0 == strcasecmp(j_type, "status")) {
      struct mgos_homeassistant_automation_data_status *dd = calloc(1, sizeof(*dd));
      if (dd) {
        json_scanf(val.ptr, val.len, "{object:%Q,status:%Q}", &dd->object, &dd->status);
        if (!mgos_homeassistant_automation_add_trigger(a, TRIGGER_STATUS, dd)) {
          LOG(LL_WARN, ("Could not add trigger JSON: %.*s, skipping", (int) val.len, val.ptr));
        } else {
          LOG(LL_DEBUG, ("Added trigger JSON: %.*s", (int) val.len, val.ptr));
        }
      }
    } else {
      LOG(LL_WARN, ("Unknown data type '%s', skipping ..", j_type));
    }
    if (j_type) free(j_type);
  }
  while ((h = json_next_elem(json.ptr, json.len, h, ".condition", &idx, &val)) != NULL) {
    char *j_type = NULL;
    json_scanf(val.ptr, val.len, "{type:%Q}", &j_type);
    if (!j_type || 0 == strcasecmp(j_type, "status")) {
      struct mgos_homeassistant_automation_data_status *dd = calloc(1, sizeof(*dd));
      if (dd) {
        json_scanf(val.ptr, val.len, "{object:%Q,status:%Q}", &dd->object, &dd->status);
        if (!mgos_homeassistant_automation_add_condition(a, CONDITION_STATUS, dd)) {
          LOG(LL_WARN, ("Could not add condition JSON: %.*s, skipping", (int) val.len, val.ptr));
        } else {
          LOG(LL_DEBUG, ("Added condition JSON: %.*s", (int) val.len, val.ptr));
        }
      }
    } else {
      LOG(LL_WARN, ("Unknown data type '%s', skipping ..", j_type));
    }
    if (j_type) free(j_type);
  }
  while ((h = json_next_elem(json.ptr, json.len, h, ".action", &idx, &val)) != NULL) {
    char *j_type = NULL;
    json_scanf(val.ptr, val.len, "{type:%Q}", &j_type);
    if (!j_type || 0 == strcasecmp(j_type, "mqtt")) {
      struct mgos_homeassistant_automation_data_action_mqtt *dd = calloc(1, sizeof(*dd));
      if (dd) {
        json_scanf(val.ptr, val.len, "{topic:%Q,payload:%Q}", &dd->topic, &dd->payload);
        if (!mgos_homeassistant_automation_add_action(a, ACTION_MQTT, dd)) {
          LOG(LL_WARN, ("Could not add action JSON: %.*s, skipping", (int) val.len, val.ptr));
        } else {
          LOG(LL_DEBUG, ("Added action JSON: %.*s", (int) val.len, val.ptr));
        }
      }
    } else if (0 == strcasecmp(j_type, "command")) {
      struct mgos_homeassistant_automation_data_action_command *dd = calloc(1, sizeof(*dd));
      if (dd) {
        json_scanf(val.ptr, val.len, "{object:%Q,payload:%Q}", &dd->object, &dd->payload);
        if (!mgos_homeassistant_automation_add_action(a, ACTION_COMMAND, dd)) {
          LOG(LL_WARN, ("Could not add action JSON: %.*s, skipping", (int) val.len, val.ptr));
        } else {
          LOG(LL_DEBUG, ("Added action JSON: %.*s", (int) val.len, val.ptr));
        }
      }
    } else {
      LOG(LL_WARN, ("Unknown data type '%s', skipping ..", j_type));
    }
    if (j_type) free(j_type);
  }

  LOG(LL_DEBUG, ("Created automation"));
  return a;
}

static bool mgos_homeassistant_automation_run_triggers(struct mgos_homeassistant_automation *a,
                                                       enum mgos_homeassistant_automation_datatype trigger_type, void *trigger_data,
                                                       void *user_data) {
  struct mgos_homeassistant_automation_data *d;
  if (!a || !trigger_data) return false;
  SLIST_FOREACH(d, &a->triggers, entry) {
    if (trigger_type != d->type) continue;
    switch (d->type) {
      case TRIGGER_STATUS:
        if (trigger_status(trigger_data, d->data, user_data)) return true;
      default:
        break;
    }
  }
  return false;
}

static bool mgos_homeassistant_automation_run_conditions(struct mgos_homeassistant_automation *a, void *user_data) {
  struct mgos_homeassistant_automation_data *d;
  if (!a) return true;
  SLIST_FOREACH(d, &a->conditions, entry) {
    switch (d->type) {
      case CONDITION_STATUS:
        if (!condition_status(d->data, user_data)) return false;
      default:
        break;
    }
  }
  return true;
}

static bool mgos_homeassistant_automation_run_actions(struct mgos_homeassistant_automation *a, void *user_data) {
  struct mgos_homeassistant_automation_data *d;
  if (!a) return true;
  SLIST_FOREACH(d, &a->actions, entry) {
    switch (d->type) {
      case ACTION_MQTT:
        action_mqtt((struct mgos_homeassistant_automation_data_action_mqtt *) d->data);
        break;
      case ACTION_COMMAND:
        action_command(d->data, user_data);
        break;
      default:
        break;
    }
  }
  return false;
  (void) user_data;
}

bool mgos_homeassistant_automation_run(struct mgos_homeassistant_automation *a, enum mgos_homeassistant_automation_datatype trigger_type,
                                       void *trigger_data, void *user_data) {
  if (!mgos_homeassistant_automation_run_triggers(a, trigger_type, trigger_data, user_data)) return false;
  if (!mgos_homeassistant_automation_run_conditions(a, user_data)) return false;
  return mgos_homeassistant_automation_run_actions(a, user_data);
}

struct mgos_homeassistant_automation_data *mgos_homeassistant_automation_data_create(enum mgos_homeassistant_automation_datatype type, void *data) {
  struct mgos_homeassistant_automation_data *d = calloc(1, sizeof(*d));
  if (!d) return NULL;
  LOG(LL_DEBUG, ("Creating automation data type %d", type));
  d->type = type;
  d->data = data;
  return d;
}

bool mgos_homeassistant_automation_data_destroy(struct mgos_homeassistant_automation_data **d) {
  if (!(*d)) return false;
  if (!(*d)->data) return true;
  LOG(LL_DEBUG, ("Destroying automation data type %d", (*d)->type));

  switch ((*d)->type) {
    case TRIGGER_STATUS:
    case CONDITION_STATUS: {
      struct mgos_homeassistant_automation_data_status *dd = (*d)->data;
      if (!dd) return true;
      if (dd->object) free(dd->object);
      if (dd->status) free(dd->status);
      break;
    }
    case ACTION_MQTT: {
      struct mgos_homeassistant_automation_data_action_mqtt *dd = (*d)->data;
      if (!dd) return true;
      if (dd->topic) free(dd->topic);
      if (dd->payload) free(dd->payload);
      break;
    }
    case ACTION_COMMAND: {
      struct mgos_homeassistant_automation_data_action_command *dd = (*d)->data;
      if (!dd) return true;
      if (dd->object) free(dd->object);
      if (dd->payload) free(dd->payload);
      break;
    }
    default:
      LOG(LL_WARN, ("Automation data type %d unknown, skipping .. ", (*d)->type));
  }
  return true;
}

bool mgos_homeassistant_automation_destroy(struct mgos_homeassistant_automation **a) {
  if (!(*a)) return false;
  LOG(LL_DEBUG, ("Destroying automation"));

  while (!SLIST_EMPTY(&(*a)->triggers)) {
    struct mgos_homeassistant_automation_data *d;
    d = SLIST_FIRST(&(*a)->triggers);
    mgos_homeassistant_automation_data_destroy(&d);
  }
  while (!SLIST_EMPTY(&(*a)->conditions)) {
    struct mgos_homeassistant_automation_data *d;
    d = SLIST_FIRST(&(*a)->conditions);
    mgos_homeassistant_automation_data_destroy(&d);
  }
  while (!SLIST_EMPTY(&(*a)->actions)) {
    struct mgos_homeassistant_automation_data *d;
    d = SLIST_FIRST(&(*a)->actions);
    mgos_homeassistant_automation_data_destroy(&d);
  }

  free(*a);
  *a = NULL;
  return true;
}
