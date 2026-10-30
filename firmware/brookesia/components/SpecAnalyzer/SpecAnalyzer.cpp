#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "esp_task.h"
#include "esp_heap_caps.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:SpecAnalyzer"
#include "esp_lib_utils.h"
#include "SpecAnalyzer.hpp"
#define APP_NAME "SpecAnalyzer"

using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(img_app_specanalyzer);

namespace {

constexpr uint32_t SPECTRUM_REFRESH_PERIOD_MS = 40;
constexpr uint32_t WORKER_JOIN_TIMEOUT_MS = 1500;
constexpr const char *STYLE_NAMES[] = {"Mic Test", "Bars", "Radial", "Halo"};
constexpr float PI = 3.14159265358979323846f;
constexpr uint32_t LABEL_UPDATE_FRAMES = 5;
constexpr uint32_t NO_DATA_HOPS = 30;
constexpr int MIC_PANEL_TOP[] = {118, 262};
constexpr int MIC_PANEL_HEIGHT = 90;

constexpr int W = BSP_LCD_H_RES;
constexpr int H = BSP_LCD_V_RES;
constexpr uint32_t OPA_MAX = 32;

inline uint16_t blend565(uint16_t bg, uint16_t fg, uint32_t alpha)
{
    const uint32_t b = (bg | (static_cast<uint32_t>(bg) << 16)) & 0x07E0F81F;
    const uint32_t f = (fg | (static_cast<uint32_t>(fg) << 16)) & 0x07E0F81F;
    const uint32_t r = ((((f - b) * alpha) >> 5) + b) & 0x07E0F81F;
    return static_cast<uint16_t>(r | (r >> 16));
}

inline void plot(uint16_t *fb, int x, int y, uint16_t color, uint32_t alpha)
{
    uint16_t *pixel = fb + y * W + x;
    *pixel = alpha >= OPA_MAX ? color : blend565(*pixel, color, alpha);
}

inline void fill_span(uint16_t *fb, int y, int x1, int x2, uint16_t color)
{
    if (y < 0 || y >= H) {
        return;
    }
    x1 = std::max(x1, 0);
    x2 = std::min(x2, W - 1);
    uint16_t *pixel = fb + y * W + x1;
    for (int x = x1; x <= x2; ++x) {
        *pixel++ = color;
    }
}

void fill_rect(uint16_t *fb, int x1, int y1, int x2, int y2, uint16_t color)
{
    for (int y = std::max(y1, 0); y <= std::min(y2, H - 1); ++y) {
        fill_span(fb, y, x1, x2, color);
    }
}

void aa_disc(uint16_t *fb, float cx, float cy, float radius, uint16_t color)
{
    const float reach = radius + 0.5f;
    const float solid = fmaxf(0.0f, radius - 0.5f);
    for (int y = std::max(0, static_cast<int>(floorf(cy - reach)));
         y <= std::min(H - 1, static_cast<int>(ceilf(cy + reach))); ++y) {
        const float dy = y - cy;
        const float outer = sqrtf(fmaxf(0.0f, reach * reach - dy * dy));
        const float inner = fabsf(dy) < solid ? sqrtf(solid * solid - dy * dy) : -1.0f;
        int fill_left = W;
        int fill_right = -1;
        if (inner >= 0.0f) {
            fill_left = static_cast<int>(ceilf(cx - inner));
            fill_right = static_cast<int>(floorf(cx + inner));
            fill_span(fb, y, fill_left, fill_right, color);
        }
        for (int x = std::max(0, static_cast<int>(floorf(cx - outer)));
             x <= std::min(W - 1, static_cast<int>(ceilf(cx + outer))); ++x) {
            if (x >= fill_left && x <= fill_right) {
                x = fill_right;
                continue;
            }
            const float cover = reach - sqrtf((x - cx) * (x - cx) + dy * dy);
            if (cover > 0.0f) {
                plot(fb, x, y, color, static_cast<uint32_t>(fminf(cover, 1.0f) * OPA_MAX));
            }
        }
    }
}

void aa_line(uint16_t *fb, float x0, float y0, float x1, float y1, float width, uint16_t color)
{
    const float reach = width * 0.5f + 0.5f;
    const float reach2 = reach * reach;
    const float solid = fmaxf(0.0f, reach - 1.0f);
    const float solid2 = solid * solid;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    const float inv_len2 = len2 > 1e-6f ? 1.0f / len2 : 0.0f;
    const float len = sqrtf(len2);
    const float ux = len > 1e-3f ? dx / len : 1.0f;
    const float uy = len > 1e-3f ? dy / len : 0.0f;
    const float inv_uy = fabsf(uy) > 1e-3f ? 1.0f / fabsf(uy) : 0.0f;
    const int bx1 = std::max(0, static_cast<int>(floorf(fminf(x0, x1) - reach)));
    const int bx2 = std::min(W - 1, static_cast<int>(ceilf(fmaxf(x0, x1) + reach)));
    const int by1 = std::max(0, static_cast<int>(floorf(fminf(y0, y1) - reach)));
    const int by2 = std::min(H - 1, static_cast<int>(ceilf(fmaxf(y0, y1) + reach)));
    for (int y = by1; y <= by2; ++y) {
        const float ry = y - y0;
        int xs = bx1;
        int xe = bx2;
        if (inv_uy > 0.0f) {
            const float center = x0 + ry * ux / uy;
            const float half = reach * inv_uy;
            xs = std::max(xs, static_cast<int>(floorf(center - half)));
            xe = std::min(xe, static_cast<int>(ceilf(center + half)));
        }
        uint16_t *pixel = fb + y * W + xs;
        for (int x = xs; x <= xe; ++x, ++pixel) {
            const float rx = x - x0;
            const float t = fmaxf(0.0f, fminf(1.0f, (rx * dx + ry * dy) * inv_len2));
            const float ex = t * dx - rx;
            const float ey = t * dy - ry;
            const float distance2 = ex * ex + ey * ey;
            if (distance2 < reach2) {
                *pixel = distance2 <= solid2 ? color : blend565(*pixel, color,
                         static_cast<uint32_t>(fminf(reach - sqrtf(distance2), 1.0f) * OPA_MAX));
            }
        }
    }
}

} // namespace

