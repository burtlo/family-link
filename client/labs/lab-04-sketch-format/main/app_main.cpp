#include "bsp/esp-box-3.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "model.hpp"
#include "fixture_map.hpp"
#include "preview_accounting.hpp"
#include "failure_publication.hpp"
#include "lvgl.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <new>

#ifndef CONFIG_LAB04_BUILD_ID
#define CONFIG_LAB04_BUILD_ID "lab04-fw-v1"
#endif

namespace
{
constexpr std::size_t kWorkerQueueCapacity = 8;
constexpr std::size_t kPreviewBytes = 28800;
constexpr std::size_t kTraceVariantCapacity = 20;
static_assert(kTraceVariantCapacity >= sizeof("legacy-colored"),
              "trace variant buffer must hold every supported variant name");
constexpr std::uint16_t kPalette[6] = {0x0000, 0xffff, 0xe600, 0x4de9, 0xe388, 0x5d1d};

enum class JobKind : std::uint8_t { Variant,
                                    Faults,
                                    Stop };
struct Job {
    JobKind kind;
    std::uint8_t variant;
};
struct TraceEvent {
    char scenario[20];
    char fixture[20];
    char variant[kTraceVariantCapacity];
    char position[8];
    char terminal[16];
    char reason[32];
    std::uint64_t operation_id;
    std::uint32_t position_ms;
    std::uint32_t terminal_count;
    std::uint32_t encoded_bytes;
    std::uint32_t preview_bytes;
    std::uint32_t preview_buffers;
    std::uint32_t live_buffers;
    std::uint32_t encode_us, decode_us, raster_us, seek_us;
    std::uint32_t raster_eligible, raster_rendered, raster_remaining;
    std::uint32_t internal_largest_free_block, psram_largest_free_block;
    std::uint32_t sample_decoded_segments;
    std::uint32_t sample_decoded_capacity_bytes;
    std::uint32_t sample_lvgl_largest_free_block;
    std::uint32_t sample_lvgl_total_size;
    std::uint32_t sample_lvgl_free_size;
    std::uint32_t sample_lvgl_used_pct;
    std::uint32_t sample_lvgl_max_used;
    std::uint32_t peak_expected_segments;
    std::uint32_t peak_capacity_bytes;
    std::uint32_t peak_internal_largest_free_block;
    std::uint32_t peak_psram_largest_free_block;
    std::uint32_t peak_lvgl_largest_free_block;
    std::uint32_t peak_lvgl_total_size;
    std::uint32_t peak_lvgl_free_size;
    std::uint32_t peak_lvgl_used_pct;
    std::uint32_t peak_lvgl_max_used;
    bool resource_sample;
    bool resource_peak;
    bool sample_lvgl_available;
    bool peak_lvgl_available;
    bool exploratory;
};

QueueHandle_t s_jobs;
QueueHandle_t s_publications;
lv_obj_t *s_status_label;
lv_obj_t *s_instructions_label;
lv_obj_t *s_controls[4] = {};
std::size_t s_control_count = 0;
void *s_preview;
std::uint64_t s_next_operation = 1;
std::uint32_t s_trace_seq = 0;
bool s_scenarios_complete = false;
lab04::FailurePublication s_harness_failure;
std::uint32_t s_queue_overflows_reported = 0;
// Only the worker calls send_trace, so this counter has a single writer. It
// covers firmware publication-queue rejection only, not UART/host byte loss.
std::uint32_t s_publication_queue_drops = 0;
// Counts trace rows passed to the sequenced writer and failures reported by
// its formatting/write APIs. It cannot establish UART delivery or capture.
std::uint32_t s_sequenced_rows_attempted = 0;
std::uint32_t s_serial_write_errors = 0;
std::uint32_t s_variant_events = 0, s_checkpoint_events = 0;
std::uint32_t s_malformed_events = 0, s_overflow_events = 0, s_cancel_events = 0;
std::uint32_t s_legacy_events = 0;
std::uint8_t s_legacy_seen = 0;
std::uint8_t s_variant_seen[3] = {0, 0, 0};
std::uint8_t s_checkpoint_positions[3] = {0, 0, 0};
std::uint8_t s_fault_seen[3] = {0, 0, 0};
std::uint8_t s_malformed_reasons[3] = {0, 0, 0};
std::uint64_t s_accepted_operation_ids[64] = {};
std::size_t s_accepted_operation_count = 0;
bool s_stop_requested = false;
TaskHandle_t s_app_task;
// Manual input acceptance is owned by the LVGL event thread. These flags are
// only read or written from the button callbacks, which LVGL dispatches
// serially while holding the BSP display lock.
bool s_manual_variant_accepted[3] = {false, false, false};
bool s_manual_faults_accepted = false;
const char *s_variant_names[] = {"points", "strokes", "preview"};
const lab04::Encoding s_encodings[] = {lab04::Encoding::TimedPoints, lab04::Encoding::TimedStrokes,
                                       lab04::Encoding::SegmentPreview};

void submit_variant(lv_event_t *event);
void submit_faults(lv_event_t *event);

void cleanup_resources()
{
    if (s_preview != nullptr) {
        heap_caps_free(s_preview);
        s_preview = nullptr;
    }
    if (s_jobs != nullptr) {
        vQueueDelete(s_jobs);
        s_jobs = nullptr;
    }
    if (s_publications != nullptr) {
        vQueueDelete(s_publications);
        s_publications = nullptr;
    }
}

// The BSP display lock is the same recursive mutex held around lv_timer_handler
// and input dispatch. Taking it waits for any callback already in flight; while
// held, remove the queue-submitting callbacks before the worker queue is freed.
bool disable_input_callbacks()
{
    if (s_control_count == 0)
        return true;
    if (!bsp_display_lock(0))
        return false;
    for (std::size_t i = 0; i < s_control_count; ++i) {
        if (s_controls[i] != nullptr) {
            lv_obj_remove_event_cb(s_controls[i], submit_variant);
            lv_obj_remove_event_cb(s_controls[i], submit_faults);
            lv_obj_set_clickable(s_controls[i], false);
        }
    }
    s_control_count = 0;
    bsp_display_unlock();
    return true;
}

void stop_worker_and_cleanup(bool worker_started)
{
    while (!disable_input_callbacks())
        vTaskDelay(1);
    if (worker_started) {
        const Job stop{JobKind::Stop, 0};
        while (xQueueSend(s_jobs, &stop, 0) != pdTRUE)
            vTaskDelay(1);
        // The worker has stopped using s_jobs before it notifies the app task.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    cleanup_resources();
}

bool load_corpus_b(lab04::Drawing &drawing)
{
    extern const std::uint8_t corpus_start[] asm("_binary_corpus_b_csv_start");
    extern const std::uint8_t corpus_end[] asm("_binary_corpus_b_csv_end");
    const char *cursor = reinterpret_cast<const char *>(corpus_start);
    const char *end = reinterpret_cast<const char *>(corpus_end);
    drawing.duration_ms = 2400;
    while (cursor < end) {
        const char *newline = static_cast<const char *>(std::memchr(cursor, '\n', end - cursor));
        const char *line_end = newline ? newline : end;
        if (cursor < line_end && *cursor != '#') {
            unsigned t, x0, y0, x1, y1, color, up;
            char line[96];
            const std::size_t len = static_cast<std::size_t>(line_end - cursor);
            if (len >= sizeof(line))
                return false;
            std::memcpy(line, cursor, len);
            line[len] = '\0';
            if (std::sscanf(line, "%u,%u,%u,%u,%u,%u,%u", &t, &x0, &y0, &x1, &y1, &color, &up) != 7 ||
                x0 >= 120 || y0 >= 120 || x1 >= 120 || y1 >= 120 || color >= 6 || up > 1)
                return false;
            drawing.segments.push_back({t, static_cast<std::uint8_t>(x0), static_cast<std::uint8_t>(y0),
                                        static_cast<std::uint8_t>(x1), static_cast<std::uint8_t>(y1),
                                        static_cast<std::uint8_t>(color), up != 0});
        }
        cursor = newline ? newline + 1 : end;
    }
    return lab04::valid_drawing(drawing);
}

bool load_corpus_a(lab04::Drawing &drawing, bool dense)
{
    extern const std::uint8_t short_start[] asm("_binary_short_sketch_start");
    extern const std::uint8_t short_end[] asm("_binary_short_sketch_end");
    extern const std::uint8_t dense_start[] asm("_binary_dense_sketch_start");
    extern const std::uint8_t dense_end[] asm("_binary_dense_sketch_end");
    const std::uint8_t *bytes = dense ? dense_start : short_start;
    const std::size_t size = static_cast<std::size_t>((dense ? dense_end : short_end) - bytes);
    if (size < 8 || std::memcmp(bytes, "L3SK", 4) != 0 || bytes[4] != 1 || bytes[5] != 0)
        return false;
    const std::uint16_t count = static_cast<std::uint16_t>(bytes[6] | (bytes[7] << 8));
    if (count > lab04::kMaximumSegments || size != 8u + static_cast<std::size_t>(count) * 10u)
        return false;
    const std::uint16_t rgb565[] = {0x001f, 0x07e0, 0xf800};
    drawing.duration_ms = 3000;
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint8_t *p = bytes + 8 + static_cast<std::size_t>(i) * 10;
        const std::uint32_t time = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                                   (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        const std::uint16_t color = static_cast<std::uint16_t>(p[8] | (p[9] << 8));
        std::uint8_t index = 0xff;
        for (std::uint8_t c = 0; c < 3; ++c)
            if (rgb565[c] == color)
                index = c;
        if (index == 0xff || p[4] >= 120 || p[5] >= 120 || p[6] >= 120 || p[7] >= 120 || time >= drawing.duration_ms)
            return false;
        drawing.segments.push_back({time, p[4], p[5], p[6], p[7], index, false});
    }
    return lab04::valid_drawing(drawing);
}

bool load_fixture(std::uint8_t fixture, lab04::Drawing &drawing)
{
    switch (fixture) {
        case 0:
            return load_corpus_a(drawing, false);
        case 1:
            return load_corpus_a(drawing, true);
        case 2:
            return load_corpus_b(drawing);
        default:
            return false;
    }
}

bool same_drawing(const lab04::Drawing &left, const lab04::Drawing &right)
{
    if (left.width != right.width || left.height != right.height ||
        left.duration_ms != right.duration_ms || left.segments.size() != right.segments.size())
        return false;
    for (std::size_t i = 0; i < left.segments.size(); ++i) {
        const auto &a = left.segments[i];
        const auto &b = right.segments[i];
        if (a.time_ms != b.time_ms || a.x0 != b.x0 || a.y0 != b.y0 ||
            a.x1 != b.x1 || a.y1 != b.y1 || a.color != b.color || a.pen_up != b.pen_up)
            return false;
    }
    return true;
}

void send_trace(const TraceEvent &event)
{
    if (xQueueSend(s_publications, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
        ++s_publication_queue_drops;
        s_harness_failure.publish();
    }
}

void sample_preview_decode_resources(TraceEvent &event, const lab04::Drawing &decoded,
                                     const lab04::Result &encoded)
{
    // SegmentPreview stores its final image contiguously at the tail of the
    // encoded byte vector. Keep that vector and decoded drawing alive until
    // this bounded point sample has been captured.
    if (encoded.bytes.size() < kPreviewBytes || decoded.segments.empty())
        return;
    event.resource_sample = true;
    event.sample_decoded_segments = static_cast<std::uint32_t>(decoded.segments.size());
    event.sample_decoded_capacity_bytes = static_cast<std::uint32_t>(
        decoded.segments.capacity() * sizeof(lab04::Segment));
    event.internal_largest_free_block = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    event.psram_largest_free_block = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    // The LVGL allocator is sampled under the BSP display mutex. This worker
    // never waits for UI work; a busy lock is recorded as an incomplete
    // resource sample rather than inventing a value.
    if (bsp_display_lock(0)) {
        lv_mem_monitor_t lvgl_memory{};
        lv_mem_monitor(&lvgl_memory);
        event.sample_lvgl_total_size = static_cast<std::uint32_t>(lvgl_memory.total_size);
        event.sample_lvgl_free_size = static_cast<std::uint32_t>(lvgl_memory.free_size);
        event.sample_lvgl_used_pct = lvgl_memory.used_pct;
        event.sample_lvgl_largest_free_block = lvgl_memory.free_biggest_size;
        event.sample_lvgl_max_used = static_cast<std::uint32_t>(lvgl_memory.max_used);
        event.sample_lvgl_available = true;
        bsp_display_unlock();
    }
}

// Called synchronously by the production decoder immediately after its
// maximum-capacity reserve and before inserting decoded records. The encoded
// preview result remains live in the caller for the entire decode.
void observe_preview_reservation(void *context, std::size_t capacity,
                                 std::size_t expected_segments)
{
    auto *event = static_cast<TraceEvent *>(context);
    if (event == nullptr)
        return;
    event->resource_peak = true;
    event->peak_expected_segments = static_cast<std::uint32_t>(expected_segments);
    event->peak_capacity_bytes = static_cast<std::uint32_t>(capacity * sizeof(lab04::Segment));
    event->peak_internal_largest_free_block = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    event->peak_psram_largest_free_block = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    // Keep Ledger A separate from Ledger B. A busy display lock leaves LVGL
    // fields unavailable and is reported explicitly when the row is emitted.
    if (bsp_display_lock(0)) {
        lv_mem_monitor_t memory{};
        lv_mem_monitor(&memory);
        event->peak_lvgl_total_size = static_cast<std::uint32_t>(memory.total_size);
        event->peak_lvgl_free_size = static_cast<std::uint32_t>(memory.free_size);
        event->peak_lvgl_used_pct = memory.used_pct;
        event->peak_lvgl_largest_free_block = memory.free_biggest_size;
        event->peak_lvgl_max_used = static_cast<std::uint32_t>(memory.max_used);
        event->peak_lvgl_available = true;
        bsp_display_unlock();
    }
}

void run_variant_one(std::uint8_t variant, std::uint8_t fixture)
{
    if (!lab04::valid_fixture_index(fixture)) {
        s_harness_failure.publish();
        return;
    }
    lab04::Drawing source;
    if (variant >= 3 || !load_fixture(fixture, source)) {
        TraceEvent failed{};
        std::strcpy(failed.scenario, "F-points");
        std::strncpy(failed.fixture, lab04::fixture_name(fixture), sizeof(failed.fixture) - 1);
        std::strcpy(failed.variant, "unsupported");
        std::strcpy(failed.terminal, "failure");
        std::strcpy(failed.reason, "fixture_invalid");
        failed.operation_id = s_next_operation++;
        failed.terminal_count = 1;
        failed.exploratory = true;
        send_trace(failed);
        return;
    }

    const auto encoding = s_encodings[variant];
    const char *name = s_variant_names[variant];
    if (variant == 2 && s_preview != nullptr) {
        heap_caps_free(s_preview);
        s_preview = nullptr;
    } else if (variant < 2 && s_preview == nullptr) {
        s_preview = heap_caps_calloc(1, kPreviewBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_preview == nullptr)
            s_preview = heap_caps_calloc(1, kPreviewBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (s_preview == nullptr) {
            TraceEvent failed{};
            std::strcpy(failed.scenario, variant == 0 ? "F-points" : "F-strokes");
            std::strncpy(failed.fixture, lab04::fixture_name(fixture), sizeof(failed.fixture) - 1);
            std::strncpy(failed.variant, name, sizeof(failed.variant) - 1);
            std::strcpy(failed.terminal, "failure");
            std::strcpy(failed.reason, "preview_alloc_failed");
            failed.operation_id = s_next_operation++;
            failed.terminal_count = 1;
            failed.exploratory = true;
            send_trace(failed);
            return;
        }
    }
    const std::uint64_t operation_id = s_next_operation++;
    const std::int64_t t0 = esp_timer_get_time();
    lab04::Result encoded = lab04::encode(source, encoding);
    const std::uint32_t encode_us = static_cast<std::uint32_t>(esp_timer_get_time() - t0);
    lab04::Drawing decoded;
    TraceEvent event{};
    std::string reason;
    const std::int64_t t1 = esp_timer_get_time();
    lab04::DecodeOperation operation(operation_id);
    const bool decoded_ok = encoded.ok && operation.run(
                                              encoded.bytes.data(), encoded.bytes.size(), encoding, decoded, false,
                                              variant == 2 ? observe_preview_reservation : nullptr, &event);
    const std::uint32_t decode_us = static_cast<std::uint32_t>(esp_timer_get_time() - t1);

    std::strcpy(event.scenario, variant == 0 ? "F-points" : variant == 1 ? "F-strokes"
                                                                         : "F-preview");
    std::strncpy(event.fixture, lab04::fixture_name(fixture), sizeof(event.fixture) - 1);
    std::strncpy(event.variant, name, sizeof(event.variant) - 1);
    const bool semantic_match = decoded_ok && same_drawing(source, decoded);
    const bool expected_size_failure = variant == 2 && fixture == 1 && !encoded.ok && encoded.reason == "input_too_large";
    std::strcpy(event.terminal, semantic_match ? "success" : "failure");
    std::strncpy(event.reason, semantic_match ? "none" : expected_size_failure ? "input_too_large"
                                                     : decoded_ok              ? "semantic_mismatch"
                                                                               : (encoded.ok ? operation.state().reason.c_str() : encoded.reason.c_str()),
                 sizeof(event.reason) - 1);
    if (expected_size_failure)
        std::strcpy(event.terminal, "failure");
    event.operation_id = operation_id;
    // Encoding is part of the accepted operation too. A model allocation
    // failure happens before DecodeOperation exists, but still has exactly
    // one failure terminal in the worker trace.
    event.terminal_count = expected_size_failure || !encoded.ok ? 1 : operation.state().terminal_count;
    event.encoded_bytes = static_cast<std::uint32_t>(encoded.bytes.size());
    const auto preview = lab04::preview_accounting(variant, encoded.ok, s_preview != nullptr);
    event.preview_bytes = preview.bytes;
    event.preview_buffers = preview.buffers;
    event.live_buffers = static_cast<std::uint32_t>(operation.state().live_buffers);
    event.encode_us = encode_us;
    event.decode_us = decode_us;
    event.exploratory = true;

    if (variant == 2) {
        event.resource_peak = semantic_match && encoded.ok && event.resource_peak;
    }
    if (semantic_match && variant == 2) {
        sample_preview_decode_resources(event, decoded, encoded);
    }

    std::uint16_t *preview_pixels = nullptr;
    if (semantic_match && variant == 2 && encoded.bytes.size() >= kPreviewBytes) {
        preview_pixels = reinterpret_cast<std::uint16_t *>(encoded.bytes.data() + encoded.bytes.size() - kPreviewBytes);
    } else if (semantic_match) {
        preview_pixels = static_cast<std::uint16_t *>(s_preview);
    }
    if (semantic_match && preview_pixels != nullptr) {
        event.position_ms = decoded.duration_ms;
        event.raster_eligible = static_cast<std::uint32_t>(std::upper_bound(
                                                               decoded.segments.begin(), decoded.segments.end(), event.position_ms,
                                                               [](std::uint32_t position, const lab04::Segment &segment) {
                                                                   return position < segment.time_ms;
                                                               }) -
                                                           decoded.segments.begin());
        std::size_t rendered = 0;
        const std::int64_t raster_start = esp_timer_get_time();
        const std::size_t raster_work = lab04::raster_update(
            preview_pixels, decoded, rendered, event.position_ms, kPalette,
            lab04::kRasterWorkCap, &reason);
        event.raster_us = static_cast<std::uint32_t>(esp_timer_get_time() - raster_start);
        event.raster_rendered = static_cast<std::uint32_t>(raster_work);
        event.raster_remaining = event.raster_eligible - event.raster_rendered;
    }
    event.internal_largest_free_block = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    event.psram_largest_free_block = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    send_trace(event);

    if (fixture == 1) {
        const std::uint32_t positions[] = {1u, source.duration_ms / 2u, source.duration_ms - 1u};
        const char *position_names[] = {"early", "middle", "late"};
        for (std::size_t i = 0; i < 3; ++i) {
            const std::int64_t t = esp_timer_get_time();
            const auto at_position = lab04::checkpoint_seek(encoding, source, positions[i]);
            TraceEvent seek_event{};
            std::strcpy(seek_event.scenario, "F-checkpoint");
            std::strcpy(seek_event.fixture, "corpus-a-dense");
            std::strncpy(seek_event.variant, name, sizeof(seek_event.variant) - 1);
            std::strcpy(seek_event.terminal, at_position.outcome == lab04::CheckpointOutcome::Unsupported ? "unsupported" : "success");
            std::strncpy(seek_event.reason, at_position.reason.c_str(), sizeof(seek_event.reason) - 1);
            seek_event.operation_id = s_next_operation++;
            seek_event.terminal_count = 1;
            seek_event.position_ms = positions[i];
            std::strncpy(seek_event.position, position_names[i], sizeof(seek_event.position) - 1);
            seek_event.seek_us = static_cast<std::uint32_t>(esp_timer_get_time() - t);
            std::snprintf(seek_event.variant, sizeof(seek_event.variant), "%s", name);
            seek_event.exploratory = true;
            send_trace(seek_event);
        }
    }
}

void run_variant(std::uint8_t variant)
{
    for (std::uint8_t fixture = 0; fixture < 3; ++fixture)
        run_variant_one(variant, fixture);
}

void run_faults(std::uint8_t variant)
{
    lab04::Drawing source;
    if (!load_corpus_b(source)) {
        s_harness_failure.publish();
        return;
    }
    if (variant >= 3)
        return;
    const auto encoding = s_encodings[variant];
    const char *variant_name = s_variant_names[variant];
    const auto encoded = lab04::encode(source, encoding);

    auto emit = [variant_name](const char *scenario, const char *terminal, const char *reason,
                               std::uint64_t op, std::uint32_t count, std::uint32_t live) {
        TraceEvent event{};
        std::strncpy(event.scenario, scenario, sizeof(event.scenario) - 1);
        std::strcpy(event.fixture, "corpus-b");
        std::strncpy(event.variant, variant_name, sizeof(event.variant) - 1);
        std::strncpy(event.terminal, terminal, sizeof(event.terminal) - 1);
        std::strncpy(event.reason, reason, sizeof(event.reason) - 1);
        event.operation_id = op;
        event.terminal_count = count;
        event.live_buffers = live;
        send_trace(event);
    };

    lab04::Drawing destination = source;
    std::string reason;
    const auto truncated_id = s_next_operation++;
    lab04::DecodeOperation truncated(truncated_id);
    truncated.run(encoded.bytes.data(), encoded.bytes.size() - 1, encoding, destination);
    emit("F-malformed", "failure", truncated.state().reason.c_str(), truncated_id,
         truncated.state().terminal_count, truncated.state().live_buffers);

    const auto oversized_id = s_next_operation++;
    std::vector<std::uint8_t> oversized(lab04::kMaximumInputSize + 1, 0);
    lab04::DecodeOperation oversized_decode(oversized_id);
    oversized_decode.run(oversized.data(), oversized.size(), encoding, destination);
    emit("F-malformed", "failure", oversized_decode.state().reason.c_str(), oversized_id,
         oversized_decode.state().terminal_count, oversized_decode.state().live_buffers);

    const auto color_id = s_next_operation++;
    std::vector<std::uint8_t> wrong_color = encoded.bytes;
    const std::size_t color_offset = encoding == lab04::Encoding::TimedStrokes ? 16 : 24;
    if (wrong_color.size() > color_offset)
        wrong_color[color_offset] = 6;
    lab04::DecodeOperation wrong_color_decode(color_id);
    wrong_color_decode.run(wrong_color.data(), wrong_color.size(), encoding, destination);
    emit("F-malformed", "failure", wrong_color_decode.state().reason.c_str(), color_id,
         wrong_color_decode.state().terminal_count, wrong_color_decode.state().live_buffers);

    const auto overflow_id = s_next_operation++;
    lab04::EncodeQueue queue;
    for (std::size_t i = 0; i < lab04::kEncodeQueueCapacity; ++i)
        queue.push(source);
    const bool accepted = queue.push(source);
    emit("F-overflow", accepted ? "success" : "failure", accepted ? "unexpected_accept" : "overflow",
         overflow_id, queue.operation().terminal_count, 0);

    const auto cancel_id = s_next_operation++;
    lab04::DecodeOperation cancelled(cancel_id);
    cancelled.run(encoded.bytes.data(), encoded.bytes.size(), encoding, destination, true);
    emit("F-cancel", "cancelled", cancelled.state().reason.c_str(), cancel_id,
         cancelled.state().terminal_count, cancelled.state().live_buffers);

    if (variant == 0) {
        lab04::Drawing legacy = source;
        for (auto &segment : legacy.segments)
            segment.color = lab04::kUncolored;
        const auto legacy_result = lab04::encode(legacy, lab04::Encoding::LegacyPoints);
        TraceEvent legacy_event{};
        std::strcpy(legacy_event.scenario, "F-legacy");
        std::strcpy(legacy_event.fixture, "corpus-b");
        std::strcpy(legacy_event.variant, "legacy");
        std::strcpy(legacy_event.terminal, legacy_result.ok ? "success" : "failure");
        std::strncpy(legacy_event.reason, legacy_result.ok ? "uncolored_compat" : legacy_result.reason.c_str(), sizeof(legacy_event.reason) - 1);
        legacy_event.operation_id = s_next_operation++;
        legacy_event.terminal_count = 1;
        legacy_event.exploratory = true;
        send_trace(legacy_event);

        const auto colored_legacy = lab04::encode(source, lab04::Encoding::LegacyPoints);
        TraceEvent unsupported_legacy{};
        std::strcpy(unsupported_legacy.scenario, "F-legacy");
        std::strcpy(unsupported_legacy.fixture, "corpus-b");
        std::snprintf(unsupported_legacy.variant, sizeof(unsupported_legacy.variant), "%s", "legacy-colored");
        std::strcpy(unsupported_legacy.terminal, colored_legacy.ok ? "success" : "unsupported");
        std::strncpy(unsupported_legacy.reason, colored_legacy.ok ? "unexpected_legacy_accept" : colored_legacy.reason.c_str(), sizeof(unsupported_legacy.reason) - 1);
        unsupported_legacy.operation_id = s_next_operation++;
        unsupported_legacy.terminal_count = 1;
        unsupported_legacy.exploratory = true;
        send_trace(unsupported_legacy);
    }
}

void worker(void *)
{
    Job job{};
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE)
            continue;
        if (job.kind == JobKind::Variant || job.kind == JobKind::Faults) {
            try {
                if (job.kind == JobKind::Variant)
                    run_variant(job.variant);
                else
                    run_faults(job.variant);
            } catch (const std::bad_alloc &) {
                // A failure outside the model's guarded boundaries must still
                // terminate this accepted worker job and leave the harness
                // visibly stopped through observe_completion().
                TraceEvent failed{};
                const std::uint8_t variant = job.variant < 3 ? job.variant : 0;
                std::strcpy(failed.scenario, job.kind == JobKind::Variant
                                                 ? (variant == 0 ? "F-points" : variant == 1 ? "F-strokes"
                                                                                             : "F-preview")
                                                 : "F-malformed");
                std::strcpy(failed.fixture, "corpus-b");
                std::strncpy(failed.variant, s_variant_names[variant], sizeof(failed.variant) - 1);
                std::strcpy(failed.terminal, "failure");
                std::strcpy(failed.reason, "no_memory");
                failed.operation_id = s_next_operation++;
                failed.terminal_count = 1;
                failed.exploratory = true;
                send_trace(failed);
            }
        } else {
            if (s_preview != nullptr) {
                heap_caps_free(s_preview);
                s_preview = nullptr;
            }
            xTaskNotifyGive(s_app_task);
            vTaskDelete(nullptr);
        }
    }
}

void submit_variant(lv_event_t *event)
{
    const std::uint8_t variant = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(lv_event_get_user_data(event)));
    if (variant >= 3 || s_manual_variant_accepted[variant]) {
        if (s_status_label != nullptr)
            lv_label_set_text(s_status_label, "Already selected - choose remaining controls");
        return;
    }
    const Job job{JobKind::Variant, variant};
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        s_harness_failure.publish_queue_overflow();
        if (s_status_label != nullptr)
            lv_label_set_text(s_status_label, "Busy - retry this control");
        return;
    }
    s_manual_variant_accepted[variant] = true;
}

