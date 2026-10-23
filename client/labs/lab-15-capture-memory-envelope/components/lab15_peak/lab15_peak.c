#include "lab15_peak.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint32_t s_trace_seq;
static int64_t s_fake_clock_ms;
static lv_obj_t *s_montage_root;
static uint32_t s_montage_nodes;

static const char *ui_slug(void)
{
#if CONFIG_LAB15_UI_CATALOG
    return "catalog";
#else
    return "none";
#endif
}

static const char *audio_slug(void)
{
#if CONFIG_LAB15_AUDIO_PCM_CHUNK
    return "chunk";
#else
    return "pcm";
#endif
}

static const char *opus_slug(void)
{
#if CONFIG_LAB15_OPUS_LINK
    return "opus";
#else
    return "none";
#endif
}

static const char *sdmmc_slug(void)
{
#if CONFIG_LAB15_SDMMC_SPILL
    return "sdmmc";
#else
    return "none";
#endif
}

const char *lab15_arm_slug(void)
{
    static char arm[48];
    snprintf(arm, sizeof(arm), "%s_%s_%s_%s", ui_slug(), audio_slug(), opus_slug(),
             sdmmc_slug());
    return arm;
}

int64_t lab15_clock_ms(void)
{
#if CONFIG_LAB15_SCRIPTED
    return s_fake_clock_ms;
#else
    return (int64_t)(esp_timer_get_time() / 1000);
#endif
}

void lab15_clock_advance_ms(int64_t delta_ms)
{
#if CONFIG_LAB15_SCRIPTED
    s_fake_clock_ms += delta_ms;
#endif
}

void lab15_sample_heap(lab15_heap_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->live_nodes = s_montage_nodes;
    out->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->internal_largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    out->internal_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    out->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out->psram_largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    out->dma_largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    out->lvgl_used = mon.total_size - mon.free_size;
    out->lvgl_largest_free = mon.free_biggest_size;
}

static void emit_trace(const char *event, const char *extra)
{
    ++s_trace_seq;
    int64_t t_us = lab15_clock_ms() * 1000;
    printf("LAB15_TRACE schema=lab15-envelope-trace/v1 event=%s seq=%" PRIu32 " t_us=%" PRId64
           " arm=%s %s\n",
           event, s_trace_seq, t_us, lab15_arm_slug(), extra);
}

void lab15_trace_resource_sample(const char *phase, const lab15_heap_snapshot_t *snap)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
             "phase=%s live_nodes=%" PRIu32 " internal_free=%zu internal_largest_free=%zu "
             "internal_min_free=%zu psram_free=%zu psram_largest_free=%zu lvgl_used=%zu "
             "lvgl_largest_free=%zu dma_largest_free=%zu",
             phase, snap->live_nodes, snap->internal_free, snap->internal_largest_free,
             snap->internal_min_free, snap->psram_free, snap->psram_largest_free,
             snap->lvgl_used, snap->lvgl_largest_free, snap->dma_largest_free);
    emit_trace("resource_sample", buf);
}

void lab15_trace_allocation_attempt(const char *kind, size_t bytes, bool ok,
                                    const char *region)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "kind=%s bytes=%zu result=%s region=%s", kind, bytes,
             ok ? "ok" : "fail", region);
    emit_trace("allocation_attempt", buf);
}

void lab15_trace_terminal(uint64_t operation_id, const char *outcome, const char *reason)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "operation_id=%" PRIu64 " outcome=%s reason=%s", operation_id,
             outcome, reason);
    emit_trace("terminal", buf);
}

static uint32_t count_tree_nodes(const lv_obj_t *root)
{
    if (root == NULL) {
        return 0;
    }
    uint32_t count = 1;
    uint32_t child_count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < child_count; ++i) {
        count += count_tree_nodes(lv_obj_get_child(root, i));
    }
    return count;
}

