/*
 * Copyright 2020 Mircho Mirev <mircho.mirev@gmail.com>
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


#ifdef MGOS_HAVE_BH1750
#include <math.h>
#include "mgos.h"
#include "mgos_homeassistant_bh1750.h"

static void bh1750_timer(void *user_data) {
  struct mgos_homeassistant_object *o = (struct mgos_homeassistant_object *) user_data;
  if (!o) return;
  mgos_homeassistant_object_send_status(o);
}

static void bh1750_stat_light(struct mgos_homeassistant_object *o, struct json_out *json) {
  struct mgos_homeassistant_bh1750 *d = NULL;
  float lux = NAN;

  if (!o || !json) return;
  if (!(d = (struct mgos_homeassistant_bh1750 *) o->user_data)) return;

  lux = mgos_bh1750_read_lux(d->dev, NULL);
  json_printf(json, "%.1f", lux);
  //initiate another measurment
  mgos_bh1750_set_config( d->dev, MGOS_BH1750_MODE_ONCE_HIGH_RES, MGOS_BH1750_MTIME_DEFAULT );
}

static void bh1750_pre_remove_cb(struct mgos_homeassistant_object *o) {
  struct mgos_homeassistant_bh1750 *d = NULL;
  if (!o) return;
  if (!(d = (struct mgos_homeassistant_bh1750 *) o->user_data)) return;
  mgos_clear_timer(d->timer);
  if (d->dev) mgos_bh1750_free(d->dev);
  free(o->user_data);
  o->user_data = NULL;
}

bool mgos_homeassistant_bh1750_fromjson(struct mgos_homeassistant *ha, struct json_token val) {
  int i2caddr = -1;
  int period = 60;
  bool ret = false;
  struct mgos_homeassistant_object *o = NULL;
  struct mgos_homeassistant_bh1750 *d = NULL;
  char object_name[20];
  char *name = NULL;
  char *nameptr = NULL;

  if (!ha) goto exit;
  if (!(d = calloc(1, sizeof(*d)))) goto exit;

  json_scanf(val.ptr, val.len, "{i2caddr:%d,period:%d,name:%Q}", &i2caddr, &period, &name);
  d->dev = mgos_bh1750_create(i2caddr);

  if (!d->dev) {
    LOG(LL_ERROR, ("Could not create bh1750 at i2caddr=%d", i2caddr));
    goto exit;
  }

  //initiate a one time measurement
  mgos_bh1750_set_config( d->dev, MGOS_BH1750_MODE_ONCE_HIGH_RES, MGOS_BH1750_MTIME_DEFAULT );

  if (!name) {
    mgos_homeassistant_object_generate_name(ha, "bh1750_", object_name, sizeof(object_name));
    nameptr = object_name;
  } else {
    nameptr = name;
  }

  o = mgos_homeassistant_object_add(ha, nameptr, COMPONENT_SENSOR, NULL, NULL, d);
  if (!o) {
    LOG(LL_ERROR, ("Could not add object %s to homeassistant", nameptr));
    goto exit;
  }
  o->pre_remove_cb = bh1750_pre_remove_cb;

  // if (!mgos_homeassistant_object_class_add(o, "illuminance", "\"unit_of_measurement\":\"lx\"", bh1750_stat_light)) {
  if (!mgos_homeassistant_object_class_add(o, "illuminance", "\"unit_of_measurement\":\"lx\"", bh1750_stat_light)) {
    LOG(LL_ERROR, ("Could not add 'illuminance' class to object %s", nameptr));
    goto exit;
  }

  if (period > 0) d->timer = mgos_set_timer(period * 1000, true, bh1750_timer, o);

  ret = true;
  LOG(LL_DEBUG, ("Successfully created object %s", nameptr));
exit:
  if (name) free(name);
  if (!ret && o) mgos_homeassistant_object_remove(&o);
  return ret;
}

#endif  // MGOS_HAVE_bh1750_I2C