void submit_faults(lv_event_t *)
{
    if (s_manual_faults_accepted) {
        if (s_status_label != nullptr)
            lv_label_set_text(s_status_label, "Fault cases already selected");
        return;
    }
    const std::uint8_t missing_mask = (s_manual_variant_accepted[0] ? 0 : 1) |
                                      (s_manual_variant_accepted[1] ? 0 : 2) |
                                      (s_manual_variant_accepted[2] ? 0 : 4);
    if (missing_mask != 0) {
        static const char *const missing_status[] = {
            "", "Need Points", "Need Strokes", "Need Points + Strokes",
            "Need Preview", "Need Points + Preview", "Need Strokes + Preview",
            "Need Points + Strokes + Preview"};
        const char *status = missing_status[missing_mask];
        if (s_status_label != nullptr)
            lv_label_set_text(s_status_label, status);
        return;
    }
    for (std::uint8_t variant = 0; variant < 3; ++variant) {
        const Job job{JobKind::Faults, variant};
        if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
            s_harness_failure.publish_queue_overflow();
            if (s_status_label != nullptr)
                lv_label_set_text(s_status_label, "Busy - fault cases incomplete");
            return;
        }
    }
    s_manual_faults_accepted = true;
    if (s_status_label != nullptr)
        lv_label_set_text(s_status_label, "Fault cases queued - please wait");
}