namespace esp_brookesia::apps {

SpecAnalyzer *SpecAnalyzer::_instance = nullptr;

SpecAnalyzer *SpecAnalyzer::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new SpecAnalyzer(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

SpecAnalyzer::SpecAnalyzer(bool use_status_bar, bool use_navigation_bar) :
    App(APP_NAME, &img_app_specanalyzer, true, use_status_bar, use_navigation_bar),
    _canvas(nullptr),
    _style_label(nullptr),
    _mic_labels{},
    _page_dots{},
    _style(Style::MicTest),
    _timer(nullptr),
    _audio_task_handle(nullptr),
    _capture_requested(false),
    _worker_exit(false),
    _worker_stopped(true),
    _codec_released(true),
    _codec_release_result(ESP_OK),
    _audio_session_acquired(false),
    _capture_state(CaptureState::Idle),
    _shown_capture_state(CaptureState::Idle),
    _draw_buf(nullptr)
{
    portMUX_INITIALIZE(&_spectrum_mux);

    // Clear DSP state so the first rendered frame starts from silence.
    std::memset(_raw_data, 0, sizeof(_raw_data));
    std::memset(_audio_buffer, 0, sizeof(_audio_buffer));
    std::memset(_fft_buffer, 0, sizeof(_fft_buffer));
    std::memset(_spectrum, 0, sizeof(_spectrum));
    std::memset(_display_spectrum, 0, sizeof(_display_spectrum));
    std::memset(_smooth_spectrum, 0, sizeof(_smooth_spectrum));
    std::memset(_published_spectrum, 0, sizeof(_published_spectrum));
    std::memset(_render_spectrum, 0, sizeof(_render_spectrum));
    std::memset(_level, 0, sizeof(_level));
    std::memset(_visual_peak, 0, sizeof(_visual_peak));
    for (int channel = 0; channel < MIC_COUNT; ++channel) {
        _rms_db[channel] = -90.0f;
        _peak_dbfs[channel] = -90.0f;
        _shown_rms_db[channel] = -90.0f;
        _zero_hops[channel] = 0;
    }
    for (int band = 0; band < STRIPE_COUNT; ++band) {
        const uint16_t cyan_hue = static_cast<uint16_t>(190 + band * 140 / STRIPE_COUNT) % 360;
        _band_color[0][band] = lv_color_to_u16(lv_color_hsv_to_rgb(cyan_hue, 100, 100));
        _band_color[1][band] = lv_color_to_u16(lv_color_hsv_to_rgb(15 + band * 40 / STRIPE_COUNT, 100, 100));
        _band_dim[band] = lv_color_to_u16(lv_color_hsv_to_rgb(cyan_hue, 100, 55));
        _band_faint[band] = lv_color_to_u16(lv_color_hsv_to_rgb(cyan_hue, 100, 28));
    }
}

SpecAnalyzer::~SpecAnalyzer()
{
    const bool deinit_ok = deinit();
    if (!deinit_ok && !_worker_stopped.load()) {
        // This is a process-lifetime singleton, so destruction is exceptional.
        // Never free its storage while the worker can still dereference it and
        // never force-delete a task which may own an I2S/codec lock.
        ESP_UTILS_LOGE("Waiting for the audio worker before destroying the singleton");
        (void)stopAudioTask(portMAX_DELAY);
    }
    destroyUi();
    _instance = nullptr;
}

bool SpecAnalyzer::stopAudioTask(TickType_t timeout_ticks)
{
    _capture_requested.store(false);
    _worker_exit.store(true);

    TaskHandle_t task = _audio_task_handle.load();
    if (task == xTaskGetCurrentTaskHandle()) {
        ESP_UTILS_LOGE("Audio worker cannot join itself");
        return false;
    }
    if (task) {
        xTaskNotifyGive(task);
    }
    if (_worker_stopped.load()) {
        return true;
    }

    const TickType_t start_tick = xTaskGetTickCount();
    while (!_worker_stopped.load()) {
        if (timeout_ticks != portMAX_DELAY &&
            (xTaskGetTickCount() - start_tick) >= timeout_ticks) {
            ESP_UTILS_LOGE("Timed out waiting for the audio worker to exit");
            return false;
        }
        vTaskDelay(1);
    }
    return true;
}

bool SpecAnalyzer::releaseCodec(CaptureState success_state)
{
    esp_err_t result = ESP_OK;
    if (_audio_session_acquired.load()) {
        result = bsp_extra_codec_dev_stop();
        if (result == ESP_OK) {
            result = bsp_extra_audio_session_release(BSP_EXTRA_AUDIO_OWNER_SPEC_ANALYZER);
            if (result == ESP_OK) {
                _audio_session_acquired.store(false);
            }
        }
    }
    _codec_release_result.store(result);
    if (result == ESP_OK && !_audio_session_acquired.load()) {
        _codec_released.store(true);
        _capture_state.store(success_state);
        return true;
    }

    _capture_requested.store(false);
    _codec_released.store(false);
    _capture_state.store(CaptureState::Error);
    ESP_UTILS_LOGE("Release audio codec failed: %s", esp_err_to_name(result));
    return false;
}

void SpecAnalyzer::destroyUi(void)
{
    if (_timer) {
        lv_timer_del(_timer);
        _timer = nullptr;
    }
    for (auto &label : _mic_labels) {
        if (label) {
            lv_obj_del(label);
            label = nullptr;
        }
    }
    if (_style_label) {
        lv_obj_del(_style_label);
        _style_label = nullptr;
    }
    if (_page_dots[0]) {
        lv_obj_del(lv_obj_get_parent(_page_dots[0]));
    }
    std::memset(_page_dots, 0, sizeof(_page_dots));
    if (_canvas) {
        lv_obj_del(_canvas);
        _canvas = nullptr;
    }
    if (_draw_buf) {
        heap_caps_free(_draw_buf);
        _draw_buf = nullptr;
    }
}

bool SpecAnalyzer::ensureAudioTask(void)
{
    if (_audio_task_handle.load() != nullptr) {
        return !_worker_exit.load() && !_worker_stopped.load();
    }
    if (_worker_exit.load() || !_worker_stopped.load()) {
        return false;
    }

    TaskHandle_t task = nullptr;
    _worker_stopped.store(false);
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        audio_fft_task,
        "audio_fft",
        8 * 1024,
        this,
        5,
        &task,
        PRO_CPU_NUM
    );
    if (task_ret != pdPASS) {
        _worker_stopped.store(true);
        ESP_UTILS_LOGE("Audio FFT task creation failed");
        return false;
    }

    _audio_task_handle.store(task);
    return true;
}

bool SpecAnalyzer::run(void)
{
    ESP_UTILS_LOGD("Run");

    if (_worker_exit.load() || !_codec_released.load() ||
        _codec_release_result.load() != ESP_OK) {
        ESP_UTILS_LOGE("Audio lifecycle is not ready for a new capture session");
        return false;
    }

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Prefer PSRAM for the large canvas buffer and fall back to SRAM if PSRAM is exhausted.
    // LVGL 9's lv_color_t is RGB888, so an RGB565 canvas must explicitly use uint16_t pixels.
    constexpr size_t pixel_count = CANVAS_WIDTH * CANVAS_HEIGHT;
    constexpr size_t buf_size = pixel_count * sizeof(uint16_t);
    _draw_buf = static_cast<uint16_t *>(
        heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );

    if (!_draw_buf) {
        ESP_UTILS_LOGE("PSRAM allocation failed! Using fallback SRAM");
        _draw_buf = static_cast<uint16_t *>(heap_caps_malloc(buf_size, MALLOC_CAP_8BIT));
    }

    if (!_draw_buf) {
        ESP_UTILS_LOGE("Canvas buffer allocation failed!");
        return false;
    }
    std::fill_n(_draw_buf, pixel_count, lv_color_to_u16(lv_color_black()));
    std::memset(_visual_peak, 0, sizeof(_visual_peak));

    // The GUI timer directly rasterizes into this RGB565 buffer. This avoids creating
    // hundreds of LVGL draw tasks per frame and waiting for them in the timer callback.
    _canvas = lv_canvas_create(lv_scr_act());
    if (!_canvas) {
        ESP_UTILS_LOGE("Canvas creation failed!");
        heap_caps_free(_draw_buf);
        _draw_buf = nullptr;
        return false;
    }
    lv_obj_set_size(_canvas, CANVAS_WIDTH, CANVAS_HEIGHT);
    lv_obj_align(_canvas, LV_ALIGN_CENTER, 0, 0);
    lv_canvas_set_buffer(_canvas, _draw_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_add_flag(_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(_canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(_canvas, canvas_event_cb, LV_EVENT_GESTURE, this);

    for (int channel = 0; channel < MIC_COUNT; ++channel) {
        _mic_labels[channel] = lv_label_create(scr);
        lv_obj_set_style_text_font(_mic_labels[channel], &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_align(_mic_labels[channel], LV_ALIGN_TOP_MID, 0, MIC_PANEL_TOP[channel] - 34);
        lv_obj_add_flag(_mic_labels[channel], LV_OBJ_FLAG_HIDDEN);
    }

    _style_label = lv_label_create(scr);
    lv_obj_set_style_text_font(_style_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(_style_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_style_label, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_style_label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(_style_label, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(_style_label, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(_style_label, 6, LV_PART_MAIN);
    lv_obj_align(_style_label, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *dots = lv_obj_create(scr);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 8, LV_PART_MAIN);
    lv_obj_remove_flag(dots, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -64);
    for (int i = 0; i < static_cast<int>(Style::Max); ++i) {
        _page_dots[i] = lv_obj_create(dots);
        lv_obj_remove_style_all(_page_dots[i]);
        lv_obj_set_size(_page_dots[i], 8, 8);
        lv_obj_set_style_radius(_page_dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(_page_dots[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_page_dots[i], LV_OPA_40, LV_PART_MAIN);
        lv_obj_remove_flag(_page_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }
    setStyle(static_cast<int>(Style::MicTest));

    // One 1024-sample capture at 16 kHz arrives about every 64 ms, so refreshing
    // faster only redraws duplicate data and starves the LVGL event loop.
    _timer = lv_timer_create(timer_cb, SPECTRUM_REFRESH_PERIOD_MS, this);
    if (!_timer) {
        ESP_UTILS_LOGE("Spectrum timer creation failed!");
        close();
        return false;
    }

    // DSP and codec setup can allocate memory, generate tables and reopen I2S.
    // Keep all of it off the LVGL event thread so opening the app remains responsive.
    if (!ensureAudioTask()) {
        close();
        return false;
    }
    _shown_capture_state = CaptureState::Starting;
    _capture_state.store(CaptureState::Starting);
    _capture_requested.store(true);
    xTaskNotifyGive(_audio_task_handle.load());

    return true;
}

bool SpecAnalyzer::back(void)
{
    ESP_UTILS_LOGD("Back");
    notifyCoreClosed();
    return true;
}

bool SpecAnalyzer::close(void)
{
    ESP_UTILS_LOGD("Close");

    if (!pause()) {
        return false;
    }
    destroyUi();

    return true;
}

bool SpecAnalyzer::init()
{
    if (!_worker_stopped.load() || _audio_task_handle.load() != nullptr) {
        ESP_UTILS_LOGE("Cannot initialize while the previous audio worker is still active");
        return false;
    }
    if (!_codec_released.load() || _codec_release_result.load() != ESP_OK) {
        ESP_UTILS_LOGE("Cannot initialize because the previous codec release failed");
        return false;
    }

    _capture_requested.store(false);
    _worker_exit.store(false);
    _capture_state.store(CaptureState::Idle);
    _shown_capture_state = CaptureState::Idle;
    return true;
}

bool SpecAnalyzer::deinit()
{
    ESP_UTILS_LOGD("Deinit");

    const bool close_ok = close();
    const bool worker_ok = stopAudioTask(pdMS_TO_TICKS(WORKER_JOIN_TIMEOUT_MS));

    // An uninstall must not leave LVGL resources pointing at this app even if
    // the codec reports a release failure. The worker never accesses the UI.
    destroyUi();

    const bool codec_ok = _codec_released.load() && _codec_release_result.load() == ESP_OK;
    if (!close_ok && worker_ok && codec_ok) {
        ESP_UTILS_LOGW("Initial close timed out, but deinit completed the codec hand-off");
    }
    return worker_ok && codec_ok;
}
bool SpecAnalyzer::pause()
{
    if (_timer) {
        lv_timer_pause(_timer);
    }

    CaptureState state = _capture_state.load();
    _capture_requested.store(false);
    if (state == CaptureState::Starting || state == CaptureState::Running) {
        _capture_state.store(CaptureState::Stopping);
    }

    TaskHandle_t task = _audio_task_handle.load();
    if (task) {
        xTaskNotifyGive(task);
    }

    // Reads are split into 256-frame chunks, so the worker normally observes the
    // request in about 16 ms. A terminal state alone is insufficient: the codec
    // close result is the hand-off acknowledgement for the next audio owner.
    auto codec_release_confirmed = [this]() {
        const CaptureState current_state = _capture_state.load();
        const bool terminal = current_state == CaptureState::Idle ||
                              current_state == CaptureState::Error;
        return terminal && _codec_released.load() &&
               _codec_release_result.load() == ESP_OK;
    };

    const TickType_t release_started = xTaskGetTickCount();
    bool stopped = codec_release_confirmed();
    while (!stopped &&
           xTaskGetTickCount() - release_started < pdMS_TO_TICKS(WORKER_JOIN_TIMEOUT_MS)) {
        vTaskDelay(pdMS_TO_TICKS(5));
        stopped = codec_release_confirmed();
    }

    if (!stopped) {
        ESP_UTILS_LOGE(
            "Microphone codec release was not acknowledged: %s",
            esp_err_to_name(_codec_release_result.load())
        );
        return false;
    }

    _shown_capture_state = CaptureState::Idle;
    return true;
}

bool SpecAnalyzer::resume()
{
    if (_capture_requested.load()) {
        if (_worker_exit.load() || _worker_stopped.load() ||
            _audio_task_handle.load() == nullptr) {
            ESP_UTILS_LOGE("Capture is requested without a live audio worker");
            return false;
        }
        if (_timer) {
            lv_timer_resume(_timer);
        }
        return true;
    }

    if (_worker_exit.load() || !_codec_released.load() ||
        _codec_release_result.load() != ESP_OK) {
        ESP_UTILS_LOGE("Cannot resume before the previous codec release succeeds");
        return false;
    }

    if (!ensureAudioTask()) {
        return false;
    }

    _shown_capture_state = CaptureState::Starting;
    _capture_state.store(CaptureState::Starting);
    _capture_requested.store(true);
    xTaskNotifyGive(_audio_task_handle.load());
    if (_timer) {
        lv_timer_resume(_timer);
    }
    return true;
}

void SpecAnalyzer::audio_fft_task(void *pvParameters)
{
    SpecAnalyzer *app = static_cast<SpecAnalyzer*>(pvParameters);
    constexpr size_t expected_bytes = N_SAMPLES * CAPTURE_CHANNELS * sizeof(int16_t);
    constexpr size_t read_frames = 256;
    constexpr size_t read_chunk_bytes = read_frames * CAPTURE_CHANNELS * sizeof(int16_t);
    constexpr size_t read_chunk_count = N_SAMPLES / read_frames;
    static_assert(N_SAMPLES % read_frames == 0, "FFT frame must contain whole I2S read chunks");
    constexpr size_t mic_channel_indices[MIC_COUNT] = {
        MIC1_CHANNEL_INDEX,
        MIC2_CHANNEL_INDEX,
    };
    bool dsp_ready = false;

    while (!app->_worker_exit.load()) {
        while (!app->_capture_requested.load() && !app->_worker_exit.load()) {
            const CaptureState state = app->_capture_state.load();
            if (state == CaptureState::Starting || state == CaptureState::Stopping) {
                app->_capture_state.store(CaptureState::Idle);
            }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        if (app->_worker_exit.load()) {
            break;
        }

        app->_capture_state.store(CaptureState::Starting);

        if (!dsp_ready) {
            // On ESP32-S3 a 1024-point table uses ESP-DSP's built-in table. Asking
            // for CONFIG_DSP_MAX_FFT_SIZE (4096 here) needlessly allocates and builds
            // a much larger table while the application is opening.
            esp_err_t ret = dsps_fft2r_init_fc32(nullptr, N_SAMPLES);
            if (ret != ESP_OK || dsps_fft_w_table_size < N_SAMPLES) {
                ESP_UTILS_LOGE(
                    "FFT init failed: ret=%s, table=%d",
                    esp_err_to_name(ret),
                    dsps_fft_w_table_size
                );
                app->_capture_requested.store(false);
                app->_codec_release_result.store(ESP_OK);
                app->_codec_released.store(true);
                app->_capture_state.store(CaptureState::Error);
                continue;
            }
            dsps_wind_hann_f32(app->_wind, N_SAMPLES);
            dsp_ready = true;
            ESP_UTILS_LOGI("1024-point FFT and Hann window initialized");
        }

        if (!app->_capture_requested.load()) {
            app->_capture_state.store(CaptureState::Idle);
            continue;
        }

        // From this point until releaseCodec() succeeds, pause/close must not
        // report that the shared codec is available to another application.
        app->_codec_released.store(false);
        app->_codec_release_result.store(ESP_ERR_INVALID_STATE);

        esp_err_t ret =
            bsp_extra_audio_session_acquire(BSP_EXTRA_AUDIO_OWNER_SPEC_ANALYZER);
        if (ret == ESP_OK) {
            app->_audio_session_acquired.store(true);
        }
        if (ret == ESP_OK) {
            ret = bsp_extra_codec_init();
        }
        if (ret == ESP_OK && app->_capture_requested.load()) {
            ret = bsp_extra_codec_set_voice_fs(
                CODEC_VOICE_SAMPLE_RATE,
                CODEC_DEFAULT_BIT_WIDTH,
                TDM_SLOT_COUNT,
                CAPTURE_TDM_SLOT_MASK,
                MIC_GAIN_MASK
            );
        }

        if (ret != ESP_OK) {
            ESP_UTILS_LOGE("Configure dual TDM microphone capture failed: %s", esp_err_to_name(ret));
            app->_capture_requested.store(false);
            (void)app->releaseCodec(CaptureState::Error);
            continue;
        }

        if (!app->_capture_requested.load()) {
            (void)app->releaseCodec(CaptureState::Idle);
            continue;
        }

        std::memset(app->_display_spectrum, 0, sizeof(app->_display_spectrum));
        std::memset(app->_smooth_spectrum, 0, sizeof(app->_smooth_spectrum));
        portENTER_CRITICAL(&app->_spectrum_mux);
        std::memset(app->_published_spectrum, 0, sizeof(app->_published_spectrum));
        portEXIT_CRITICAL(&app->_spectrum_mux);

        app->_capture_state.store(CaptureState::Running);
        TickType_t last_slot_log_tick = xTaskGetTickCount();
        bool capture_failed = false;

        while (app->_capture_requested.load() && !app->_worker_exit.load()) {
            size_t bytes_read = 0;
            bool frame_read_failed = false;
            for (size_t chunk = 0; chunk < read_chunk_count; ++chunk) {
                size_t chunk_bytes_read = 0;
                ret = bsp_extra_i2s_read(
                    app->_raw_data + chunk * read_frames * CAPTURE_CHANNELS,
                    read_chunk_bytes,
                    &chunk_bytes_read,
                    portMAX_DELAY
                );
                bytes_read += chunk_bytes_read;

                // Short chunks limit pause latency while audio is arriving. If
                // it is not, the esp_codec_dev v1.5.11 read can still wait up
                // to its fixed 1000 ms before this worker observes the stop.
                if (!app->_capture_requested.load() || app->_worker_exit.load()) {
                    break;
                }
                if (ret != ESP_OK || chunk_bytes_read < read_chunk_bytes) {
                    if (ret == ESP_ERR_INVALID_STATE) {
                        ESP_UTILS_LOGW("I2S recorder became unavailable");
                        capture_failed = true;
                    } else {
                        ESP_UTILS_LOGW(
                            "I2S read error: %s, bytes: %u",
                            esp_err_to_name(ret),
                            static_cast<unsigned>(chunk_bytes_read)
                        );
                    }
                    frame_read_failed = true;
                    break;
                }
            }

            if (!app->_capture_requested.load() || app->_worker_exit.load()) {
                break;
            }
            if (capture_failed) {
                break;
            }
            if (frame_read_failed || bytes_read < expected_bytes) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            TickType_t now = xTaskGetTickCount();
            if ((now - last_slot_log_tick) >= pdMS_TO_TICKS(2000)) {
                int32_t channel_peaks[CAPTURE_CHANNELS] = {};
                for (size_t frame = 0; frame < N_SAMPLES; ++frame) {
                    for (size_t channel = 0; channel < CAPTURE_CHANNELS; ++channel) {
                        int32_t sample = static_cast<int32_t>(
                            app->_raw_data[frame * CAPTURE_CHANNELS + channel]
                        );
                        int32_t magnitude = sample < 0 ? -sample : sample;
                        if (magnitude > channel_peaks[channel]) {
                            channel_peaks[channel] = magnitude;
                        }
                    }
                }
                ESP_UTILS_LOGI(
                    "TDM peaks [MIC1, MIC3(ref), MIC2, MIC4]: %d, %d, %d, %d",
                    static_cast<int>(channel_peaks[0]),
                    static_cast<int>(channel_peaks[1]),
                    static_cast<int>(channel_peaks[2]),
                    static_cast<int>(channel_peaks[3])
                );
                last_slot_log_tick = now;
            }

            const size_t samples_read = bytes_read / sizeof(int16_t);
            for (int ch = 0; ch < MIC_COUNT; ++ch) {
                float sum_squares = 0.0f;
                int max_abs = 0;
                for (int i = 0; i < N_SAMPLES; ++i) {
                    const size_t sample_index = i * CAPTURE_CHANNELS + mic_channel_indices[ch];
                    const int16_t sample = sample_index < samples_read ? app->_raw_data[sample_index] : 0;
                    app->_audio_buffer[ch][i] = sample / 32768.0f;
                    sum_squares += static_cast<float>(sample) * sample;
                    max_abs = std::max(max_abs, std::abs(static_cast<int>(sample)));
                }
                app->_rms_db[ch] = 20.0f * log10f(sqrtf(sum_squares / N_SAMPLES) / 32768.0f + 1e-9f);
                app->_peak_dbfs[ch] = 20.0f * log10f(max_abs / 32768.0f + 1e-9f);
                app->_zero_hops[ch] = max_abs == 0 ? app->_zero_hops[ch] + 1 : 0;
            }

            for (int ch = 0; ch < MIC_COUNT; ++ch) {
                dsps_mul_f32(
                    app->_audio_buffer[ch],
                    app->_wind,
                    app->_audio_buffer[ch],
                    N_SAMPLES,
                    1,
                    1,
                    1
                );

                for (int i = 0; i < N_SAMPLES; ++i) {
                    app->_fft_buffer[ch][2 * i] = app->_audio_buffer[ch][i];
                    app->_fft_buffer[ch][2 * i + 1] = 0.0f;
                }

                dsps_fft2r_fc32(app->_fft_buffer[ch], N_SAMPLES);
                dsps_bit_rev_fc32(app->_fft_buffer[ch], N_SAMPLES);

                for (int i = 0; i < N_SAMPLES / 2; ++i) {
                    const float real = app->_fft_buffer[ch][2 * i];
                    const float imag = app->_fft_buffer[ch][2 * i + 1];
                    const float magnitude = sqrtf(real * real + imag * imag);
                    app->_spectrum[ch][i] = 20.0f * log10f(magnitude / (N_SAMPLES / 2) + 1e-9f);
                }

                for (int i = 0; i < STRIPE_COUNT; ++i) {
                    const int fft_idx = i * (N_SAMPLES / 2) / STRIPE_COUNT;
                    const float boosted_db = app->_spectrum[ch][fft_idx] + 10.0f;
                    app->_display_spectrum[ch][i] = fmaxf(-90.0f, fminf(0.0f, boosted_db));

                    constexpr float smooth_factor = 0.2f;
                    app->_smooth_spectrum[ch][i] =
                        app->_smooth_spectrum[ch][i] * (1.0f - smooth_factor) +
                        app->_display_spectrum[ch][i] * smooth_factor;
                }
            }

            portENTER_CRITICAL(&app->_spectrum_mux);
            std::memcpy(
                app->_published_spectrum,
                app->_smooth_spectrum,
                sizeof(app->_published_spectrum)
            );
            portEXIT_CRITICAL(&app->_spectrum_mux);
            taskYIELD();
        }

        // Only this worker opens, reads and closes the shared codec. Publishing the
        // terminal state after stop lets pause() safely hand it to MusicPlayer.
        const bool codec_released = app->releaseCodec(
            capture_failed ? CaptureState::Error : CaptureState::Idle
        );
        if (app->_worker_exit.load()) {
            break;
        }
        if (capture_failed) {
            app->_capture_requested.store(false);
        }
        if (!codec_released) {
            app->_capture_requested.store(false);
        }
    }

    if (!app->_codec_released.load()) {
        (void)app->releaseCodec(CaptureState::Idle);
    } else if (app->_codec_release_result.load() == ESP_OK) {
        app->_capture_state.store(CaptureState::Idle);
    }
    app->_audio_task_handle.store(nullptr);
    app->_worker_stopped.store(true);
    vTaskDelete(NULL);
}

void SpecAnalyzer::setStyle(int style)
{
    const int count = static_cast<int>(Style::Max);
    style = (style % count + count) % count;
    _style = static_cast<Style>(style);
    std::memset(_visual_peak, 0, sizeof(_visual_peak));
    if (_style_label) {
        lv_label_set_text(_style_label, STYLE_NAMES[style]);
        lv_obj_set_style_opa(_style_label, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_fade_out(_style_label, 400, 1200);
    }
    for (int i = 0; i < count; ++i) {
        if (_page_dots[i]) {
            const bool active = i == style;
            lv_obj_set_width(_page_dots[i], active ? 22 : 8);
            lv_obj_set_style_bg_opa(_page_dots[i], active ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
        }
    }
    for (int ch = 0; ch < MIC_COUNT; ++ch) {
        if (_mic_labels[ch]) {
            if (_style == Style::MicTest) {
                lv_obj_remove_flag(_mic_labels[ch], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(_mic_labels[ch], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (_style == Style::MicTest) {
        updateMicLabels();
    }
}

void SpecAnalyzer::canvas_event_cb(lv_event_t *event)
{
    auto *app = static_cast<SpecAnalyzer *>(lv_event_get_user_data(event));
    lv_indev_t *indev = lv_indev_active();
    if (!app || !indev) {
        return;
    }
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if (direction == LV_DIR_LEFT || direction == LV_DIR_RIGHT) {
        app->setStyle(static_cast<int>(app->_style) + (direction == LV_DIR_LEFT ? 1 : -1));
        lv_indev_wait_release(indev);
    }
}

void SpecAnalyzer::updateLevels(void)
{
    for (int ch = 0; ch < MIC_COUNT; ++ch) {
        for (int band = 0; band < STRIPE_COUNT; ++band) {
            float norm = fmaxf(0.0f, fminf(1.0f, (_render_spectrum[ch][band] + 90.0f) / 90.0f));
            const float target = powf(norm, 0.8f);
            float &level = _level[ch][band];
            level += (target - level) * (target > level ? 0.55f : 0.18f);
            float &peak = _visual_peak[ch][band];
            peak = peak < level ? level : fmaxf(0.0f, peak - (0.003f + peak * 0.008f));
        }
        _shown_rms_db[ch] += (_rms_db[ch] - _shown_rms_db[ch]) * 0.3f;
    }
}

void SpecAnalyzer::updateMicLabels(void)
{
    for (int ch = 0; ch < MIC_COUNT; ++ch) {
        if (!_mic_labels[ch]) {
            continue;
        }
        const int other = (ch + 1) % MIC_COUNT;
        const char *verdict = "OK";
        uint32_t color = 0x4CAF50;
        if (_zero_hops[ch] >= NO_DATA_HOPS) {
            verdict = "NO DATA";
            color = 0xF44336;
        } else if (_peak_dbfs[ch] >= -1.0f) {
            verdict = "CLIP";
            color = 0xFF9800;
        } else if (_shown_rms_db[ch] < -75.0f) {
            verdict = "QUIET";
            color = 0x9E9E9E;
        } else if (_shown_rms_db[ch] < _shown_rms_db[other] - 20.0f) {
            verdict = "LOW";
            color = 0xFF9800;
        }
        lv_label_set_text_fmt(_mic_labels[ch], "MIC%d  %d dBFS  %s", ch + 1,
                              static_cast<int>(lroundf(_shown_rms_db[ch])), verdict);
        lv_obj_set_style_text_color(_mic_labels[ch], lv_color_hex(color), LV_PART_MAIN);
    }
}

void SpecAnalyzer::drawMicTest(void)
{
    const int pitch = 320 / STRIPE_COUNT;
    const int left = (CANVAS_WIDTH - pitch * STRIPE_COUNT) / 2;
    const int bar_width = pitch - 1;
    const uint16_t white = lv_color_to_u16(lv_color_white());
    const uint16_t axis = lv_color_to_u16(lv_color_hex(0x404040));
    for (int ch = 0; ch < MIC_COUNT; ++ch) {
        const int base_y = MIC_PANEL_TOP[ch] + MIC_PANEL_HEIGHT;
        fill_rect(_draw_buf, left, base_y + 2, left + pitch * STRIPE_COUNT - 2, base_y + 2, axis);
        for (int band = 0; band < STRIPE_COUNT; ++band) {
            const int x1 = left + band * pitch;
            const int x2 = x1 + bar_width - 1;
            const int height = 2 + static_cast<int>(_level[ch][band] * (MIC_PANEL_HEIGHT - 2));
            fill_rect(_draw_buf, x1, base_y - height, x2, base_y, _band_color[ch][band]);
            const int peak_y = base_y - 2 - static_cast<int>(_visual_peak[ch][band] * (MIC_PANEL_HEIGHT - 2));
            fill_rect(_draw_buf, x1, peak_y - 1, x2, peak_y, white);
        }
    }
}

void SpecAnalyzer::drawBars(void)
{
    const int pitch = 360 / STRIPE_COUNT;
    const int left = (CANVAS_WIDTH - pitch * STRIPE_COUNT) / 2;
    const int bar_width = pitch - 1;
    const int base_y = CANVAS_HEIGHT * 62 / 100;
    const int max_height = 170;
    const uint16_t white = lv_color_to_u16(lv_color_white());
    for (int band = 0; band < STRIPE_COUNT; ++band) {
        const int x1 = left + band * pitch;
        const int x2 = x1 + bar_width - 1;
        const int height = 4 + static_cast<int>(_level[0][band] * max_height);
        fill_rect(_draw_buf, x1, base_y - height, x2, base_y, _band_color[0][band]);
        fill_rect(_draw_buf, x1, base_y + 3, x2, base_y + 3 + height * 50 / max_height, _band_faint[band]);
        const int peak_y = base_y - 4 - static_cast<int>(_visual_peak[0][band] * max_height);
        fill_rect(_draw_buf, x1, peak_y - 2, x2, peak_y, white);
    }
}

void SpecAnalyzer::drawRadial(void)
{
    const float cx = CANVAS_WIDTH / 2.0f;
    const float cy = CANVAS_HEIGHT / 2.0f;
    float energy = 0.0f;
    for (int band = 0; band < STRIPE_COUNT / 4; ++band) energy += _level[0][band];
    energy /= STRIPE_COUNT / 4;
    aa_disc(_draw_buf, cx, cy, 56 + energy * 30,
            lv_color_to_u16(lv_color_hsv_to_rgb(200, 80, 25 + static_cast<uint8_t>(energy * 55))));
    aa_disc(_draw_buf, cx, cy, 52 + energy * 30,
            lv_color_to_u16(lv_color_hsv_to_rgb(200, 80, 12 + static_cast<uint8_t>(energy * 30))));
    for (int spoke = 0; spoke < STRIPE_COUNT; ++spoke) {
        const int band = spoke < STRIPE_COUNT / 2 ? 2 * spoke : 2 * (STRIPE_COUNT - 1 - spoke);
        const float angle = (spoke + 0.5f) * 2.0f * PI / STRIPE_COUNT - PI / 2.0f;
        const float cosine = cosf(angle);
        const float sine = sinf(angle);
        const float length = 6.0f + _level[0][band] * 118.0f;
        aa_line(_draw_buf, cx + cosine * 96.0f, cy + sine * 96.0f,
                cx + cosine * (96.0f + length), cy + sine * (96.0f + length), 4.5f, _band_color[0][band]);
        const float peak = 106.0f + _visual_peak[0][band] * 118.0f;
        aa_disc(_draw_buf, cx + cosine * peak, cy + sine * peak, 2.0f, lv_color_to_u16(lv_color_white()));
    }
}

void SpecAnalyzer::drawHalo(void)
{
    constexpr int points = STRIPE_COUNT * 2;
    float px[points + 1], py[points + 1], qx[points + 1], qy[points + 1];
    const float cx = CANVAS_WIDTH / 2.0f;
    const float cy = CANVAS_HEIGHT / 2.0f;
    for (int point = 0; point <= points; ++point) {
        const int index = point % points;
        const int band = index < STRIPE_COUNT ? index : points - 1 - index;
        const float angle = index * 2.0f * PI / points - PI / 2.0f;
        const float radius = 112.0f + _level[0][band] * 96.0f;
        const float peak_radius = 120.0f + _visual_peak[0][band] * 96.0f;
        px[point] = cx + cosf(angle) * radius;
        py[point] = cy + sinf(angle) * radius;
        qx[point] = cx + cosf(angle) * peak_radius;
        qy[point] = cy + sinf(angle) * peak_radius;
    }
    for (int point = 0; point < points; ++point) {
        const int band = point < STRIPE_COUNT ? point : points - 1 - point;
        aa_line(_draw_buf, qx[point], qy[point], qx[point + 1], qy[point + 1], 1.5f, _band_dim[band]);
        aa_line(_draw_buf, px[point], py[point], px[point + 1], py[point + 1], 3.5f, _band_color[0][band]);
    }
}

// The worker publishes FFT bins; all drawing and touch-driven style changes stay in LVGL's task.
void SpecAnalyzer::timer_cb(lv_timer_t *timer)
{
    static uint32_t label_frames = 0;
    auto *app = static_cast<SpecAnalyzer *>(lv_timer_get_user_data(timer));
    if (!app || !app->_canvas || !app->_draw_buf) {
        return;
    }
    app->_shown_capture_state = app->_capture_state.load();
    portENTER_CRITICAL(&app->_spectrum_mux);
    std::memcpy(app->_render_spectrum, app->_published_spectrum, sizeof(app->_render_spectrum));
    portEXIT_CRITICAL(&app->_spectrum_mux);
    app->updateLevels();
    std::fill_n(app->_draw_buf, static_cast<size_t>(app->CANVAS_WIDTH) * app->CANVAS_HEIGHT,
                lv_color_to_u16(lv_color_black()));
    switch (app->_style) {
    case Style::MicTest:
        app->drawMicTest();
        if (++label_frames % LABEL_UPDATE_FRAMES == 0) {
            app->updateMicLabels();
        }
        break;
    case Style::Bars: app->drawBars(); break;
    case Style::Radial: app->drawRadial(); break;
    case Style::Halo: app->drawHalo(); break;
    case Style::Max: break;
    }
    lv_obj_invalidate(app->_canvas);
}

} // namespace esp_brookesia::apps
