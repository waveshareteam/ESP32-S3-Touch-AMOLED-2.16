/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <math.h>
#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>
#include <unistd.h>
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:App:Calculator"
#include "esp_lib_utils.h"
#include "esp_brookesia_app_calculator.hpp"

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(img_app_calculator);

#define APP_NAME                "Calculator"

#define CALCULATOR_MARGIN       14
#define CALCULATOR_KEY_GAP      8
#define CALCULATOR_DISPLAY_H    140
#define CALCULATOR_MIN_SCALE    154
#define CALCULATOR_DIGIT_COLOR    lv_color_hex(0x1F232C)
#define CALCULATOR_FUNCTION_COLOR lv_color_hex(0x3A3F4C)
#define CALCULATOR_OPERATOR_COLOR lv_color_hex(0xFF9F0A)
#define CALCULATOR_RESULT_COLOR   lv_color_hex(0x9AA0AC)

static constexpr int MAX_NUMBER_DIGITS = 15;
static constexpr int MAX_FORMULA_CHARS = 64;

static void format_result(double value, char *out, size_t size)
{
    if (!std::isfinite(value)) {
        snprintf(out, size, "Error");
        return;
    }
    if (value == 0) {
        value = 0; // Do not display negative zero.
    }
    snprintf(out, size, "%.15g", value);
    if (strchr(out, 'e') != nullptr) {
        snprintf(out, size, "%.9g", value);
    }
}

static const char *keyboard_map[] = {
    "C", "/", "x", LV_SYMBOL_BACKSPACE, "\n",
    "7", "8", "9", "-", "\n",
    "4", "5", "6", "+", "\n",
    "1", "2", "3", "%", "\n",
    "0", ".", "=", ""
};

namespace esp_brookesia::apps {

Calculator *Calculator::_instance = nullptr;

Calculator *Calculator::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new Calculator(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

Calculator::Calculator(bool use_status_bar, bool use_navigation_bar) :
   App(APP_NAME, &img_app_calculator, true, use_status_bar, use_navigation_bar)
{
}

Calculator::~Calculator()
{
    ESP_UTILS_LOGD("Destroy(@0x%p)", this);
}

bool Calculator::init(void)
{
    ESP_UTILS_LOGD("Init(@0x%p)", this);
    return true;
}

bool Calculator::deinit(void)
{
    ESP_UTILS_LOGD("Deinit(@0x%p)", this);
    return true;
}

bool Calculator::run(void)
{
    ESP_UTILS_LOGD("Run(@0x%p)", this);
    _is_starting = true;

    auto visual_area = getVisualArea();
    _width = lv_area_get_width(&visual_area);
    _height = lv_area_get_height(&visual_area);

    if (_width == 0 || _height == 0) {
        _width = 480;
        _height = 480;
    }

    formula_len = 1;
    _font_small = &lv_font_montserrat_24;
    _font_big = &lv_font_montserrat_40;

    // ESP32-S3-Touch-AMOLED-2.16 is a square panel, not a round display.
    // Keep its calculator in the fixed 4-column rectangular layout.
    const int keyboard_h = _height - CALCULATOR_DISPLAY_H - CALCULATOR_MARGIN * 2;
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    keyboard = lv_btnmatrix_create(screen);
    lv_btnmatrix_set_map(keyboard, keyboard_map);
    // The final row is 0 (two columns), decimal point and equals: a true 4-column grid.
    lv_btnmatrix_set_btn_width(keyboard, 16, 2);
    lv_obj_set_size(keyboard, _width - CALCULATOR_MARGIN * 2, keyboard_h);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -CALCULATOR_MARGIN);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_set_style_pad_all(keyboard, 0, 0);
    lv_obj_set_style_pad_row(keyboard, CALCULATOR_KEY_GAP, 0);
    lv_obj_set_style_pad_column(keyboard, CALCULATOR_KEY_GAP, 0);
    lv_obj_set_style_radius(keyboard, 20, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_30, LV_PART_ITEMS);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_ALL, this);
    lv_obj_t *history_obj = lv_obj_create(screen);
    lv_obj_add_flag(history_obj, LV_OBJ_FLAG_HIDDEN);
    history_label = lv_textarea_create(history_obj);