bool add_button(lv_obj_t *parent, const char *text, std::uint8_t variant, int x)
{
    lv_obj_t *button = lv_button_create(parent);
    if (button == nullptr || s_control_count >= sizeof(s_controls) / sizeof(s_controls[0]))
        return false;
    s_controls[s_control_count++] = button;
    lv_obj_set_size(button, 96, 42);
    lv_obj_set_pos(button, x, 24);
    lv_obj_add_event_cb(button, submit_variant, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<std::uintptr_t>(variant)));
    lv_obj_t *label = lv_label_create(button);
    if (label == nullptr)
        return false;
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return true;
}

bool write_sequenced_line(const char *line, int length)
{
    ++s_sequenced_rows_attempted;
    if (length < 0) {
        ++s_serial_write_errors;
        s_harness_failure.publish();
        return false;
    }
    const std::size_t written = std::fwrite(line, 1, static_cast<std::size_t>(length), stdout);
    const bool flushed = std::fflush(stdout) == 0;
    if (written != static_cast<std::size_t>(length) || !flushed || std::ferror(stdout)) {
        ++s_serial_write_errors;
        s_harness_failure.publish();
        return false;
    }
    return true;
}

bool write_sequenced_trace(const char *format, ...)
{
    constexpr std::size_t kTraceLineCapacity = 1536;
    char line[kTraceLineCapacity];
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(line))
        return write_sequenced_line(line, -1);
    return write_sequenced_line(line, length);
}

