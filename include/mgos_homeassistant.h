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

#include "mgos_homeassistant_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ha_provider_cfg_handler)(struct mgos_homeassistant *ha, struct json_token val);

struct mgos_homeassistant *mgos_homeassistant_get_global(void);
bool mgos_homeassistant_fromfile(struct mgos_homeassistant *ha, const char *filename);
bool mgos_homeassistant_fromjson(struct mgos_homeassistant *ha, const char *json);
bool mgos_homeassistant_clear(struct mgos_homeassistant *ha);
bool mgos_homeassistant_register_provider(const char *provider, ha_provider_cfg_handler cfg_handler, const char *mos_mod);

#ifdef __cplusplus
}
#endif
