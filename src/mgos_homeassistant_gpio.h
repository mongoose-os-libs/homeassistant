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
#include "mgos.h"
#include "mgos_gpio.h"
#include "mgos_homeassistant.h"
#include "timespec.h"

struct mgos_homeassistant_gpio_motion {
  int gpio;
  int timeout_secs;
  int debounce_ms;
  bool invert;
  bool state;

  mgos_timer_id timer;
};

struct mgos_homeassistant_gpio_binary_sensor {
  int gpio;
  int debounce_ms;
  int timeout_ms;
  bool invert;

  uint8_t click_count;
  mgos_timer_id click_timer;
};

struct mgos_homeassistant_gpio_switch {
  int gpio;
  bool invert;

  // Duration tracking
  mgos_timer_id timer;

  // Schedule tracking
  struct mgos_timespec *schedule_timespec;
  bool schedule_override;
  mgos_timer_id schedule_timer;
};

bool mgos_homeassistant_gpio_fromjson(struct mgos_homeassistant *ha, struct json_token val);