void write_unsequenced_trace(const char *format, ...)
{
    constexpr std::size_t kTraceLineCapacity = 512;
    char line[kTraceLineCapacity];
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(line))
        return;
    const std::size_t written = std::fwrite(line, 1, static_cast<std::size_t>(length), stdout);
    (void)std::fflush(stdout);
    (void)(written == static_cast<std::size_t>(length) && !std::ferror(stdout));
}

void emit_trace(const TraceEvent &event)
{
    if (event.resource_peak) {
        const std::uint32_t seq = ++s_trace_seq;
        constexpr std::size_t kTraceLineCapacity = 1536;
        char line[kTraceLineCapacity];
        if (event.peak_lvgl_available) {
            const int length = std::snprintf(line, sizeof(line),
                                             "LAB04_TRACE schema=lab04-trace/v1 event=resource_peak seq=%" PRIu32
                                             " monotonic_us=%" PRIi64 " scenario=F-preview fixture=%s variant=preview"
                                             " operation_id=%" PRIu64 " sample_kind=peak"
                                             " phase=maximum_output_capacity_reserved_before_record_insertion_encoded_payload_live"
                                             " ledger_b_image_resident=1 ledger_b_image_storage=encoded_payload"
                                             " ledger_b_image_bytes=28800 ledger_b_image_caps=MALLOC_CAP_DEFAULT"
                                             " ledger_b_image_lifetime=encoded_result_live_through_raster"
                                             " ledger_b_decoded_resident=1 ledger_b_expected_output_segments=%" PRIu32
                                             " ledger_b_decoded_capacity_bytes=%" PRIu32
                                             " image_resident=1 image_storage=encoded_payload image_bytes=28800"
                                             " decoded_resident=1 expected_output_segments=%" PRIu32
                                             " decoded_capacity_bytes=%" PRIu32
                                             " internal_largest_free_block_bytes=%" PRIu32 " psram_largest_free_block_bytes=%" PRIu32
                                             " lvgl_total_size_bytes=%" PRIu32 " lvgl_free_size_bytes=%" PRIu32
                                             " lvgl_used_pct=%" PRIu32 " lvgl_free_biggest_size_bytes=%" PRIu32
                                             " lvgl_largest_free_block_bytes=%" PRIu32 " lvgl_max_used_bytes=%" PRIu32 "\n",
                                             seq, static_cast<std::int64_t>(esp_timer_get_time()), event.fixture,
                                             event.operation_id, event.peak_expected_segments, event.peak_capacity_bytes,
                                             event.peak_expected_segments, event.peak_capacity_bytes,
                                             event.peak_internal_largest_free_block, event.peak_psram_largest_free_block,
                                             event.peak_lvgl_total_size, event.peak_lvgl_free_size, event.peak_lvgl_used_pct,
                                             event.peak_lvgl_largest_free_block, event.peak_lvgl_largest_free_block,
                                             event.peak_lvgl_max_used);
            write_sequenced_line(line, length < static_cast<int>(sizeof(line)) ? length : -1);
        } else {
            const int length = std::snprintf(line, sizeof(line),
                                             "LAB04_TRACE schema=lab04-trace/v1 event=resource_peak seq=%" PRIu32
                                             " monotonic_us=%" PRIi64 " scenario=F-preview fixture=%s variant=preview"
                                             " operation_id=%" PRIu64 " sample_kind=peak"
                                             " phase=maximum_output_capacity_reserved_before_record_insertion_encoded_payload_live"
                                             " ledger_b_image_resident=1 ledger_b_image_storage=encoded_payload"
                                             " ledger_b_image_bytes=28800 ledger_b_image_caps=MALLOC_CAP_DEFAULT"
                                             " ledger_b_image_lifetime=encoded_result_live_through_raster"
                                             " ledger_b_decoded_resident=1 ledger_b_expected_output_segments=%" PRIu32
                                             " ledger_b_decoded_capacity_bytes=%" PRIu32
                                             " image_resident=1 image_storage=encoded_payload image_bytes=28800"
                                             " decoded_resident=1 expected_output_segments=%" PRIu32
                                             " decoded_capacity_bytes=%" PRIu32
                                             " internal_largest_free_block_bytes=%" PRIu32 " psram_largest_free_block_bytes=%" PRIu32
                                             " lvgl_sample_status=display_lock_busy\n",
                                             seq, static_cast<std::int64_t>(esp_timer_get_time()), event.fixture,
                                             event.operation_id, event.peak_expected_segments, event.peak_capacity_bytes, event.peak_expected_segments,
                                             event.peak_capacity_bytes, event.peak_internal_largest_free_block,
                                             event.peak_psram_largest_free_block);
            write_sequenced_line(line, length < static_cast<int>(sizeof(line)) ? length : -1);
        }
    }
    if (event.resource_sample) {
        const std::uint32_t seq = ++s_trace_seq;
        constexpr std::size_t kTraceLineCapacity = 1536;
        char line[kTraceLineCapacity];
        if (event.sample_lvgl_available) {
            const int length = std::snprintf(line, sizeof(line),
                                             "LAB04_TRACE schema=lab04-trace/v1 event=resource_sample seq=%" PRIu32
                                             " monotonic_us=%" PRIi64 " scenario=F-preview fixture=%s variant=preview"
                                             " operation_id=%" PRIu64 " sample_kind=point phase=post_decode_pre_raster"
                                             " ledger_b_image_resident=1 ledger_b_image_storage=encoded_payload"
                                             " ledger_b_image_bytes=28800 ledger_b_image_caps=MALLOC_CAP_DEFAULT"
                                             " ledger_b_image_lifetime=encoded_result_live_through_raster"
                                             " ledger_b_decoded_resident=1 ledger_b_decoded_segments=%" PRIu32
                                             " ledger_b_decoded_capacity_bytes=%" PRIu32
                                             " image_resident=1 image_storage=encoded_payload image_bytes=28800"
                                             " decoded_resident=1 decoded_segments=%" PRIu32
                                             " internal_largest_free_block_bytes=%" PRIu32 " psram_largest_free_block_bytes=%" PRIu32
                                             " lvgl_total_size_bytes=%" PRIu32 " lvgl_free_size_bytes=%" PRIu32
                                             " lvgl_used_pct=%" PRIu32 " lvgl_free_biggest_size_bytes=%" PRIu32
                                             " lvgl_largest_free_block_bytes=%" PRIu32 " lvgl_max_used_bytes=%" PRIu32 "\n",
                                             seq, static_cast<std::int64_t>(esp_timer_get_time()), event.fixture,
                                             event.operation_id, event.sample_decoded_segments, event.sample_decoded_capacity_bytes,
                                             event.sample_decoded_segments, event.internal_largest_free_block,
                                             event.psram_largest_free_block, event.sample_lvgl_total_size, event.sample_lvgl_free_size,
                                             event.sample_lvgl_used_pct, event.sample_lvgl_largest_free_block,
                                             event.sample_lvgl_largest_free_block, event.sample_lvgl_max_used);
            write_sequenced_line(line, length < static_cast<int>(sizeof(line)) ? length : -1);
        } else {
            const int length = std::snprintf(line, sizeof(line),
                                             "LAB04_TRACE schema=lab04-trace/v1 event=resource_sample seq=%" PRIu32
                                             " monotonic_us=%" PRIi64 " scenario=F-preview fixture=%s variant=preview"
                                             " operation_id=%" PRIu64 " sample_kind=point phase=post_decode_pre_raster"
                                             " ledger_b_image_resident=1 ledger_b_image_storage=encoded_payload"
                                             " ledger_b_image_bytes=28800 ledger_b_image_caps=MALLOC_CAP_DEFAULT"
                                             " ledger_b_image_lifetime=encoded_result_live_through_raster"
                                             " ledger_b_decoded_resident=1 ledger_b_decoded_segments=%" PRIu32
                                             " ledger_b_decoded_capacity_bytes=%" PRIu32
                                             " image_resident=1 image_storage=encoded_payload image_bytes=28800"
                                             " decoded_resident=1 decoded_segments=%" PRIu32
                                             " internal_largest_free_block_bytes=%" PRIu32 " psram_largest_free_block_bytes=%" PRIu32
                                             " lvgl_sample_status=display_lock_busy\n",
                                             seq, static_cast<std::int64_t>(esp_timer_get_time()), event.fixture, event.operation_id,
                                             event.sample_decoded_segments, event.sample_decoded_capacity_bytes,
                                             event.sample_decoded_segments, event.internal_largest_free_block,
                                             event.psram_largest_free_block);
            write_sequenced_line(line, length < static_cast<int>(sizeof(line)) ? length : -1);
        }
    }
    lv_mem_monitor_t lvgl_memory{};
    char lvgl_suffix[80];
    if (bsp_display_lock(0)) {
        lv_mem_monitor(&lvgl_memory);
        bsp_display_unlock();
        std::snprintf(lvgl_suffix, sizeof(lvgl_suffix), "lvgl_largest_free_block_bytes=%" PRIu32,
                      static_cast<std::uint32_t>(lvgl_memory.free_biggest_size));
    } else {
        std::snprintf(lvgl_suffix, sizeof(lvgl_suffix), "lvgl_sample_status=display_lock_busy");
    }
    write_sequenced_trace("LAB04_TRACE schema=lab04-trace/v1 event=scenario_terminal seq=%" PRIu32
                          " monotonic_us=%" PRIi64 " scenario=%s fixture=%s class=%s variant=%s checkpoint_position=%s"
                          " operation_id=%" PRIu64 " terminal=%s terminal_count=%" PRIu32 " reason=%s encoded_bytes=%" PRIu32
                          " preview_bytes=%" PRIu32 " preview_buffers=%" PRIu32 " live_buffers=%" PRIu32
                          " position_ms=%" PRIu32 " encode_us=%" PRIu32 " decode_us=%" PRIu32
                          " raster_us=%" PRIu32 " raster_work_cap=%" PRIu32 " raster_eligible=%" PRIu32
                          " raster_rendered=%" PRIu32 " raster_remaining=%" PRIu32 " seek_us=%" PRIu32
                          " internal_largest_free_block_bytes=%" PRIu32 " psram_largest_free_block_bytes=%" PRIu32 " %s\n",
                          ++s_trace_seq, static_cast<std::int64_t>(esp_timer_get_time()), event.scenario, event.fixture,
                          event.exploratory ? "exploratory" : "decided", event.variant, event.position,
                          event.operation_id, event.terminal, event.terminal_count, event.reason, event.encoded_bytes,
                          event.preview_bytes, event.preview_buffers, event.live_buffers, event.position_ms,
                          event.encode_us, event.decode_us, event.raster_us,
                          static_cast<std::uint32_t>(lab04::kRasterWorkCap), event.raster_eligible,
                          event.raster_rendered, event.raster_remaining, event.seek_us,
                          event.internal_largest_free_block, event.psram_largest_free_block,
                          lvgl_suffix);
}

