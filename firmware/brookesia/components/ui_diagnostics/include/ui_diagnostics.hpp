/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

namespace esp_brookesia::systems::phone {
class Phone;
}

/**
 * Start the development-only USB UI test console. Commands are documented by
 * the console's `help` command. Calling this more than once is harmless.
 */
esp_err_t ui_diagnostics_start(esp_brookesia::systems::phone::Phone *phone,
                               lv_display_t *display);