    lv_obj_t *formula_label_obj = lv_obj_create(screen);
    lv_obj_remove_style_all(formula_label_obj);
    lv_obj_set_size(formula_label_obj, _width - CALCULATOR_MARGIN * 2,
                    lv_font_get_line_height(_font_big));
    lv_obj_align(formula_label_obj, LV_ALIGN_TOP_MID, 0, 30);
    formula_label = lv_label_create(formula_label_obj);
    lv_obj_set_style_text_color(formula_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(formula_label, _font_big, 0);
    lv_label_set_text(formula_label, "0");
    lv_obj_align(formula_label, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *result_label_obj = lv_obj_create(screen);
    lv_obj_remove_style_all(result_label_obj);
    lv_obj_set_size(result_label_obj, _width - CALCULATOR_MARGIN * 2,
                    lv_font_get_line_height(_font_big));
    lv_obj_align(result_label_obj, LV_ALIGN_TOP_MID, 0, 84);
    result_label = lv_label_create(result_label_obj);
    lv_obj_set_style_text_color(result_label, CALCULATOR_RESULT_COLOR, 0);
    lv_obj_set_style_text_font(result_label, _font_small, 0);
    lv_label_set_text(result_label, "= 0");
    lv_obj_align(result_label, LV_ALIGN_RIGHT_MID, 0, 0);

    _is_starting = false;
    return true;
}

void Calculator::fitLabel(lv_obj_t *label)
{
    lv_point_t size;
    lv_text_get_size(&size, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN), 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int32_t width = lv_obj_get_width(lv_obj_get_parent(label));
    int32_t scale = LV_SCALE_NONE;
    if (size.x > width) {
        scale = LV_MAX(CALCULATOR_MIN_SCALE, width * LV_SCALE_NONE / size.x);
    }
    const bool clip_start = size.x * scale / LV_SCALE_NONE > width;
    lv_obj_set_style_transform_scale(label, scale, 0);
    lv_obj_set_style_transform_pivot_x(label, clip_start ? lv_pct(100) : lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(label, lv_pct(50), 0);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, 0, 0);
}

void Calculator::drawKey(lv_event_t *e)
{
    lv_draw_task_t *task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = static_cast<lv_draw_dsc_base_t *>(lv_draw_task_get_draw_dsc(task));
    if (base->part != LV_PART_ITEMS) {
        return;
    }
    lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);
    if (fill == nullptr) {
        return;
    }
    lv_obj_t *keyboard = lv_event_get_target_obj(e);
    const int id = base->id1;
    const bool pressed = lv_btnmatrix_get_selected_btn(keyboard) == base->id1 &&
                         lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_PRESSED);
    lv_color_t color = CALCULATOR_DIGIT_COLOR;
    if (id == 0 || id == 3) {
        color = CALCULATOR_FUNCTION_COLOR;
    } else if (id == 1 || id == 2 || id == 7 || id == 11 || id == 15 || id == 18) {
        color = CALCULATOR_OPERATOR_COLOR;
    }
    fill->color = pressed ? lv_color_lighten(color, LV_OPA_30) : color;
}

bool Calculator::back(void)
{
    ESP_UTILS_LOGD("Back(@0x%p)", this);

    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");

    return true;
}

bool Calculator::close(void)
{
    ESP_UTILS_LOGD("Close(@0x%p)", this);
    _is_stopping = true;

    _is_stopping = false;
    return true;
}

bool Calculator::isStartZero(void)
{
    const char *text = lv_label_get_text(formula_label);
    int text_len = strlen(text);

    if ((text_len == 1) && (text[0] == '0')) {
        // LOG_I(TAG, "start 0");
        return true;
    }
    if ((text_len >= 2) && (text[text_len - 1] == '0') &&
            !isdigit(static_cast<unsigned char>(text[text_len - 2])) && (text[text_len - 2] != '.')) {
        // LOG_I(TAG, "start 0");
        return true;
    }

    return false;
}

