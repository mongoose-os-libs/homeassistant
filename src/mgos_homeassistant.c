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

bool mgos_homeassistant_fromfile(struct mgos_homeassistant *ha,
                                 const char *filename) {
  return mgos_homeassistant_fromjson(ha, json_fread(filename));
}

bool mgos_homeassistant_fromjson(struct mgos_homeassistant *ha,
                                 const char *json) {
  if (!ha || !json) return false;

  return false;
}