bool lab15_montage_mount(lv_obj_t *screen, uint32_t *live_nodes_out)
{
    if (screen == NULL) {
        return false;
    }
#if !CONFIG_LAB15_UI_CATALOG
    s_montage_nodes = 0;
    if (live_nodes_out != NULL) {
        *live_nodes_out = 0;
    }
    return true;
#else
    if (s_montage_root != NULL) {
        return false;
    }
    s_montage_root = lv_obj_create(screen);
    lv_obj_set_size(s_montage_root, 320, 240);
    lv_obj_set_style_pad_all(s_montage_root, 0, 0);
    lv_obj_set_scrollbar_mode(s_montage_root, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *header = lv_obj_create(s_montage_root);
    lv_obj_set_size(header, 320, 48);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Family Link Inbox Hea…");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, -8);
    lv_obj_t *subtitle = lv_label_create(header);
    lv_label_set_text(subtitle, "Signed in as Lynn Frank…");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_align(subtitle, LV_ALIGN_LEFT_MID, 8, 14);

    lv_obj_t *rail = lv_obj_create(s_montage_root);
    lv_obj_set_size(rail, 80, 240);
    lv_obj_align(rail, LV_ALIGN_TOP_RIGHT, 0, 48);
    static const uint32_t swatch_colors[] = {0x000000, 0xF8F8F8, 0xE8C040, 0x4CAF50,
                                             0xE53935, 0x5AA0E8};
    for (int i = 0; i < 6; ++i) {
        lv_obj_t *swatch = lv_obj_create(rail);
        lv_obj_set_size(swatch, 44, 44);
        lv_obj_align(swatch, LV_ALIGN_TOP_MID, (i % 2) ? 11 : -11, 8 + i * 36);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(swatch_colors[i]), 0);
        lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
    }

    lv_obj_t *canvas = lv_obj_create(s_montage_root);
    lv_obj_set_size(canvas, 240, 240);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 48);
    lv_obj_set_style_bg_color(canvas, lv_color_hex(0x000000), 0);
    lv_obj_t *canvas_label = lv_label_create(canvas);
    lv_label_set_text(canvas_label, "240x240 canvas");
    lv_obj_center(canvas_label);

    s_montage_nodes = count_tree_nodes(s_montage_root);
    if (live_nodes_out != NULL) {
        *live_nodes_out = s_montage_nodes;
    }
    return true;
#endif
}

void lab15_montage_unmount(void)
{
    if (s_montage_root != NULL) {
        lv_obj_delete(s_montage_root);
        s_montage_root = NULL;
    }
    s_montage_nodes = 0;
}

static void *alloc_capture_block(size_t bytes, const char *kind, const char *region)
{
    void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        region = "internal";
    } else {
        region = "psram";
    }
    lab15_trace_allocation_attempt(kind, bytes, ptr != NULL, region);
    return ptr;
}

bool lab15_reserve_sketch(void **sketch_out, size_t *bytes_out)
{
    const size_t bytes = LAB15_SKETCH_BYTES_MAX;
    void *ptr = alloc_capture_block(bytes, "sketch", "psram");
    if (sketch_out != NULL) {
        *sketch_out = ptr;
    }
    if (bytes_out != NULL) {
        *bytes_out = ptr != NULL ? bytes : 0;
    }
    return ptr != NULL;
}

bool lab15_reserve_canvas(void **canvas_out, size_t *bytes_out)
{
    const size_t bytes = LAB15_CANVAS_BYTES_PER_BUFFER * LAB15_CANVAS_BUFFER_COUNT;
    void *ptr = alloc_capture_block(bytes, "canvas", "psram");
    if (canvas_out != NULL) {
        *canvas_out = ptr;
    }
    if (bytes_out != NULL) {
        *bytes_out = ptr != NULL ? bytes : 0;
    }
    return ptr != NULL;
}

static void *s_pcm_chunks[16];
static int s_pcm_chunk_count;

bool lab15_reserve_pcm(void **pcm_out, size_t *bytes_out)
{
#if CONFIG_LAB15_AUDIO_PCM_CHUNK
    const int chunks = CONFIG_LAB15_PCM_CHUNK_COUNT;
    const size_t chunk_bytes = CONFIG_LAB15_PCM_CHUNK_BYTES;
    size_t total = 0;
    s_pcm_chunk_count = 0;
    for (int i = 0; i < chunks; ++i) {
        void *chunk = alloc_capture_block(chunk_bytes, "pcm_chunk", "psram");
        if (chunk == NULL) {
            for (int j = 0; j < s_pcm_chunk_count; ++j) {
                heap_caps_free(s_pcm_chunks[j]);
                s_pcm_chunks[j] = NULL;
            }
            s_pcm_chunk_count = 0;
            if (pcm_out != NULL) {
                *pcm_out = NULL;
            }
            if (bytes_out != NULL) {
                *bytes_out = 0;
            }
            return false;
        }
        s_pcm_chunks[s_pcm_chunk_count++] = chunk;
        total += chunk_bytes;
    }
    if (pcm_out != NULL) {
        *pcm_out = s_pcm_chunks[0];
    }
    if (bytes_out != NULL) {
        *bytes_out = total;
    }
    return true;
#else
    const size_t bytes = CONFIG_LAB15_PCM_RING_BYTES;
    void *ptr = alloc_capture_block(bytes, "pcm_ring", "psram");
    if (pcm_out != NULL) {
        *pcm_out = ptr;
    }
    if (bytes_out != NULL) {
        *bytes_out = ptr != NULL ? bytes : 0;
    }
    return ptr != NULL;
#endif
}

