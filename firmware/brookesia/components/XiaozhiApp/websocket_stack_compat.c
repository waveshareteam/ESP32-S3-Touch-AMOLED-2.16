/* SPDX-License-Identifier: Apache-2.0 */
#include "esp_log.h"
#include "esp_websocket_client.h"

/* esp_xiaozhi 0.1.1 does not expose the transport task stack in its public
 * configuration. Its zero value selects the WebSocket client's 4 KB default,
 * which overflowed on this S3 during TLS/MCP startup. Keep the adjustment in
 * this product without modifying the managed component. */
esp_websocket_client_handle_t __real_esp_websocket_client_init(
    const esp_websocket_client_config_t *config);

esp_websocket_client_handle_t __wrap_esp_websocket_client_init(
    const esp_websocket_client_config_t *config)
{
    if (config == NULL) {
        return __real_esp_websocket_client_init(config);
    }
    esp_websocket_client_config_t local = *config;
    if (local.task_stack == 0) {
        local.task_stack = 12 * 1024;
        ESP_LOGI("XiaozhiTransport", "WebSocket task stack: %d bytes", local.task_stack);
    }
    return __real_esp_websocket_client_init(&local);
}
