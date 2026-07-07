# ESP-Brookesia Notes

`examples/esp-idf/03_esp-brookesia` is a rich UI firmware example with LVGL, ESP-Brookesia, local app assets, SPIFFS music assets, and board audio glue.

Treat ESP-IDF v6 support as conditional on the current ESP-Brookesia and managed component releases. If a v6 build failure is rooted in upstream Brookesia compatibility, fix or pin the upstream component first instead of adding broad product-local workarounds.

Future TODO: when a verified v6-compatible Brookesia reference is available, synchronize the compatibility changes into this example and update the CI notes.