void lab15_release_capture_buffers(void **sketch, void **canvas, void **pcm)
{
    if (sketch != NULL && *sketch != NULL) {
        heap_caps_free(*sketch);
        *sketch = NULL;
    }
    if (canvas != NULL && *canvas != NULL) {
        heap_caps_free(*canvas);
        *canvas = NULL;
    }
#if CONFIG_LAB15_AUDIO_PCM_CHUNK
    for (int i = 0; i < s_pcm_chunk_count; ++i) {
        if (s_pcm_chunks[i] != NULL) {
            heap_caps_free(s_pcm_chunks[i]);
            s_pcm_chunks[i] = NULL;
        }
    }
    s_pcm_chunk_count = 0;
    if (pcm != NULL) {
        *pcm = NULL;
    }
#else
    if (pcm != NULL && *pcm != NULL) {
        heap_caps_free(*pcm);
        *pcm = NULL;
    }
#endif
}

bool lab15_pcm_fill_deterministic(void *pcm, size_t bytes, int64_t end_ms)
{
    if (bytes == 0) {
        return false;
    }
#if CONFIG_LAB15_AUDIO_PCM_CHUNK
    if (s_pcm_chunk_count == 0) {
        return false;
    }
    size_t offset = 0;
    for (int i = 0; i < s_pcm_chunk_count; ++i) {
        const size_t chunk_bytes = CONFIG_LAB15_PCM_CHUNK_BYTES;
        uint8_t *samples = (uint8_t *)s_pcm_chunks[i];
        if (samples == NULL) {
            return false;
        }
        for (size_t j = 0; j < chunk_bytes; ++j) {
            samples[j] = (uint8_t)(((offset + j) * 131U) & 0xFFU);
        }
        offset += chunk_bytes;
    }
#else
    if (pcm == NULL) {
        return false;
    }
    uint8_t *samples = (uint8_t *)pcm;
    for (size_t i = 0; i < bytes; ++i) {
        samples[i] = (uint8_t)((i * 131U) & 0xFFU);
    }
#endif
    lab15_clock_advance_ms(end_ms - lab15_clock_ms());
    return true;
}

static void sample_phase(const char *phase)
{
    lab15_heap_snapshot_t snap;
    lab15_sample_heap(&snap);
    lab15_trace_resource_sample(phase, &snap);
}

static void run_capture_envelope(void)
{
    void *sketch = NULL;
    void *canvas = NULL;
    void *pcm = NULL;
    size_t pcm_bytes = 0;

    sample_phase("boot");

    uint32_t nodes = 0;
    lv_obj_t *screen = lv_screen_active();
    if (!lab15_montage_mount(screen, &nodes)) {
        lab15_trace_terminal(LAB15_CAPTURE_OPERATION_ID, "allocation_failure",
                             "montage_mount_failed");
        return;
    }
    sample_phase("ui_mounted");

    if (!lab15_reserve_canvas(&canvas, NULL) || !lab15_reserve_sketch(&sketch, NULL) ||
        !lab15_reserve_pcm(&pcm, &pcm_bytes)) {
        lab15_trace_terminal(LAB15_CAPTURE_OPERATION_ID, "allocation_failure",
                             "capture_reserve_failed");
        lab15_release_capture_buffers(&sketch, &canvas, &pcm);
        return;
    }
    sample_phase("audio_reserved");

#if CONFIG_LAB15_PEAK_ALLOC_ONLY
    lab15_trace_terminal(LAB15_CAPTURE_OPERATION_ID, "peak_alloc_complete",
                         "peak_alloc_only");
    lab15_release_capture_buffers(&sketch, &canvas, &pcm);
    return;
#endif

    if (!lab15_pcm_fill_deterministic(pcm, pcm_bytes, LAB15_DURATION_CAP_MS)) {
        lab15_trace_terminal(LAB15_CAPTURE_OPERATION_ID, "allocation_failure", "pcm_fill_failed");
        lab15_release_capture_buffers(&sketch, &canvas, &pcm);
        return;
    }
    sample_phase("duration_cap");
    lab15_trace_terminal(LAB15_CAPTURE_OPERATION_ID, "duration_cap", "duration_cap_ms_180000");
    lab15_release_capture_buffers(&sketch, &canvas, &pcm);
}

void lab15_scripted_harness_run(void)
{
    run_capture_envelope();
    printf("LAB15_HARNESS_DONE status=complete\n");
}
