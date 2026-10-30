/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "./ui/AboutPage.hpp"
#include "./ui/DisplayPage.hpp"
#include "./ui/PowerPage.hpp"
#include "./ui/SoundPage.hpp"
#include "./ui/StoragePage.hpp"
#include "./ui/WlanPage.hpp"

namespace esp_brookesia::apps
{

    /**
     * @brief Settings application for displaying a list of options
     *
     */
    class Settings : public systems::phone::App
    {
    public:
        static Settings *requestInstance(bool use_status_bar = true, bool use_navigation_bar = false);
        /** Open WLAN for an app and return to it only after this request gets an IP. */
        static bool openWlan(int return_app_id);
        /** Cancel an outstanding WLAN redirect. A negative id cancels any redirect. */
        static void cancelWlan(int return_app_id = -1);
        ~Settings();
        bool run(void) override;
        void showRootPage();

    protected:
        Settings(bool use_status_bar, bool use_navigation_bar);

        bool back(void) override;
        bool close(void) override;
        bool init(void) override;
        bool deinit(void) override;
        bool pause(void) override;
        bool resume(void) override;

    private:
        enum class ActivePage {
            None,
            Wlan,
            Sound,
            Display,
            Power,
            Storage,
            About,
        };

        static void event_handler_cb(lv_event_t *e);
        static void open_page_async_cb(void *user_data);
        static void wlanRedirectTimerCallback(lv_timer_t *timer);
        void add_button_with_arrow(const void *icon, const char *text);
        void create_settings_ui();
        void destroy_settings_ui();
        void event_handler(lv_event_t *e);
        void open_page(const char *page_name);
        void close_active_page();
        void consumeWlanStartRequest();
        void ensureWlanRedirectTimer();
        void processWlanRedirect();

        esp_err_t initWifi();

    private:
        static Settings *_instance;
        lv_obj_t *page_root;
        lv_obj_t *list1;
        lv_style_t style_list;
        lv_style_t style_list_btn;
        lv_style_t style_list_text;
        lv_style_t style_list_btn_pressed;
        ActivePage active_page;
        bool fonts_acquired = false;
        bool wlan_redirect_active = false;
        lv_timer_t *wlan_redirect_timer = nullptr;
    };

} // namespace esp_brookesia::apps