bool Calculator::isStartNum(void)
{
    const char *text = lv_label_get_text(formula_label);
    int text_len = strlen(text);

    if ((text[text_len - 1] >= '0') && (text[text_len - 1] <= '9')) {
        // LOG_I(TAG, "start num");
        return true;
    }

    return false;
}

bool Calculator::isStartPercent(void)
{
    const char *text = lv_label_get_text(formula_label);
    int text_len = strlen(text);

    if (text[text_len - 1] == '%') {
        // LOG_I(TAG, "start %");
        return true;
    }

    return false;
}

int Calculator::numberDigits(void)
{
    const char *text = lv_label_get_text(formula_label);
    int digits = 0;
    for (int i = strlen(text) - 1; i >= 0 &&
            (isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.'); --i) {
        digits += text[i] != '.';
    }
    return digits;
}

bool Calculator::isLegalDot(void)
{
    const char *text = lv_label_get_text(formula_label);
    int text_len = strlen(text);

    while (text_len-- > 0) {
        if (text[text_len] == '.') {
            // LOG_I(TAG, "illegal dot");
            return false;
        } else if ((text[text_len] < '0') ||
                   (text[text_len] > '9')) {
            return true;
        }
    }

    return true;
}

double Calculator::calculate(const char *input)
{
    std::vector<double> stk;
    int input_len = strlen(input);
    double num = 0;
    bool dot_flag = false;
    int dot_len = 0;
    char pre_sign = '+';

    for (int i = 0; i < input_len; i++) {
        if (input[i] == '.') {
            dot_flag = true;
            dot_len = 0;
        } else if (isdigit(input[i])) {
            if (!dot_flag) {
                num = num * 10 + input[i] - '0';
            } else {
                num += (input[i] - '0') / pow(10.0, ++dot_len);
            }
        } else if (input[i] == '%') {
            num /= 100.0;
        } else if (i != input_len - 1) {
            dot_flag = false;
            dot_len = 0;
            switch (pre_sign) {
            case '+':
                stk.push_back(num);
                break;
            case '-':
                stk.push_back(-num);
                break;
            case 'x':
                stk.back() *= num;
                break;
            default:
                if (num != 0) {
                    stk.back() /= num;
                } else {
                    return NAN;
                }
            }
            num = 0;
            pre_sign = input[i];
        }

        if (i == input_len - 1) {
            switch (pre_sign) {
            case '+':
                stk.push_back(num);
                break;
            case '-':
                stk.push_back(-num);
                break;
            case 'x':
                stk.back() *= num;
                break;
            default:
                if (num != 0) {
                    stk.back() /= num;
                } else {
                    return NAN;
                }
            }
            num = 0;
        }
    }

    for (int i = 0; i < stk.size(); i++) {
        num += stk.at(i);
    }
    // LOG_I(TAG, "cal: %s = %f", input, num);

    return num;
}

void Calculator::keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    Calculator *app = (Calculator *)lv_event_get_user_data(e);

    if (code == LV_EVENT_DRAW_TASK_ADDED) {
        app->drawKey(e);
        return;
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *matrix = lv_event_get_target_obj(e);
        const int index = lv_btnmatrix_get_selected_btn(matrix);
        const char *btn_text = lv_btnmatrix_get_btn_text(matrix, index);
        const int btn_id = index;
        bool calculate_flag = false;
        bool equal_flag = false;
        double res_num;
        char res_str[32];
        char history_str[MAX_FORMULA_CHARS + sizeof(res_str) + 8];

        if (lv_obj_get_style_text_font(app->formula_label, LV_PART_MAIN) == app->_font_small) {
            lv_obj_set_style_text_font(app->formula_label, app->_font_big, 0);
            lv_obj_set_style_text_font(app->result_label, app->_font_small, 0);
        }

        switch (btn_id) {
        // "C"
        case 0:
            lv_label_set_text(app->formula_label, "0");
            app->formula_len = 1;
            calculate_flag = true;
            break;
        // "<"
        case 3:
            if ((app->formula_len == 1) && app->isStartZero()) {
                break;
            }
            lv_label_cut_text(
                app->formula_label,
                --(app->formula_len),
                1
            );
            if (app->formula_len == 0) {
                lv_label_set_text(app->formula_label, "0");
                app->formula_len = 1;
            }
            calculate_flag = true;
            break;
        // "="
        case 18:
            calculate_flag = true;
            equal_flag = true;
            break;
        // "/,X,-,+,%"
        case 1:
        case 2:
        case 7:
        case 11:
        case 15:
            if (app->formula_len >= MAX_FORMULA_CHARS) {
                break;
            }
            // Need number or percent
            if (app->isStartPercent() ||
                    app->isStartNum()) {
                // Firstly input "+, -"
                if (((btn_id == 7) || (btn_id == 11)) &&
                        (app->isStartZero())) {
                    // Remove "0"
                    lv_label_cut_text(
                        app->formula_label,
                        --(app->formula_len),
                        1
                    );
                }
                lv_label_ins_text(
                    app->formula_label,
                    app->formula_len++,
                    btn_text
                );
                if (btn_id == 15) {
                    calculate_flag = true;
                }
            }
            break;
        // "1234567890"
        case 4:
        case 5:
        case 6:
        case 8:
        case 9:
        case 10:
        case 12:
        case 13:
        case 14:
        case 16:
            if (app->formula_len >= MAX_FORMULA_CHARS || app->numberDigits() >= MAX_NUMBER_DIGITS) {
                break;
            }
            // Remove First "0"
            if (app->isStartZero()) {
                lv_label_cut_text(
                    app->formula_label,
                    --(app->formula_len),
                    1
                );
            }
            // Except "%"
            if (!app->isStartPercent()) {
                lv_label_ins_text(
                    app->formula_label,
                    app->formula_len++,
                    btn_text
                );
                calculate_flag = true;
            }
            break;
        // "."
        case 17:
            if (app->formula_len < MAX_FORMULA_CHARS && app->isLegalDot() && app->isStartNum()) {
                lv_label_ins_text(
                    app->formula_label,
                    app->formula_len++,
                    "."
                );
            }
            break;
        default:
            break;
        }

        if (calculate_flag) {
            lv_obj_set_style_text_font(app->formula_label, app->_font_big, 0);

            res_num = app->calculate(lv_label_get_text(app->formula_label));
            format_result(res_num, res_str, sizeof(res_str));
            lv_label_set_text_fmt(app->result_label, "= %s", res_str);
            lv_obj_set_style_text_font(app->result_label, app->_font_small, 0);
        }

        if (equal_flag) {
            lv_obj_set_style_text_font(app->result_label, app->_font_big, 0);

            snprintf(history_str, sizeof(history_str), "\n%.64s = %.31s ",
                     lv_label_get_text(app->formula_label), res_str);
            lv_textarea_set_cursor_pos(app->history_label, strlen(lv_textarea_get_text(app->history_label)));
            lv_textarea_add_text(app->history_label, history_str);

            const bool can_continue = strchr(res_str, 'e') == nullptr && strcmp(res_str, "Error") != 0;
            lv_label_set_text_fmt(app->formula_label, "%s", can_continue ? res_str : "0");
            lv_obj_set_style_text_font(app->formula_label, app->_font_small, 0);
            app->formula_len = strlen(lv_label_get_text(app->formula_label));
        }

        const bool result_big = lv_obj_get_style_text_font(app->result_label, LV_PART_MAIN) == app->_font_big;
        lv_obj_set_style_text_color(app->formula_label, result_big ? CALCULATOR_RESULT_COLOR : lv_color_white(), 0);
        lv_obj_set_style_text_color(app->result_label, result_big ? lv_color_white() : CALCULATOR_RESULT_COLOR, 0);
        app->fitLabel(app->formula_label);
        app->fitLabel(app->result_label);
    }
}

}
