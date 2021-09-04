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

#ifdef MGOS_HAVE_BAROMETER
#include "mgos_homeassistant_barometer.h"

#include <math.h>

static void barometer_timer(void *ud) {
  struct mgos_homeassistant_object *o = (struct mgos_homeassistant_object *) ud;
  if (!o) return;
  mgos_homeassistant_object_send_status(o);
}

static void barometer_stat_humidity(struct mgos_homeassistant_object *o, struct json_out *json) {
  struct mgos_homeassistant_barometer *d = NULL;
  float humidity = NAN;

  if (!o || !json) return;
  if (!(d = (struct mgos_homeassistant_barometer *) o->user_data)) return;

  if (mgos_barometer_get_humidity(d->dev, &humidity)) json_printf(json, "%.1f", humidity);
}

static void barometer_stat_temperature(struct mgos_homeassistant_object *o, struct json_out *json) {
  struct mgos_homeassistant_barometer *d = NULL;
  float temperature = NAN;

  if (!o || !json) return;
  if (!(d = (struct mgos_homeassistant_barometer *) o->user_data)) return;

  if (mgos_barometer_get_temperature(d->dev, &temperature)) json_printf(json, "%.2f", temperature);
}

static void barometer_stat_pressure(struct mgos_homeassistant_object *o, struct json_out *json) {
  struct mgos_homeassistant_barometer *d = NULL;
  float pressure = NAN;

  if (!o || !json) return;
  if (!(d = (struct mgos_homeassistant_barometer *) o->user_data)) return;

  // Report pressure as hPa.
  if (mgos_barometer_get_pressure(d->dev, &pressure)) json_printf(json, "%.2f", pressure / 100.);
}

static void barometer_pre_remove_cb(struct mgos_homeassistant_object *o) {
  struct mgos_homeassistant_barometer *d = NULL;
  if (!o) return;
  if (!(d = (struct mgos_homeassistant_barometer *) o->user_data)) return;
  mgos_clear_timer(d->timer);
  if (d->dev) mgos_barometer_destroy(&d->dev);
  free(d);
  o->user_data = NULL;
}

bool mgos_homeassistant_barometer_fromjson(struct mgos_homeassistant *ha, struct json_token val) {
  int i2caddr = -1;
  int period = 60;
  bool ret = false;
  struct mgos_homeassistant_object *o = NULL;
  struct mgos_homeassistant_barometer *d = NULL;
  enum mgos_barometer_type baro_type;
  char object_name[20];
  char *name = NULL;
  char *type = NULL;
  char *nameptr = NULL;

  if (!ha) goto exit;

  json_scanf(val.ptr, val.len, "{i2caddr:%d,period:%d,name:%Q,type:%Q}", &i2caddr, &period, &name, &type);
  if (!type) {
    LOG(LL_ERROR, ("Missing mandatory field: type"));
    goto exit;
  }
  if (0 == strcasecmp(type, "bme280"))
    baro_type = BARO_BME280;
  else if (0 == strcasecmp(type, "bmp280"))
    baro_type = BARO_BME280;
  else if (0 == strcasecmp(type, "mpl115"))
    baro_type = BARO_MPL115;
  else if (0 == strcasecmp(type, "mpl3115"))
    baro_type = BARO_MPL3115;
  else if (0 == strcasecmp(type, "ms5611"))
    baro_type = BARO_MS5611;
  else {
    goto exit;
  }
  if (!(d = calloc(1, sizeof(*d)))) goto exit;

  if (!(d->dev = mgos_barometer_create_i2c(mgos_i2c_get_global(), i2caddr, baro_type))) {
    LOG(LL_ERROR, ("Could not create barometer of type %s at i2caddr %d", type, i2caddr));
    goto exit;
  }

  if (!name) {
    mgos_homeassistant_object_generate_name(ha, "barometer_", object_name, sizeof(object_name));
    nameptr = object_name;
  } else {
    nameptr = name;
  }

  o = mgos_homeassistant_object_add(ha, nameptr, COMPONENT_SENSOR, NULL, NULL, d);
  if (!o) {
    LOG(LL_ERROR, ("Could not add object %s to homeassistant", nameptr));
    goto exit;
  }
  o->pre_remove_cb = barometer_pre_remove_cb;

  float value = NAN;
  if (mgos_barometer_get_humidity(d->dev, &value)) {
    if (!mgos_homeassistant_object_class_add(o, "humidity", "\"unit_of_measurement\":\"%\"", barometer_stat_humidity)) {
      LOG(LL_ERROR, ("Could not add 'humidity' class to object %s", nameptr));
      goto exit;
    }
  }
  if (mgos_barometer_get_temperature(d->dev, &value)) {
    if (!mgos_homeassistant_object_class_add(o, "temperature", "\"unit_of_measurement\":\"°C\"", barometer_stat_temperature)) {
      LOG(LL_ERROR, ("Could not add 'temperature' class to object %s", nameptr));
      goto exit;
    }
  }
  if (mgos_barometer_get_pressure(d->dev, &value)) {
    if (!mgos_homeassistant_object_class_add(o, "pressure", "\"unit_of_measurement\":\"hPa\"", barometer_stat_pressure)) {
      LOG(LL_ERROR, ("Could not add 'pressure' class to object %s", nameptr));
      goto exit;
    }
  }

  if (period > 0) d->timer = mgos_set_timer(period * 1000, true, barometer_timer, o);

  ret = true;
  LOG(LL_DEBUG, ("Successfully created object %s", nameptr));
exit:
  if (name) free(name);
  if (type) free(type);
  if (!ret && !o && d) free(d);
  if (!ret && o) mgos_homeassistant_object_remove(&o);
  return ret;
}

#endif  // MGOS_HAVE_BAROMETER
