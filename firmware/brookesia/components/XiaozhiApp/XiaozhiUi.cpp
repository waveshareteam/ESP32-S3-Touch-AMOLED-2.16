/* SPDX-FileCopyrightText: 2026 Waveshare
 * SPDX-License-Identifier: Apache-2.0 */
#include "XiaozhiUi.hpp"

#include <cstring>
#include "app_fonts.h"
#include "esp_log.h"

namespace esp_brookesia::apps {
namespace {
constexpr uint32_t BACKGROUND = 0x070B18;
constexpr uint32_t SURFACE = 0x151C2D;
constexpr uint32_t BORDER = 0x273451;
constexpr uint32_t PRIMARY = 0xF2F5FF;
constexpr uint32_t SECONDARY = 0xAAB4CB;
constexpr uint32_t USER = 0x8E98AA;
constexpr char TAG[] = "XiaozhiUi";

void setText(lv_obj_t *label, const char *text)
{
    if (!label) return;
    const char *value = text ? text : "";
    if (strcmp(lv_label_get_text(label), value) != 0) lv_label_set_text(label, value);
}

void setHidden(lv_obj_t *obj, bool hidden)
{
    if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void makePanel(lv_obj_t *obj, uint32_t color, int radius)
{
    // Geometry is explicit; the default theme's card padding would make the
    // fixed user/reply viewports overlap inside their 204 px parent.
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
}
} // namespace

XiaozhiUi::~XiaozhiUi() { destroy(); }
bool XiaozhiUi::preload() { return true; }

bool XiaozhiUi::create(lv_obj_t *parent, ActionCallback action_callback,
                       void *action_context, bool multiline_subtitle)
{
    (void)multiline_subtitle;
    if (!parent) return false;
    destroyView();
    app_fonts_acquire();
    _fonts_acquired = true;
    _text_font = app_font_text_20();
    if (!_text_font) { destroyView(); return false; }
    _action_callback = action_callback;
    _action_context = action_context;

    _root = lv_obj_create(parent);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));
    lv_obj_center(_root);
    lv_obj_set_style_bg_color(_root, lv_color_hex(BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(_root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), 72);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(header, lv_color_hex(BORDER), LV_PART_MAIN);
    _network_label = lv_label_create(header);
    lv_label_set_text(_network_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(_network_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(_network_label, LV_ALIGN_LEFT_MID, 28, 0);
    _status_dot = lv_obj_create(header);
    lv_obj_remove_style_all(_status_dot);
    lv_obj_set_size(_status_dot, 10, 10);
    lv_obj_set_style_radius(_status_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_align(_status_dot, LV_ALIGN_LEFT_MID, 66, 0);
    _status_label = lv_label_create(header);
    lv_obj_set_width(_status_label, 350);
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(_status_label, _text_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(_status_label, lv_color_hex(PRIMARY), LV_PART_MAIN);
    lv_obj_align(_status_label, LV_ALIGN_LEFT_MID, 88, 0);

    _activation_box = lv_obj_create(_root);
    lv_obj_set_size(_activation_box, 368, 186);
    lv_obj_align(_activation_box, LV_ALIGN_TOP_MID, 0, 126);
    makePanel(_activation_box, SURFACE, 18);
    lv_obj_add_flag(_activation_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_activation_box, clickCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(_activation_box, LV_OBJ_FLAG_HIDDEN);
    _activation_code = lv_label_create(_activation_box);
    lv_obj_set_width(_activation_code, lv_pct(100));
    lv_label_set_long_mode(_activation_code, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(_activation_code, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(_activation_code, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(_activation_code, lv_color_hex(PRIMARY), LV_PART_MAIN);
    lv_obj_align(_activation_code, LV_ALIGN_TOP_MID, 0, 24);
    _activation_message = lv_label_create(_activation_box);
    lv_obj_set_width(_activation_message, 320);
    lv_label_set_long_mode(_activation_message, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(_activation_message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(_activation_message, _text_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(_activation_message, lv_color_hex(SECONDARY), LV_PART_MAIN);
    lv_obj_align(_activation_message, LV_ALIGN_BOTTOM_MID, 0, -20);

    _conversation_box = lv_obj_create(_root);
    lv_obj_set_size(_conversation_box, 416, 204);
    lv_obj_align(_conversation_box, LV_ALIGN_TOP_MID, 0, 92);
    makePanel(_conversation_box, SURFACE, 18);
    lv_obj_clear_flag(_conversation_box, LV_OBJ_FLAG_SCROLLABLE);

    _user_box = lv_obj_create(_conversation_box);
    lv_obj_remove_style_all(_user_box);
    lv_obj_set_size(_user_box, 384, 56);
    lv_obj_align(_user_box, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_remove_flag(_user_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(_user_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_user_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_user_box, LV_SCROLLBAR_MODE_OFF);
    _user_label = lv_label_create(_user_box);
    lv_obj_set_width(_user_label, lv_pct(100));
    lv_label_set_long_mode(_user_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(_user_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(_user_label, _text_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(_user_label, lv_color_hex(USER), LV_PART_MAIN);
    lv_obj_align(_user_label, LV_ALIGN_TOP_MID, 0, 0);

    _reply_box = lv_obj_create(_conversation_box);
    lv_obj_remove_style_all(_reply_box);
    lv_obj_set_size(_reply_box, 384, 112);
    lv_obj_align(_reply_box, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_remove_flag(_reply_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_border_width(_reply_box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(_reply_box, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_color(_reply_box, lv_color_hex(BORDER), LV_PART_MAIN);
    lv_obj_add_flag(_reply_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_reply_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_reply_box, LV_SCROLLBAR_MODE_OFF);
    _reply_label = lv_label_create(_reply_box);
    lv_obj_set_width(_reply_label, lv_pct(100));
    lv_label_set_long_mode(_reply_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(_reply_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(_reply_label, _text_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(_reply_label, lv_color_hex(PRIMARY), LV_PART_MAIN);
    lv_obj_align(_reply_label, LV_ALIGN_TOP_MID, 0, 8);

    _action_button = lv_button_create(_root);
    lv_obj_set_size(_action_button, 326, 76);
    lv_obj_align(_action_button, LV_ALIGN_TOP_MID, 0, 352);
    lv_obj_set_style_radius(_action_button, 18, LV_PART_MAIN);
    lv_obj_set_style_border_width(_action_button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(_action_button, clickCallback, LV_EVENT_CLICKED, this);
    _action_label = lv_label_create(_action_button);
    lv_obj_set_width(_action_label, 292);
    lv_label_set_long_mode(_action_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(_action_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(_action_label, _text_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(_action_label, lv_color_hex(PRIMARY), LV_PART_MAIN);
    lv_obj_center(_action_label);
    _hint_label = lv_label_create(_root);
    lv_obj_set_width(_hint_label, 390);
    lv_label_set_long_mode(_hint_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(_hint_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(_hint_label, _text_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(_hint_label, lv_color_hex(SECONDARY), LV_PART_MAIN);
    lv_obj_set_height(_hint_label, 30);
    lv_obj_align(_hint_label, LV_ALIGN_TOP_MID, 0, 306);
    applyMode();
    setNetworkReady(false);
    setStatus("");
    clearChatMessages();
    lv_obj_remove_flag(_conversation_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_update_layout(_root);
    lv_area_t user = {}, reply = {}, hint = {}, button = {};
    lv_obj_get_coords(_user_box, &user);
    lv_obj_get_coords(_reply_box, &reply);
    lv_obj_get_coords(_hint_label, &hint);
    lv_obj_get_coords(_action_button, &button);
    ESP_LOGI(TAG, "Layout user=(%ld,%ld)-(%ld,%ld) reply=(%ld,%ld)-(%ld,%ld) hint=(%ld,%ld)-(%ld,%ld) button=(%ld,%ld)-(%ld,%ld)",
             (long)user.x1, (long)user.y1, (long)user.x2, (long)user.y2,
             (long)reply.x1, (long)reply.y1, (long)reply.x2, (long)reply.y2,
             (long)hint.x1, (long)hint.y1, (long)hint.x2, (long)hint.y2,
             (long)button.x1, (long)button.y1, (long)button.x2, (long)button.y2);
    return true;
}

void XiaozhiUi::destroy() { destroyView(); }
void XiaozhiUi::destroyView()
{
    if (_root) lv_obj_delete(_root);
    _root = nullptr;
    _network_label = _status_dot = _status_label = _hint_label = nullptr;
    _action_button = _action_label = nullptr;
    _activation_box = _activation_code = _activation_message = nullptr;
    _conversation_box = _user_box = _reply_box = nullptr;
    _user_label = _reply_label = nullptr;
    _action_callback = nullptr;
    _action_context = nullptr;
    _activation_visible = false;
    if (_fonts_acquired) { app_fonts_release(); _fonts_acquired = false; }
    _text_font = nullptr;
}

uint32_t XiaozhiUi::modeColor(Mode mode)
{
    switch (mode) {
    case Mode::Offline: return 0x6B7280;
    case Mode::Busy: case Mode::Activation: return 0x5B8CFF;
    case Mode::Ready: return 0x4F7CFF;
    case Mode::Listening: return 0x2EE6A6;
    case Mode::Thinking: return 0xFFB84D;
    case Mode::Speaking: return 0xA78BFA;
    case Mode::Error: default: return 0xFF5C7A;
    }
}

void XiaozhiUi::applyMode()
{
    if (!_root) return;
    const lv_color_t color = lv_color_hex(modeColor(_mode));
    lv_obj_set_style_bg_color(_status_dot, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_status_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_action_button, color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_action_button, lv_color_darken(color, LV_OPA_30), LV_STATE_PRESSED);
    const char *action = "Retry";
    switch (_mode) {
    case Mode::Offline: action = "Wi-Fi"; break;
    case Mode::Busy: case Mode::Thinking: action = "Wait"; break;
    case Mode::Activation: action = "Activate"; break;
    case Mode::Ready: action = "Talk"; break;
    case Mode::Listening: case Mode::Speaking: action = "Stop"; break;
    case Mode::Error: default: break;
    }
    setText(_action_label, action);
}

void XiaozhiUi::setNetworkReady(bool ready)
{
    if (!_network_label) return;
    lv_label_set_text(_network_label, ready ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(_network_label, lv_color_hex(ready ? 0x67E8B6 : SECONDARY), LV_PART_MAIN);
}
void XiaozhiUi::setStatus(const char *status) { setText(_status_label, status); }
void XiaozhiUi::setMode(Mode mode) { if (_mode != mode) { _mode = mode; applyMode(); } }
void XiaozhiUi::setLevel(float level) { (void)level; }
void XiaozhiUi::setChatMessage(const char *role, const char *text)
{
    const char *value = text ? text : "";
    if (!value[0]) clearChatMessages();
    else if (role && strcmp(role, "user") == 0) { setText(_user_label, value); setText(_reply_label, ""); }
    else if (role && strcmp(role, "assistant") == 0) setText(_reply_label, value);
}
void XiaozhiUi::clearChatMessages() { setText(_user_label, ""); setText(_reply_label, ""); }
void XiaozhiUi::setHint(const char *hint)
{
    setText(_hint_label, hint);
}
void XiaozhiUi::showToast(const char *text) { setStatus(text); }
void XiaozhiUi::setEmotion(const char *emotion) { (void)emotion; }
void XiaozhiUi::setActivation(const char *code, const char *message, bool visible)
{
    visible = visible && code && code[0];
    setText(_activation_code, visible ? code : "");
    setText(_activation_message, visible ? message : "");
    if (_activation_visible == visible) return;
    _activation_visible = visible;
    setHidden(_activation_box, !visible);
    setHidden(_action_button, visible);
    setHidden(_hint_label, visible);
    setHidden(_conversation_box, visible);
}
void XiaozhiUi::clickCallback(lv_event_t *event)
{
    auto *ui = static_cast<XiaozhiUi *>(lv_event_get_user_data(event));
    if (ui && ui->_action_callback) ui->_action_callback(ui->_action_context);
}
} // namespace esp_brookesia::apps
