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

#ifdef MGOS_HAVE_SI7021_I2C
#include "mgos_homeassistant_si7021.h"

#include <math.h>

static void si7021_timer(void *user_data) {
  struct mgos_homeassistant_object *o = (struct mgos_homeassistant_object *) user_data;
  if (!o) return;
  mgos_homeassistant_object_send_status(o);
}

static void si7021_stat_humidity(struct mgos_homeassistant_object *o, struct json_out *json) {
  struct mgos_homeassistant_si7021 *d = NULL;
  float humidity = NAN;

  if (!o || !json) return;
  if (!(d = (struct mgos_homeassistant_si7021 *) o->user_data)) return;

  humidity = mgos_si7021_getHumidity(d->dev);
  json_printf(json, "%.1f", humidity);
}

static void si7021_stat_temperature(struct mgos_homeassistant_object *o, struct json_out *json) {
  struct mgos_homeassistant_si7021 *d = NULL;
  float temperature = NAN;

  if (!o || !json) return;
  if (!(d = (struct mgos_homeassistant_si7021 *) o->user_data)) return;

  temperature = mgos_si7021_getTemperature(d->dev);
  json_printf(json, "%.2f", temperature);
}

static void si7021_pre_remove_cb(struct mgos_homeassistant_object *o) {
  struct mgos_homeassistant_si7021 *d = NULL;
  if (!o) return;
  if (!(d = (struct mgos_homeassistant_si7021 *) o->user_data)) return;
  mgos_clear_timer(d->timer);
  if (d->dev) mgos_si7021_destroy(&d->dev);
  free(o->user_data);
  o->user_data = NULL;
}

bool mgos_homeassistant_si7021_fromjson(struct mgos_homeassistant *ha, struct json_token val) {
  int i2caddr = -1;
  int period = 60;
  bool ret = false;
  struct mgos_homeassistant_object *o = NULL;
  struct mgos_homeassistant_si7021 *d = NULL;
  char object_name[20];
  char *name = NULL;
  char *nameptr = NULL;

  if (!ha) goto exit;
  if (!(d = calloc(1, sizeof(*d)))) goto exit;

  json_scanf(val.ptr, val.len, "{i2caddr:%d,period:%d,name:%Q}", &i2caddr, &period, &name);
  d->dev = mgos_si7021_create(mgos_i2c_get_global(), i2caddr);
  if (!d->dev) {
    LOG(LL_ERROR, ("Could not create si7021 at i2caddr=%d", i2caddr));
    goto exit;
  }

  if (!name) {
    mgos_homeassistant_object_generate_name(ha, "si7021_", object_name, sizeof(object_name));
    nameptr = object_name;
  } else {
    nameptr = name;
  }

  o = mgos_homeassistant_object_add(ha, nameptr, COMPONENT_SENSOR, NULL, NULL, d);
  if (!o) {
    LOG(LL_ERROR, ("Could not add object %s to homeassistant", nameptr));
    goto exit;
  }
  o->pre_remove_cb = si7021_pre_remove_cb;

  if (!mgos_homeassistant_object_class_add(o, "humidity", "\"unit_of_measurement\":\"%\"", si7021_stat_humidity)) {
    LOG(LL_ERROR, ("Could not add 'humidity' class to object %s", nameptr));
    goto exit;
  }
  if (!mgos_homeassistant_object_class_add(o, "temperature", "\"unit_of_measurement\":\"°C\"", si7021_stat_temperature)) {
    LOG(LL_ERROR, ("Could not add 'temperature' class to object %s", nameptr));
    goto exit;
  }

  if (period > 0) d->timer = mgos_set_timer(period * 1000, true, si7021_timer, o);

  ret = true;
  LOG(LL_DEBUG, ("Successfully created object %s", nameptr));
exit:
  if (name) free(name);
  if (!ret) si7021_pre_remove_cb(o);
  return ret;
}

#endif  // MGOS_HAVE_SI7021_I2C
