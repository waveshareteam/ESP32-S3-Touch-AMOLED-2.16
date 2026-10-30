/* SPDX-FileCopyrightText: 2026 Waveshare
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>
#include "lvgl.h"

namespace esp_brookesia::apps {

/** A static status view that leaves rendering time and RAM for speech recognition. */
class XiaozhiUi {
public:
    using ActionCallback = void (*)(void *context);
    enum class Mode : uint8_t {
        Offline, Busy, Activation, Ready, Listening, Thinking, Speaking, Error,
    };

    XiaozhiUi() = default;
    ~XiaozhiUi();
    XiaozhiUi(const XiaozhiUi &) = delete;
    XiaozhiUi &operator=(const XiaozhiUi &) = delete;

    bool preload();
    bool create(lv_obj_t *parent, ActionCallback action_callback, void *action_context,
                bool multiline_subtitle = false);
    void destroy();
    void setNetworkReady(bool ready);
    void setStatus(const char *status);
    void setMode(Mode mode);
    void setLevel(float level);
    void setChatMessage(const char *role, const char *text);
    void clearChatMessages();
    void setHint(const char *hint);
    void showToast(const char *text);
    void setEmotion(const char *emotion);
    void setActivation(const char *code, const char *message, bool visible);
    lv_obj_t *root() const { return _root; }

private:
    static void clickCallback(lv_event_t *event);
    void destroyView();
    void applyMode();
    static uint32_t modeColor(Mode mode);

    lv_obj_t *_root = nullptr;
    lv_obj_t *_network_label = nullptr;
    lv_obj_t *_status_dot = nullptr;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_hint_label = nullptr;
    lv_obj_t *_action_button = nullptr;
    lv_obj_t *_action_label = nullptr;
    lv_obj_t *_activation_box = nullptr;
    lv_obj_t *_activation_code = nullptr;
    lv_obj_t *_activation_message = nullptr;
    lv_obj_t *_conversation_box = nullptr;
    lv_obj_t *_user_box = nullptr;
    lv_obj_t *_reply_box = nullptr;
    lv_obj_t *_user_label = nullptr;
    lv_obj_t *_reply_label = nullptr;
    ActionCallback _action_callback = nullptr;
    void *_action_context = nullptr;
    const lv_font_t *_text_font = nullptr;
    bool _fonts_acquired = false;
    Mode _mode = Mode::Offline;
    bool _activation_visible = false;
};

} // namespace esp_brookesia::apps
