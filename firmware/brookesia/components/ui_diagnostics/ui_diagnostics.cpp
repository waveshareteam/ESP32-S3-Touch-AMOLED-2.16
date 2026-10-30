/* SPDX-License-Identifier: Apache-2.0 */

#include "ui_diagnostics.hpp"

#include <cctype>
#include <atomic>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "driver/usb_serial_jtag.h"
#include "bsp_board_extra.h"
#include "VideoPlayer.hpp"
#include "XiaozhiUi.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "systems/base/esp_brookesia_base_context.hpp"
#include "systems/phone/esp_brookesia_phone.hpp"

namespace {

constexpr char TAG[] = "ui_diag";
constexpr size_t LINE_CAPACITY = 96;
constexpr size_t POINTER_QUEUE_CAPACITY = 16;
constexpr size_t CAPTURE_BYTES_PER_LINE = 48;
constexpr size_t ASYNC_REPLY_CAPACITY = 2048;
constexpr uint32_t AUDIO_SAMPLE_RATE = 24000;
constexpr size_t AUDIO_SLOT_COUNT = 4;
constexpr size_t AUDIO_CHUNK_FRAMES = AUDIO_SAMPLE_RATE / 100;
constexpr size_t AUDIO_BASELINE_CHUNKS = 10;
constexpr size_t AUDIO_TONE_CHUNKS = 40;
constexpr size_t DISPLAY_HEIGHT = 480;
constexpr size_t DISPLAY_WIDTH = 480;
constexpr size_t FULL_WIDTH_ROW_BYTES = DISPLAY_HEIGHT / 8;

enum class AsyncCommand : uint8_t {
    NONE,
    APPS,
    RUN,
    CLOSE,
    HOME,
    BACK,
    CAPTURE,
    CHAT_LAYOUT,
};

struct PointerSample {
    lv_coord_t x;
    lv_coord_t y;
    lv_indev_state_t state;
};

struct State {
    esp_brookesia::systems::phone::Phone *phone = nullptr;
    lv_display_t *display = nullptr;
    lv_indev_t *virtual_pointer = nullptr;
    portMUX_TYPE pointer_lock = portMUX_INITIALIZER_UNLOCKED;
    PointerSample pointers[POINTER_QUEUE_CAPACITY] = {};
    size_t pointer_head = 0;
    size_t pointer_tail = 0;
    std::atomic<uint32_t> render_count{0};
    std::atomic<uint32_t> flush_count{0};
    std::atomic<uint64_t> render_time_us{0};
    std::atomic<uint64_t> flush_time_us{0};
    std::atomic<int64_t> render_started_us{0};
    std::atomic<int64_t> flush_started_us{0};
    std::atomic<uint32_t> flush_wait_count{0};
    std::atomic<uint64_t> flush_wait_time_us{0};
    std::atomic<int64_t> flush_wait_started_us{0};
    std::atomic<uint32_t> full_screen_frames{0};
    std::atomic<uint64_t> flushed_pixels{0};
    uint8_t full_width_rows[FULL_WIDTH_ROW_BYTES] = {};
    int64_t stats_epoch_us = 0;
    SemaphoreHandle_t command_done = nullptr;
    AsyncCommand command = AsyncCommand::NONE;
    char command_specifier[48] = {};
    char async_reply[ASYNC_REPLY_CAPACITY] = {};
    size_t async_reply_length = 0;
    lv_draw_buf_t *capture = nullptr;
};

State state;

struct AudioStats {
    uint64_t sum_sq[AUDIO_SLOT_COUNT] = {};
    uint32_t peak[AUDIO_SLOT_COUNT] = {};
    uint32_t samples = 0;
};

void write_text(const char *text)
{
    if (text == nullptr) {
        return;
    }
    const size_t length = strlen(text);
    size_t offset = 0;
    while (offset < length) {
        const int result = usb_serial_jtag_write_bytes(text + offset, length - offset, pdMS_TO_TICKS(1000));
        if (result <= 0) {
            return;
        }
        offset += static_cast<size_t>(result);
    }
}

void reply(const char *format, ...)
{
    char buffer[768];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    write_text(buffer);
}

void reset_async_reply()
{
    state.async_reply[0] = '\0';
    state.async_reply_length = 0;
}

void append_async_reply(const char *format, ...)
{
    if (state.async_reply_length >= sizeof(state.async_reply) - 1) {
        return;
    }
    va_list args;
    va_start(args, format);
    const int result = vsnprintf(state.async_reply + state.async_reply_length,
                                 sizeof(state.async_reply) - state.async_reply_length, format, args);
    va_end(args);
    if (result > 0) {
        const size_t available = sizeof(state.async_reply) - state.async_reply_length - 1;
        state.async_reply_length += static_cast<size_t>(result) < available ?
                                    static_cast<size_t>(result) : available;
    }
}

bool enqueue_pointer(lv_coord_t x, lv_coord_t y, lv_indev_state_t input_state)
{
    bool accepted = false;
    portENTER_CRITICAL(&state.pointer_lock);
    const size_t next = (state.pointer_head + 1) % POINTER_QUEUE_CAPACITY;
    if (next != state.pointer_tail) {
        state.pointers[state.pointer_head] = {x, y, input_state};
        state.pointer_head = next;
        accepted = true;
    }
    portEXIT_CRITICAL(&state.pointer_lock);
    return accepted;
}

void pointer_read_cb(lv_indev_t *, lv_indev_data_t *data)
{
    PointerSample sample = {0, 0, LV_INDEV_STATE_RELEASED};
    bool has_sample = false;
    bool has_more = false;
    portENTER_CRITICAL(&state.pointer_lock);
    if (state.pointer_tail != state.pointer_head) {
        sample = state.pointers[state.pointer_tail];
        state.pointer_tail = (state.pointer_tail + 1) % POINTER_QUEUE_CAPACITY;
        has_sample = true;
        has_more = state.pointer_tail != state.pointer_head;
    }
    portEXIT_CRITICAL(&state.pointer_lock);
    if (has_sample) {
        data->point.x = sample.x;
        data->point.y = sample.y;
        data->state = sample.state;
        data->continue_reading = has_more;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void display_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    const int64_t now = esp_timer_get_time();
    if (code == LV_EVENT_RENDER_START) {
        state.render_started_us.store(now, std::memory_order_relaxed);
        memset(state.full_width_rows, 0, sizeof(state.full_width_rows));
    } else if (code == LV_EVENT_RENDER_READY) {
        const int64_t started = state.render_started_us.exchange(0, std::memory_order_relaxed);
        state.render_count.fetch_add(1, std::memory_order_relaxed);
        if (started != 0 && now >= started) {
            state.render_time_us.fetch_add(now - started, std::memory_order_relaxed);
        }
        bool full_height = true;
        for (uint8_t row : state.full_width_rows) {
            if (row != 0xff) {
                full_height = false;
                break;
            }
        }
        if (full_height) {
            state.full_screen_frames.fetch_add(1, std::memory_order_relaxed);
        }
    } else if (code == LV_EVENT_FLUSH_START) {
        state.flush_started_us.store(now, std::memory_order_relaxed);
        const auto *area = static_cast<const lv_area_t *>(lv_event_get_param(event));
        if (area != nullptr) {
            const int32_t x1 = area->x1 < 0 ? 0 : area->x1;
            const int32_t y1 = area->y1 < 0 ? 0 : area->y1;
            const int32_t x2 = area->x2 >= DISPLAY_WIDTH ? DISPLAY_WIDTH - 1 : area->x2;
            const int32_t y2 = area->y2 >= DISPLAY_HEIGHT ? DISPLAY_HEIGHT - 1 : area->y2;
            if (x1 <= x2 && y1 <= y2) {
                state.flushed_pixels.fetch_add(
                    static_cast<uint64_t>(x2 - x1 + 1) * (y2 - y1 + 1), std::memory_order_relaxed);
            }
            if (x1 == 0 && x2 == DISPLAY_WIDTH - 1 && y1 <= y2) {
                for (int32_t y = y1; y <= y2; ++y) {
                    state.full_width_rows[y / 8] |= static_cast<uint8_t>(1U << (y % 8));
                }
            }
        }
    } else if (code == LV_EVENT_FLUSH_FINISH) {
        const int64_t started = state.flush_started_us.exchange(0, std::memory_order_relaxed);
        state.flush_count.fetch_add(1, std::memory_order_relaxed);
        if (started != 0 && now >= started) {
            state.flush_time_us.fetch_add(now - started, std::memory_order_relaxed);
        }
    } else if (code == LV_EVENT_FLUSH_WAIT_START) {
        state.flush_wait_started_us.store(now, std::memory_order_relaxed);
    } else if (code == LV_EVENT_FLUSH_WAIT_FINISH) {
        const int64_t started = state.flush_wait_started_us.exchange(0, std::memory_order_relaxed);
        state.flush_wait_count.fetch_add(1, std::memory_order_relaxed);
        if (started != 0 && now >= started) {
            state.flush_wait_time_us.fetch_add(now - started, std::memory_order_relaxed);
        }
    }
}

bool parse_int(const char *text, int *value)
{
    if (text == nullptr || value == nullptr) {
        return false;
    }
    char *end = nullptr;
    const long parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

int find_app(const char *specifier)
{
    if (state.phone == nullptr || specifier == nullptr) {
        return -1;
    }
    int numeric = -1;
    if (parse_int(specifier, &numeric) && state.phone->getManager().checkAppID_Valid(numeric)) {
        return numeric;
    }
    for (int id = 1; id < 64; ++id) {
        auto *app = state.phone->getManager().getInstalledApp(id);
        if (app != nullptr && app->getName() != nullptr && strcasecmp(app->getName(), specifier) == 0) {
            return id;
        }
    }
    return -1;
}

void command_apps()
{
    append_async_reply("UIDIAG APPS BEGIN\n");
    for (int id = 1; id < 64; ++id) {
        auto *app = state.phone->getManager().getInstalledApp(id);
        if (app != nullptr) {
            append_async_reply("UIDIAG APP %d %s%s\n", id, app->getName(),
                               state.phone->getManager().getActiveApp() == app ? " ACTIVE" : "");
            if (state.phone->getManager().getRunningAppById(id) != nullptr) {
                append_async_reply("UIDIAG RUNNING %d %s\n", id, app->getName());
            }
        }
    }
    append_async_reply("UIDIAG APPS END\n");
}

void command_run(const char *specifier)
{
    const int id = find_app(specifier);
    if (id < 0) {
        append_async_reply("UIDIAG ERR app\n");
        return;
    }
    esp_brookesia::systems::base::Context::AppEventData event = {
        .id = id,
        .type = esp_brookesia::systems::base::Context::AppEventType::START,
        .data = nullptr,
    };
    const bool result = state.phone->sendAppEvent(&event);
    append_async_reply("UIDIAG RUN %d %s\n", id, result ? "OK" : "ERR");
}

void command_close(const char *specifier)
{
    const int id = find_app(specifier);
    if (id < 0) {
        append_async_reply("UIDIAG ERR app\n");
        return;
    }
    esp_brookesia::systems::base::Context::AppEventData event = {
        .id = id,
        .type = esp_brookesia::systems::base::Context::AppEventType::STOP,
        .data = nullptr,
    };
    const bool result = state.phone->sendAppEvent(&event);
    const bool closed = state.phone->getManager().getRunningAppById(id) == nullptr;
    append_async_reply("UIDIAG CLOSE %d %s\n", id, result && closed ? "OK" : "ERR");
}

void command_home_or_back(bool home)
{
    const bool result = state.phone->sendNavigateEvent(
                            home ? esp_brookesia::systems::base::Manager::NavigateType::HOME
                                 : esp_brookesia::systems::base::Manager::NavigateType::BACK);
    append_async_reply("UIDIAG %s %s\n", home ? "HOME" : "BACK", result ? "OK" : "ERR");
}

void command_stats()
{
    const int64_t now = esp_timer_get_time();
    const int64_t elapsed = state.stats_epoch_us == 0 ? 0 : now - state.stats_epoch_us;
    const uint32_t renders = state.render_count.exchange(0, std::memory_order_relaxed);
    const uint32_t flushes = state.flush_count.exchange(0, std::memory_order_relaxed);
    const uint64_t render_time = state.render_time_us.exchange(0, std::memory_order_relaxed);
    const uint64_t flush_time = state.flush_time_us.exchange(0, std::memory_order_relaxed);
    const uint32_t flush_waits = state.flush_wait_count.exchange(0, std::memory_order_relaxed);
    const uint64_t flush_wait_time = state.flush_wait_time_us.exchange(0, std::memory_order_relaxed);
    const uint32_t full_frames = state.full_screen_frames.exchange(0, std::memory_order_relaxed);
    const uint64_t flushed_pixels = state.flushed_pixels.exchange(0, std::memory_order_relaxed);
    const uint32_t video_presented = esp_brookesia::apps::VideoPlayer::requestInstance()->getPresentedFrameCount();
    state.stats_epoch_us = now;
    reply("UIDIAG STATS elapsed_ms=%lld render=%u render_fps=%.2f render_avg_ms=%.2f "
          "flush_segments=%u flush_segment_avg_ms=%.2f "
          "flush_waits=%u flush_wait_avg_ms=%.2f flush_wait_total_ms=%.2f "
          "full_screen_frames=%u flushed_pixels=%llu video_presented=%u "
          "internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
          static_cast<long long>(elapsed / 1000), renders,
          elapsed ? renders * 1000000.0 / elapsed : 0.0,
          renders ? render_time / 1000.0 / renders : 0.0, flushes,
          flushes ? flush_time / 1000.0 / flushes : 0.0,
          flush_waits, flush_waits ? flush_wait_time / 1000.0 / flush_waits : 0.0,
          flush_wait_time / 1000.0,
          full_frames, static_cast<unsigned long long>(flushed_pixels), video_presented,
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

uint32_t integer_sqrt(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = 1ULL << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<uint32_t>(result);
}

void accumulate_audio(AudioStats *stats, const int16_t *samples, size_t bytes)
{
    if (stats == nullptr || samples == nullptr) {
        return;
    }
    const size_t frames = bytes / (sizeof(int16_t) * AUDIO_SLOT_COUNT);
    for (size_t frame = 0; frame < frames; ++frame) {
        for (size_t slot = 0; slot < AUDIO_SLOT_COUNT; ++slot) {
            const int32_t sample = samples[frame * AUDIO_SLOT_COUNT + slot];
            const uint32_t magnitude = static_cast<uint32_t>(sample < 0 ? -sample : sample);
            if (magnitude > stats->peak[slot]) {
                stats->peak[slot] = magnitude;
            }
            stats->sum_sq[slot] += static_cast<uint64_t>(sample) * sample;
        }
    }
    stats->samples += static_cast<uint32_t>(frames);
}

void report_audio_stats(const char *phase, const AudioStats &stats)
{
    static constexpr const char *slot_names[AUDIO_SLOT_COUNT] = {
        "mic1", "echo_mic3", "mic2", "mic4_empty",
    };
    for (size_t slot = 0; slot < AUDIO_SLOT_COUNT; ++slot) {
        const uint32_t rms = stats.samples == 0 ? 0 : integer_sqrt(stats.sum_sq[slot] / stats.samples);
        reply("UIDIAG AUDIO %s slot=%u name=%s peak=%u rms=%u samples=%u\n", phase,
              static_cast<unsigned>(slot), slot_names[slot], stats.peak[slot], rms, stats.samples);
    }
}

void command_audio_probe()
{
    if (bsp_extra_audio_session_get_owner() != BSP_EXTRA_AUDIO_OWNER_NONE) {
        reply("UIDIAG AUDIO BUSY owner=%s\n",
              bsp_extra_audio_owner_name(bsp_extra_audio_session_get_owner()));
        return;
    }

    esp_err_t result = bsp_extra_audio_session_acquire(BSP_EXTRA_AUDIO_OWNER_MUSIC);
    if (result != ESP_OK) {
        reply("UIDIAG AUDIO ERR acquire=%s\n", esp_err_to_name(result));
        return;
    }

    const int original_volume = bsp_extra_codec_volume_get();
    auto *record = static_cast<int16_t *>(heap_caps_malloc(
        AUDIO_CHUNK_FRAMES * AUDIO_SLOT_COUNT * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    auto *tone = static_cast<int16_t *>(heap_caps_malloc(
        AUDIO_CHUNK_FRAMES * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    AudioStats baseline = {};
    AudioStats tone_stats = {};

    do {
        if (record == nullptr || tone == nullptr) {
            result = ESP_ERR_NO_MEM;
            break;
        }
        result = bsp_extra_codec_init();
        if (result != ESP_OK) {
            break;
        }
        result = bsp_extra_codec_set_voice_fs(
            AUDIO_SAMPLE_RATE, CODEC_DEFAULT_BIT_WIDTH, AUDIO_SLOT_COUNT,
            BSP_EXTRA_ES7210_TDM_ALL_SLOTS_MASK, BSP_EXTRA_ES7210_PHYSICAL_FRONT_MIC_MASK);
        if (result != ESP_OK) {
            break;
        }

        reply("UIDIAG AUDIO BEGIN rate=%u tone_hz=1000 tone_ms=400 volume=30\n", AUDIO_SAMPLE_RATE);
        // Discard codec/ADC startup samples before measuring the quiet baseline.
        for (size_t chunk = 0; chunk < AUDIO_BASELINE_CHUNKS * 2; ++chunk) {
            size_t read = 0;
            result = bsp_extra_i2s_read(record, AUDIO_CHUNK_FRAMES * AUDIO_SLOT_COUNT * sizeof(int16_t), &read, 1000);
            if (result != ESP_OK) {
                break;
            }
        }
        if (result != ESP_OK) {
            break;
        }
        for (size_t chunk = 0; chunk < AUDIO_BASELINE_CHUNKS; ++chunk) {
            size_t read = 0;
            result = bsp_extra_i2s_read(record, AUDIO_CHUNK_FRAMES * AUDIO_SLOT_COUNT * sizeof(int16_t), &read, 1000);
            if (result != ESP_OK) {
                break;
            }
            accumulate_audio(&baseline, record, read);
        }
        if (result != ESP_OK) {
            break;
        }
        report_audio_stats("baseline", baseline);

        result = bsp_extra_codec_volume_set(30, nullptr);
        if (result != ESP_OK) {
            break;
        }
        static constexpr int16_t sine_1khz_24k[24] = {
            0, 2588, 5000, 7071, 8660, 9659, 10000, 9659, 8660, 7071, 5000, 2588,
            0, -2588, -5000, -7071, -8660, -9659, -10000, -9659, -8660, -7071, -5000, -2588,
        };
        for (size_t chunk = 0; chunk < AUDIO_TONE_CHUNKS; ++chunk) {
            for (size_t frame = 0; frame < AUDIO_CHUNK_FRAMES; ++frame) {
                const int16_t sample = sine_1khz_24k[frame % 24];
                tone[frame * 2] = sample;
                tone[frame * 2 + 1] = sample;
            }
            size_t written = 0;
            result = bsp_extra_i2s_write(tone, AUDIO_CHUNK_FRAMES * 2 * sizeof(int16_t), &written, 1000);
            if (result != ESP_OK || written != AUDIO_CHUNK_FRAMES * 2 * sizeof(int16_t)) {
                if (result == ESP_OK) {
                    result = ESP_FAIL;
                }
                break;
            }
            size_t read = 0;
            result = bsp_extra_i2s_read(record, AUDIO_CHUNK_FRAMES * AUDIO_SLOT_COUNT * sizeof(int16_t), &read, 1000);
            if (result != ESP_OK) {
                break;
            }
            accumulate_audio(&tone_stats, record, read);
        }
        if (result == ESP_OK) {
            report_audio_stats("tone", tone_stats);
        }
    } while (false);

    if (original_volume >= 0) {
        (void)bsp_extra_codec_volume_set(original_volume, nullptr);
    }
    (void)bsp_extra_codec_dev_stop();
    if (record != nullptr) {
        heap_caps_free(record);
    }
    if (tone != nullptr) {
        heap_caps_free(tone);
    }
    (void)bsp_extra_audio_session_release(BSP_EXTRA_AUDIO_OWNER_MUSIC);
    if (result == ESP_OK) {
        reply("UIDIAG AUDIO END OK\n");
    } else {
        reply("UIDIAG AUDIO ERR %s\n", esp_err_to_name(result));
    }
}

void command_capture_on_lvgl()
{
    lv_obj_t *screen = lv_display_get_screen_active(state.display);
    state.capture = screen ? lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565) : nullptr;
    append_async_reply(state.capture == nullptr ? "UIDIAG ERR capture\n" : "UIDIAG CAPTURE READY\n");
}

void command_chat_layout_on_lvgl()
{
    // Off-screen, real-device rasterization of the production view.  These
    // clearly marked samples validate long-text clipping without changing the
    // active conversation or displaying synthetic messages on the panel.
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_set_size(screen, 480, 480);
    esp_brookesia::apps::XiaozhiUi preview;
    if (preview.create(screen, nullptr, nullptr, true)) {
        preview.setNetworkReady(true);
        preview.setMode(esp_brookesia::apps::XiaozhiUi::Mode::Ready);
        preview.setStatus("Long-text layout test");
        preview.setHint("Tap the button to talk");
        preview.setChatMessage("user", "用户长文本布局测试：这是一段较长的用户消息，用于验证多行换行、独立滚动以及边界裁剪。文字不能进入助手回复区域，也不能覆盖下方的提示和按钮。");
        preview.setChatMessage("assistant", "助手长回复布局测试：这段内容专门验证固定回复区域。\n第一段：较长回答应在自身容器内换行。\n第二段：超出可见范围时允许上下滚动。\n第三段：底部提示与操作按钮必须保持独立。\n第四段：无论回复长度如何变化，都不能绘制到提示或按钮上。\n第五段：这仍然是设备上的真实 LVGL 渲染，不是浏览器排版示意。");
        lv_obj_update_layout(preview.root());
        state.capture = lv_snapshot_take(preview.root(), LV_COLOR_FORMAT_RGB565);
    }
    preview.destroy();
    lv_obj_delete(screen);
    append_async_reply(state.capture ? "UIDIAG CAPTURE READY\n" : "UIDIAG ERR capture\n");
}

void transmit_capture()
{
    lv_draw_buf_t *snapshot = state.capture;
    state.capture = nullptr;
    if (snapshot == nullptr) {
        return;
    }
    reply("UIDIAG CAPTURE BEGIN w=%u h=%u stride=%u cf=RGB565 encoding=base64 bytes=%u\n",
          snapshot->header.w, snapshot->header.h, snapshot->header.stride, snapshot->data_size);
    static constexpr char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char line[4 * ((CAPTURE_BYTES_PER_LINE + 2) / 3) + 32];
    for (uint32_t offset = 0; offset < snapshot->data_size; offset += CAPTURE_BYTES_PER_LINE) {
        const uint32_t bytes = (snapshot->data_size - offset) < CAPTURE_BYTES_PER_LINE ?
                               snapshot->data_size - offset : CAPTURE_BYTES_PER_LINE;
        int length = snprintf(line, sizeof(line), "UIDIAG DATA ");
        for (uint32_t i = 0; i < bytes; i += 3) {
            const uint32_t group = static_cast<uint32_t>(snapshot->data[offset + i]) << 16 |
                                   (i + 1 < bytes ? static_cast<uint32_t>(snapshot->data[offset + i + 1]) << 8 : 0) |
                                   (i + 2 < bytes ? snapshot->data[offset + i + 2] : 0);
            line[length++] = base64[(group >> 18) & 0x3f];
            line[length++] = base64[(group >> 12) & 0x3f];
            line[length++] = i + 1 < bytes ? base64[(group >> 6) & 0x3f] : '=';
            line[length++] = i + 2 < bytes ? base64[group & 0x3f] : '=';
        }
        line[length++] = '\n';
        line[length] = '\0';
        write_text(line);
    }
    reply("UIDIAG CAPTURE END\n");
    lv_draw_buf_destroy(snapshot);
}

void async_command_cb(void *)
{
    reset_async_reply();
    switch (state.command) {
    case AsyncCommand::APPS:
        command_apps();
        break;
    case AsyncCommand::RUN:
        command_run(state.command_specifier);
        break;
    case AsyncCommand::CLOSE:
        command_close(state.command_specifier);
        break;
    case AsyncCommand::HOME:
        command_home_or_back(true);
        break;
    case AsyncCommand::BACK:
        command_home_or_back(false);
        break;
    case AsyncCommand::CAPTURE:
        command_capture_on_lvgl();
        break;
    case AsyncCommand::CHAT_LAYOUT:
        command_chat_layout_on_lvgl();
        break;
    case AsyncCommand::NONE:
        append_async_reply("UIDIAG ERR command\n");
        break;
    }
    state.command = AsyncCommand::NONE;
    xSemaphoreGive(state.command_done);
}

bool submit_async_command(AsyncCommand command, const char *specifier = nullptr)
{
    if (state.command_done == nullptr || state.command != AsyncCommand::NONE) {
        return false;
    }
    state.command = command;
    snprintf(state.command_specifier, sizeof(state.command_specifier), "%s", specifier ? specifier : "");
    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        state.command = AsyncCommand::NONE;
        return false;
    }
    const lv_result_t scheduled = lv_async_call(async_command_cb, nullptr);
    esp_lv_adapter_unlock();
    if (scheduled != LV_RESULT_OK) {
        state.command = AsyncCommand::NONE;
        return false;
    }
    if (xSemaphoreTake(state.command_done, pdMS_TO_TICKS(10000)) != pdTRUE) {
        state.command = AsyncCommand::NONE;
        return false;
    }
    write_text(state.async_reply);
    if (command == AsyncCommand::CAPTURE || command == AsyncCommand::CHAT_LAYOUT) {
        transmit_capture();
    }
    return true;
}

void command_pointer(char *command, char *arg1, char *arg2, char *arg3, char *arg4)
{
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (strcmp(command, "tap") == 0 && parse_int(arg1, &x1) && parse_int(arg2, &y1)) {
        const bool result = enqueue_pointer(x1, y1, LV_INDEV_STATE_PRESSED) &&
                            enqueue_pointer(x1, y1, LV_INDEV_STATE_RELEASED);
        reply("UIDIAG TAP %s\n", result ? "OK" : "FULL");
    } else if (strcmp(command, "drag") == 0 && parse_int(arg1, &x1) && parse_int(arg2, &y1) &&
               parse_int(arg3, &x2) && parse_int(arg4, &y2)) {
        const bool result = enqueue_pointer(x1, y1, LV_INDEV_STATE_PRESSED) &&
                            enqueue_pointer(x2, y2, LV_INDEV_STATE_PRESSED) &&
                            enqueue_pointer(x2, y2, LV_INDEV_STATE_RELEASED);
        reply("UIDIAG DRAG %s\n", result ? "OK" : "FULL");
    } else {
        reply("UIDIAG ERR touch\n");
    }
}

void process_command(char *line)
{
    char *save = nullptr;
    char *command = strtok_r(line, " \t", &save);
    if (command == nullptr) {
        return;
    }
    for (char *cursor = command; *cursor != '\0'; ++cursor) {
        *cursor = static_cast<char>(tolower(static_cast<unsigned char>(*cursor)));
    }
    char *arg1 = strtok_r(nullptr, " \t", &save);
    char *arg2 = strtok_r(nullptr, " \t", &save);
    char *arg3 = strtok_r(nullptr, " \t", &save);
    char *arg4 = strtok_r(nullptr, " \t", &save);

    if (strcmp(command, "help") == 0) {
        reply("UIDIAG HELP apps|run <id|name>|close <id|name>|home|back|tap <x> <y>|drag <x1> <y1> <x2> <y2>|capture|stats|audio_probe\n");
    } else if (strcmp(command, "apps") == 0) {
        if (!submit_async_command(AsyncCommand::APPS)) reply("UIDIAG ERR async\n");
    } else if (strcmp(command, "run") == 0 && arg1 != nullptr) {
        if (!submit_async_command(AsyncCommand::RUN, arg1)) reply("UIDIAG ERR async\n");
    } else if (strcmp(command, "close") == 0 && arg1 != nullptr) {
        if (!submit_async_command(AsyncCommand::CLOSE, arg1)) reply("UIDIAG ERR async\n");
    } else if (strcmp(command, "home") == 0) {
        if (!submit_async_command(AsyncCommand::HOME)) reply("UIDIAG ERR async\n");
    } else if (strcmp(command, "back") == 0) {
        if (!submit_async_command(AsyncCommand::BACK)) reply("UIDIAG ERR async\n");
    } else if (strcmp(command, "tap") == 0 || strcmp(command, "drag") == 0) {
        command_pointer(command, arg1, arg2, arg3, arg4);
    } else if (strcmp(command, "capture") == 0) {
        if (!submit_async_command(AsyncCommand::CAPTURE)) reply("UIDIAG ERR async\n");
    } else if (strcmp(command, "layout_chat") == 0) {
        if (!submit_async_command(AsyncCommand::CHAT_LAYOUT)) reply("UIDIAG ERR async\n");
    } else if (strcmp(command, "stats") == 0) {
        command_stats();
    } else if (strcmp(command, "audio_probe") == 0) {
        command_audio_probe();
    } else {
        reply("UIDIAG ERR command\n");
    }
}

void console_task(void *)
{
    char line[LINE_CAPACITY] = {};
    size_t length = 0;
    while (true) {
        uint8_t byte = 0;
        const int read = usb_serial_jtag_read_bytes(&byte, 1, portMAX_DELAY);
        if (read != 1) {
            continue;
        }
        if (byte == '\r' || byte == '\n') {
            if (length != 0) {
                line[length] = '\0';
                process_command(line);
                length = 0;
            }
        } else if (length + 1 < sizeof(line)) {
            line[length++] = static_cast<char>(byte);
        } else {
            length = 0;
            reply("UIDIAG ERR line\n");
        }
    }
}

} // namespace

esp_err_t ui_diagnostics_start(esp_brookesia::systems::phone::Phone *phone, lv_display_t *display)
{
#if !CONFIG_UI_DIAGNOSTICS_ENABLE
    (void)phone;
    (void)display;
    return ESP_OK;
#else
    if (state.phone != nullptr) {
        return ESP_OK;
    }
    if (phone == nullptr || display == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    state.phone = phone;
    state.display = display;
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        config.tx_buffer_size = 2048;
        config.rx_buffer_size = 256;
        const esp_err_t result = usb_serial_jtag_driver_install(&config);
        if (result != ESP_OK) {
            return result;
        }
    }
    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    state.virtual_pointer = lv_indev_create();
    if (state.virtual_pointer != nullptr) {
        lv_indev_set_type(state.virtual_pointer, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(state.virtual_pointer, display);
        lv_indev_set_read_cb(state.virtual_pointer, pointer_read_cb);
        lv_display_add_event_cb(display, display_event_cb, LV_EVENT_RENDER_START, nullptr);
        lv_display_add_event_cb(display, display_event_cb, LV_EVENT_RENDER_READY, nullptr);
        lv_display_add_event_cb(display, display_event_cb, LV_EVENT_FLUSH_START, nullptr);
        lv_display_add_event_cb(display, display_event_cb, LV_EVENT_FLUSH_FINISH, nullptr);
        lv_display_add_event_cb(display, display_event_cb, LV_EVENT_FLUSH_WAIT_START, nullptr);
        lv_display_add_event_cb(display, display_event_cb, LV_EVENT_FLUSH_WAIT_FINISH, nullptr);
    }
    esp_lv_adapter_unlock();
    if (state.virtual_pointer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    state.stats_epoch_us = esp_timer_get_time();
    state.command_done = xSemaphoreCreateBinary();
    if (state.command_done == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(console_task, "ui_diag", 6144, nullptr, 4, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "USB UI diagnostics ready; type help");
    return ESP_OK;
#endif
}
