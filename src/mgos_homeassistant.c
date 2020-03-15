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

#include "mgos_homeassistant.h"

#include "mgos.h"
#include "mgos_homeassistant_si7021.h"

bool mgos_homeassistant_fromfile(struct mgos_homeassistant *ha,
                                 const char *filename) {
  return mgos_homeassistant_fromjson(ha, json_fread(filename));
}

bool mgos_homeassistant_fromjson(struct mgos_homeassistant *ha,
                                 const char *json) {
  struct json_token val;
  void *h = NULL;
  int idx;

  char *name = NULL;

  if (!ha || !json) return false;

  // Set global config elements
  json_scanf(json, strlen(json), "{name:%Q}", &name);
  if (name) {
    if (ha->node_name) free(ha->node_name);
    ha->node_name = strdup(name);
  }

  // Read providers
  while ((h = json_next_elem(json, strlen(json), h, ".provider.si7021", &idx,
                             &val)) != NULL) {
#ifdef MGOS_HAVE_SI7021_I2C
    if (!mgos_homeassistant_si7021_fromjson(ha, val)) {
      LOG(LL_WARN, ("Failed to add object from provider si7021, index %d, json "
                    "follows:%.*s",
                    idx, (int) val.len, val.ptr));
    }
#else
    LOG(LL_ERROR, ("provider.si7021 config found: Add si7021-i2c to mos.yml, "
                   "skipping .. "));
#endif
  }

  if (name) free(name);
  return true;
}