void observe_completion(const TraceEvent &event)
{
    auto bad = [] { s_harness_failure.publish(); };
    if (event.operation_id == 0 || event.terminal_count != 1) {
        bad();
        return;
    }
    for (std::size_t i = 0; i < s_accepted_operation_count; ++i) {
        if (s_accepted_operation_ids[i] == event.operation_id) {
            bad();
            return;
        }
    }
    if (s_accepted_operation_count >= sizeof(s_accepted_operation_ids) / sizeof(s_accepted_operation_ids[0])) {
        bad();
        return;
    }
    s_accepted_operation_ids[s_accepted_operation_count++] = event.operation_id;
    std::uint8_t variant = std::strcmp(event.variant, "points") == 0 ? 0 : std::strcmp(event.variant, "strokes") == 0 ? 1
                                                                       : std::strcmp(event.variant, "preview") == 0   ? 2
                                                                                                                      : 0xff;
    if (std::strcmp(event.scenario, "F-points") == 0 || std::strcmp(event.scenario, "F-strokes") == 0 || std::strcmp(event.scenario, "F-preview") == 0) {
        const std::uint8_t expected_variant = std::strcmp(event.scenario, "F-points") == 0 ? 0 : std::strcmp(event.scenario, "F-strokes") == 0 ? 1
                                                                                                                                               : 2;
        const std::uint8_t fixture = std::strcmp(event.fixture, "corpus-a-short") == 0 ? 0 : std::strcmp(event.fixture, "corpus-a-dense") == 0 ? 1
                                                                                         : std::strcmp(event.fixture, "corpus-b") == 0         ? 2
                                                                                                                                               : 0xff;
        const bool dense_preview_limit = expected_variant == 2 && fixture == 1;
        if (fixture > 2 || variant != expected_variant ||
            (dense_preview_limit ? (std::strcmp(event.terminal, "failure") != 0 || std::strcmp(event.reason, "input_too_large") != 0) : std::strcmp(event.terminal, "success") != 0))
            bad();
        else {
            s_variant_seen[fixture] |= static_cast<std::uint8_t>(1u << expected_variant);
            ++s_variant_events;
        }
    } else if (std::strcmp(event.scenario, "F-checkpoint") == 0) {
        std::uint8_t pos = event.position_ms == 1 ? 0 : event.position_ms == 1500 ? 1
                                                    : event.position_ms == 2999   ? 2
                                                                                  : 0xff;
        if (variant > 2 || std::strcmp(event.fixture, "corpus-a-dense") != 0 || pos > 2 ||
            std::strcmp(event.terminal, "unsupported") != 0 || std::strcmp(event.reason, "unsupported") != 0)
            bad();
        else {
            s_checkpoint_positions[variant] |= static_cast<std::uint8_t>(1u << pos);
            ++s_checkpoint_events;
        }
    } else if (std::strcmp(event.scenario, "F-malformed") == 0) {
        std::uint8_t reason_bit = std::strcmp(event.reason, "truncated_records") == 0 || std::strcmp(event.reason, "truncated_stroke") == 0 ? 1 : std::strcmp(event.reason, "input_too_large") == 0 ? 2
                                                                                                                                              : std::strcmp(event.reason, "wrong_color_count") == 0 ? 4
                                                                                                                                                                                                    : 0;
        if (variant > 2 || std::strcmp(event.fixture, "corpus-b") != 0 || std::strcmp(event.terminal, "failure") != 0 || reason_bit == 0)
            bad();
        else {
            s_malformed_reasons[variant] |= reason_bit;
            ++s_malformed_events;
        }
    } else if (std::strcmp(event.scenario, "F-overflow") == 0) {
        if (variant > 2 || std::strcmp(event.fixture, "corpus-b") != 0 || std::strcmp(event.terminal, "failure") != 0 || std::strcmp(event.reason, "overflow") != 0)
            bad();
        else {
            s_fault_seen[variant] |= 1;
            ++s_overflow_events;
        }
    } else if (std::strcmp(event.scenario, "F-cancel") == 0) {
        if (variant > 2 || std::strcmp(event.fixture, "corpus-b") != 0 || std::strcmp(event.terminal, "cancelled") != 0 || event.live_buffers != 0)
            bad();
        else {
            s_fault_seen[variant] |= 2;
            ++s_cancel_events;
        }
    } else if (std::strcmp(event.scenario, "F-legacy") == 0) {
        if (std::strcmp(event.variant, "legacy") == 0 && std::strcmp(event.terminal, "success") == 0 && std::strcmp(event.reason, "uncolored_compat") == 0)
            s_legacy_seen |= 1;
        else if (std::strcmp(event.variant, "legacy-colored") == 0 && std::strcmp(event.terminal, "unsupported") == 0 && std::strcmp(event.reason, "unsupported_legacy") == 0)
            s_legacy_seen |= 2;
        else
            bad();
        ++s_legacy_events;
    } else
        bad();

    if (s_variant_events == 9 && s_checkpoint_events == 9 && s_malformed_events == 9 &&
        s_overflow_events == 3 && s_cancel_events == 3 && s_legacy_events == 2 && s_legacy_seen == 3 &&
        s_variant_seen[0] == 7 && s_variant_seen[1] == 7 && s_variant_seen[2] == 7 &&
        s_checkpoint_positions[0] == 7 && s_checkpoint_positions[1] == 7 && s_checkpoint_positions[2] == 7 &&
        s_malformed_reasons[0] == 7 && s_malformed_reasons[1] == 7 && s_malformed_reasons[2] == 7 &&
        s_fault_seen[0] == 3 && s_fault_seen[1] == 3 && s_fault_seen[2] == 3 && !s_harness_failure.failed())
        s_scenarios_complete = true;
}

#ifdef CONFIG_LAB04_SCRIPTED
void scripted_run()
{
    for (std::uint8_t variant = 0; variant < 3; ++variant) {
        const Job job{JobKind::Variant, variant};
        if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
            s_harness_failure.publish();
            return;
        }
    }
    for (std::uint8_t variant = 0; variant < 3; ++variant) {
        const Job faults{JobKind::Faults, variant};
        if (xQueueSend(s_jobs, &faults, 0) != pdTRUE) {
            s_harness_failure.publish();
            return;
        }
    }
}
#endif
} // namespace

extern "C" void app_main(void)
{
    s_app_task = xTaskGetCurrentTaskHandle();
    s_jobs = xQueueCreate(kWorkerQueueCapacity, sizeof(Job));
    s_publications = xQueueCreate(16, sizeof(TraceEvent));
    if (s_jobs == nullptr || s_publications == nullptr) {
        cleanup_resources();
        std::puts("LAB04_INIT_FAILED status=queue_init_failed");
        std::puts("LAB04_HARNESS_DONE status=failed variants=0 checkpoints=0");
        return;
    }
    s_preview = heap_caps_calloc(1, kPreviewBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_preview == nullptr)
        s_preview = heap_caps_calloc(1, kPreviewBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_preview == nullptr) {
        cleanup_resources();
        std::puts("LAB04_INIT_FAILED status=preview_alloc_failed");
        std::puts("LAB04_HARNESS_DONE status=failed variants=0 checkpoints=0");
        return;
    }
    if (xTaskCreate(worker, "lab04_worker", 8192, nullptr, 5, nullptr) != pdPASS) {
        cleanup_resources();
        std::puts("LAB04_INIT_FAILED status=worker_init_failed");
        std::puts("LAB04_HARNESS_DONE status=failed variants=0 checkpoints=0");
        return;
    }

    // Queue, publication channel, preview memory, and worker are ready before
    // any queue-submitting LVGL callback is installed.
    lv_display_t *display = bsp_display_start();
    if (display == nullptr) {
        stop_worker_and_cleanup(true);
        std::puts("LAB04_INIT_FAILED status=display_start_failed");
        std::puts("LAB04_HARNESS_DONE status=failed variants=0 checkpoints=0");
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000)) {
        stop_worker_and_cleanup(true);
        std::puts("LAB04_INIT_FAILED status=display_lock_failed");
        std::puts("LAB04_HARNESS_DONE status=failed variants=0 checkpoints=0");
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    bool controls_ready = screen != nullptr;
    if (controls_ready) {
        lv_obj_set_scrollable(screen, false);
        s_status_label = lv_label_create(screen);
        controls_ready = s_status_label != nullptr;
    }
    if (controls_ready) {
        lv_label_set_text(s_status_label, "Select a format comparison");
        lv_obj_set_pos(s_status_label, 8, 4);
        s_instructions_label = lv_label_create(screen);
        controls_ready = s_instructions_label != nullptr;
    }
    if (controls_ready) {
        lv_label_set_long_mode(s_instructions_label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(s_instructions_label, 304);
        lv_obj_set_pos(s_instructions_label, 8, 124);
        lv_label_set_text(s_instructions_label,
                          "Goal: compare 3 sketch encodings.\n"
                          "No product format is selected.\n"
                          "Tap Points, Strokes, Preview once each.\n"
                          "Then Fault cases once (bad input,\n"
                          "overflow, cancellation).\n"
                          "Wait for Complete; reset to rerun.");
        controls_ready = add_button(screen, "Points", 0, 8) &&
                         add_button(screen, "Strokes", 1, 112) &&
                         add_button(screen, "Preview", 2, 216);
        lv_obj_t *fault_button = controls_ready ? lv_button_create(screen) : nullptr;
        controls_ready = controls_ready && fault_button != nullptr && s_control_count < sizeof(s_controls) / sizeof(s_controls[0]);
        if (controls_ready) {
            s_controls[s_control_count++] = fault_button;
            lv_obj_set_size(fault_button, 120, 42);
            lv_obj_set_pos(fault_button, 8, 70);
            lv_obj_add_event_cb(fault_button, submit_faults, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *fault_label = lv_label_create(fault_button);
            controls_ready = fault_label != nullptr;
            if (controls_ready) {
                lv_label_set_text(fault_label, "Fault cases");
                lv_obj_center(fault_label);
            }
        }
    }
    bsp_display_unlock();
    if (!controls_ready) {
        stop_worker_and_cleanup(true);
        std::puts("LAB04_INIT_FAILED status=controls_init_failed");
        std::puts("LAB04_HARNESS_DONE status=failed variants=0 checkpoints=0");
        return;
    }

#ifdef CONFIG_LAB04_SCRIPTED
    const char *mode = "scripted";
#else
    const char *mode = "manual";
#endif
    std::printf("LAB04_READY build=%s mode=%s server=none trace_schema=lab04-trace/v1\n",
                CONFIG_LAB04_BUILD_ID, mode);
#ifdef CONFIG_LAB04_SCRIPTED
    scripted_run();
#endif

    TraceEvent event{};
    for (;;) {
        while (xQueueReceive(s_publications, &event, 0) == pdTRUE) {
            emit_trace(event);
            observe_completion(event);
            if (s_status_label != nullptr && bsp_display_lock(0)) {
                char status[48];
                std::snprintf(status, sizeof(status), "Last: %s %s", event.scenario, event.terminal);
                lv_label_set_text(s_status_label, status);
                bsp_display_unlock();
            }
        }
        const std::uint32_t overflow_count = s_harness_failure.queue_overflows();
        if (overflow_count > s_queue_overflows_reported) {
            s_queue_overflows_reported = overflow_count;
            std::printf("LAB04_TRACE monotonic_us=%" PRIi64 " event=worker_queue_overflow capacity=8 count=%" PRIu32 "\n",
                        static_cast<std::int64_t>(esp_timer_get_time()), overflow_count);
        }
        if ((s_scenarios_complete || s_harness_failure.failed()) && !s_stop_requested) {
            // Drain any in-flight input callback and remove controls before
            // enqueueing Stop, so no new work can be accepted behind it.
            while (!disable_input_callbacks())
                vTaskDelay(1);
            const Job stop{JobKind::Stop, 0};
            s_stop_requested = xQueueSend(s_jobs, &stop, 0) == pdTRUE;
        }
        if (s_stop_requested && ulTaskNotifyTake(pdTRUE, 0) > 0) {
            // The worker has stopped using the queue; callbacks were already
            // drained and removed before Stop was submitted.
            // Drain its final publications before reporting terminal status.
            while (xQueueReceive(s_publications, &event, 0) == pdTRUE) {
                emit_trace(event);
                observe_completion(event);
                if (s_status_label != nullptr && bsp_display_lock(0)) {
                    char status[48];
                    std::snprintf(status, sizeof(status), "Last: %s %s", event.scenario, event.terminal);
                    lv_label_set_text(s_status_label, status);
                    bsp_display_unlock();
                }
            }
            cleanup_resources();
            write_unsequenced_trace("LAB04_TRACE schema=lab04-trace/v1 event=publication_loss"
                                    " source=firmware_publication_queue dropped_events=%" PRIu32 "\n",
                                    s_publication_queue_drops);
            // This unsequenced footer reconciles all sequenced row attempts;
            // its own write is intentionally outside expected_rows/errors.
            write_unsequenced_trace("LAB04_TRACE schema=lab04-trace/v1 event=trace_summary"
                                    " expected_rows=%" PRIu32 " publication_queue_drops=%" PRIu32
                                    " serial_write_errors=%" PRIu32 "\n",
                                    s_sequenced_rows_attempted, s_publication_queue_drops, s_serial_write_errors);
            if (s_status_label != nullptr && bsp_display_lock(0)) {
                lv_label_set_text(s_status_label, s_harness_failure.failed()
                                                      ? "Stopped - reset to rerun"
                                                      : "Complete - reset to rerun");
                bsp_display_unlock();
            }
            // All three encoding variants completed. Checkpoints are explicitly
            // unsupported at every tested position, so the checkpoint bit is 0.
            if (!s_harness_failure.failed())
                std::puts("LAB04_HARNESS_DONE status=complete variants=3 checkpoints=0");
            else
                std::puts("LAB04_HARNESS_DONE status=failed variants_incomplete=1 reason=trace_or_terminal_mismatch");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
