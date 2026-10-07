/* H38 bounded FAT32 setup and filesystem I/O qualification. */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bsp/esp-box-3.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_dma_utils.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "diskio.h"
#include "ff.h"
#include "h38_disk_guard.h"
#include "h38_fatfs_adapter.h"
#include "h38_filesystem_io.h"
#include "sdmmc_cmd.h"

#ifndef H38_INTENT_HEADER
#error H38_INTENT_HEADER must name private generated manifest constants
#endif
#include H38_INTENT_HEADER

#ifndef H38_RUN_EPOCH
#error H38_RUN_EPOCH is required
#endif
#ifndef H35_REFERENCE_EPOCH
#error H35_REFERENCE_EPOCH is required
#endif
#ifndef H38_PRIVATE_CID_SHA256
#error H38_PRIVATE_CID_SHA256 is required in the private intent header
#endif
#ifndef H38_OLD_MBR_SHA256
#error H38_OLD_MBR_SHA256 is required in the private intent header
#endif
#ifndef H38_WRITE_MANIFEST_SHA256
#error H38_WRITE_MANIFEST_SHA256 is required in the private intent header
#endif
#ifndef H38_SOURCE_REVISION
#error H38_SOURCE_REVISION is required in the private intent header
#endif
#ifndef H38_INTENT_SHA256
#error H38_INTENT_SHA256 is required in the private intent header
#endif

_Static_assert(BSP_SD_D0 == 9 && BSP_SD_D1 == 13 && BSP_SD_D2 == 42 &&
               BSP_SD_D3 == 12 && BSP_SD_CMD == 14 && BSP_SD_CLK == 11 &&
               BSP_SD_POWER == 43, "Pinned BOX-3 SDMMC pins changed");
_Static_assert(sizeof(H38_PRIVATE_CID_SHA256) == 65 &&
               sizeof(H38_OLD_MBR_SHA256) == 65 &&
               sizeof(H38_WRITE_MANIFEST_SHA256) == 65 &&
               sizeof(H38_SOURCE_REVISION) == 41 &&
               sizeof(H38_INTENT_SHA256) == 65,
               "Private H38 manifest hashes must be 64 lowercase hex characters");
_Static_assert(FM_FAT32 == 2 && FM_SFD == 8 && FS_FAT32 == 3,
               "Pinned FatFs ABI changed");

enum {
    CMD_MAX = 512, RX_BYTES = 512, TX_BYTES = 8192, DMA_BYTES = 4096,
    SESSION_MS = 1200000, IDLE_MS = 15000, COMMAND_MS = 5000,
    DISCOVERY_MS = 45000, COMMAND_TIMEOUT_MS = 1000, POWER_GPIO = 43,
    MBR_BYTES = 512
};
static const uint32_t EXPECTED_SECTORS = 121503744u;
static const uint32_t EXPECTED_FREQ_KHZ = 20000u;
static char elf_hex[65];
static bool usb_installed, host_initialized, power_configured, bound;
static bool host_deinit_attempted, power_off_attempted, sd_unmount_attempted;
static int32_t host_deinit_error, power_off_error, sd_unmount_error;
static uint32_t command_count, layout_write_count;
static int64_t start_us, last_activity_us, command_start_us;
static char line[CMD_MAX + 1];
static uint8_t rx[RX_BYTES];
static uint8_t dma_buffer[DMA_BYTES] DMA_ATTR __attribute__((aligned(4)));
static uint8_t mbr_readback[MBR_BYTES] DMA_ATTR __attribute__((aligned(4)));
static uint8_t bpb_sector[MBR_BYTES] DMA_ATTR __attribute__((aligned(4)));
static char tx_record[TX_BYTES];
/* Only the synchronous application task calls EMIT; no ISR/task shares it. */
static char record_fields[4096];
static sdmmc_card_t card;
static h38_disk_guard_t guard;
static h38_fatfs_adapter_t fatfs;
static bool guard_ready, fatfs_ready, layout_written;
static uint32_t mount_count;
static h38_filesystem_io_result_t io_result;
static uint64_t raw_read_bytes, raw_read_attempts;
static int32_t last_backend_error;
static char cid_digest_hex[65];

typedef enum {
    ST_NONE, ST_RESOURCES, ST_RESET, ST_POWER, ST_HOST, ST_SLOT, ST_CARD,
    ST_GEOMETRY, ST_BIND, ST_LAYOUT, ST_FORMAT, ST_MOUNT, ST_IO_CREATE,
    ST_IO_WRITE, ST_IO_FFLUSH, ST_IO_FSYNC, ST_IO_FCLOSE, ST_IO_RENAME,
    ST_IO_READBACK, ST_IO_CHECKSUM, ST_IO_DELETE, ST_PROBE, ST_SEMANTICS,
    ST_REMOUNT, ST_RECLAIM, ST_BUDGET, ST_TIMEOUT, ST_CLEANUP
} stage_t;
static stage_t init_stage = ST_POWER;
static const char *stage_name(stage_t stage)
{
    static const char *const names[] = {
        "none","resources","reset","power","host","slot","card",
        "geometry","bind","layout","format","mount","io_create",
        "io_write","io_fflush","io_fsync","io_fclose","io_rename",
        "io_readback","io_checksum","io_delete","probe","semantics",
        "remount","reclaim","budget","timeout","cleanup"
    };
    return (unsigned)stage < sizeof(names)/sizeof(names[0]) ? names[stage] : "resources";
}
static void fail(stage_t stage, int32_t error);

static void make_elf_hex(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    for (unsigned i = 0; i < 32; i++) snprintf(elf_hex + 2*i, 3, "%02x", app->app_elf_sha256[i]);
}

static esp_err_t emit_record(const char *event, const char *fields)
{
    int n = snprintf(tx_record, sizeof(tx_record),
                     "H38,1,%s,epoch=%s,elf_sha256=%s%s%s\n", event,
                     H38_RUN_EPOCH, elf_hex,
                     fields && *fields ? "," : "", fields ? fields : "");
    if (n < 0 || (size_t)n >= sizeof(tx_record)) return ESP_ERR_INVALID_SIZE;
    if (printf("%s", tx_record) != n || fflush(stdout) != 0) return ESP_FAIL;
    if (usb_installed && usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(15000)) != ESP_OK)
        return ESP_ERR_TIMEOUT;
    return ESP_OK;
}
#define EMIT(event, ...) do { \
    int _n = snprintf(record_fields, sizeof(record_fields), __VA_ARGS__); \
    if (_n < 0 || (size_t)_n >= sizeof(record_fields)) fail(ST_RESOURCES, ESP_ERR_INVALID_SIZE); \
    if (emit_record((event), record_fields) != ESP_OK) fail(ST_TIMEOUT, ESP_ERR_TIMEOUT); \
} while (0)

static bool hex_lower(const char *s, size_t n)
{
    if (!s || strlen(s) != n) return false;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}
static bool split_fields(char *s, char **out, size_t cap, size_t *count)
{
    size_t n = 0; char *start = s;
    for (char *p = s;; p++) {
        if (*p == ',' || *p == '\0') {
            if (p == start || n == cap) return false;
            out[n++] = start;
            if (*p == '\0') break;
            *p = '\0'; start = p + 1;
        }
    }
    *count = n;
    return true;
}
static esp_err_t read_command(char *dst, size_t cap, int64_t *last_rx)
{
    size_t used = 0;
    command_start_us = 0;
    while (true) {
        int64_t now = esp_timer_get_time();
        if ((uint64_t)(now - (int64_t)start_us) / 1000u >= SESSION_MS ||
            (now - *last_rx) / 1000 >= IDLE_MS ||
            (used && (now - command_start_us) / 1000 >= COMMAND_MS)) return ESP_ERR_TIMEOUT;
        int n = usb_serial_jtag_read_bytes(rx, sizeof(rx), 0);
        if (n < 0) return ESP_FAIL;
        if (n == 0) { vTaskDelay(1); continue; }
        if (!used) command_start_us = now;
        *last_rx = now;
        for (int i = 0; i < n; i++) {
            unsigned char c = rx[i];
            if (c == '\n') {
                if (i + 1 != n || used + 1 > CMD_MAX || used + 1 >= cap) return ESP_ERR_INVALID_SIZE;
                dst[used++] = '\n'; dst[used] = '\0'; return ESP_OK;
            }
            if (c < 0x20 || c > 0x7e || used + 2 > CMD_MAX || used + 1 >= cap)
                return ESP_ERR_INVALID_SIZE;
            dst[used++] = (char)c;
        }
    }
}
static bool read_fields(char **fields, size_t cap, size_t *count)
{
    esp_err_t err = read_command(line, sizeof(line), &last_activity_us);
    size_t len;
    if (err != ESP_OK) { fail(err == ESP_ERR_TIMEOUT ? ST_TIMEOUT : ST_RESOURCES, err); }
    len = strlen(line);
    if (!len || line[len - 1] != '\n') return false;
    line[len - 1] = '\0';
    return split_fields(line, fields, cap, count);
}
static void sha256_hex(const uint8_t *data, size_t len, char out[65])
{
    uint8_t digest[32];
    if (mbedtls_sha256(data, len, digest, 0) != 0) fail(ST_RESOURCES, ESP_FAIL);
    for (unsigned i = 0; i < 32; i++) snprintf(out + 2*i, 3, "%02x", digest[i]);
    out[64] = '\0';
    memset(digest, 0, sizeof(digest));
}
static int32_t compute_cid_digest(const sdmmc_card_t *c, char out[65])
{
    char canonical[160]; uint8_t digest[32];
    int n = snprintf(canonical, sizeof(canonical), "%s:%08x:%08x:%.*s:%08x:%08x:%08x",
                     H35_REFERENCE_EPOCH, c->cid.mfg_id, c->cid.oem_id,
                     (int)sizeof(c->cid.name), c->cid.name,
                     c->cid.revision, c->cid.serial, c->cid.date);
    if (n <= 0 || n >= (int)sizeof(canonical) ||
        mbedtls_sha256((const unsigned char *)canonical, (size_t)n, digest, 0) != 0) {
        memset(canonical, 0, sizeof(canonical)); return -1;
    }
    for (unsigned i = 0; i < 32; i++) snprintf(out + 2*i, 3, "%02x", digest[i]);
    out[64] = '\0';
    memset(canonical, 0, sizeof(canonical)); memset(digest, 0, sizeof(digest));
    return 0;
}
static void emit_cid(void)
{
    char name_hex[33];
    for (size_t i = 0; i < sizeof(card.cid.name); i++)
        snprintf(name_hex + 2*i, 3, "%02x", (unsigned char)card.cid.name[i]);
    name_hex[2*sizeof(card.cid.name)] = '\0';
    EMIT("CID_PRIVATE", "mfg_id=%" PRIu32 ",oem_id=%" PRIu32 ",revision=%" PRIu32
         ",serial=%" PRIu32 ",date=%" PRIu32 ",name_size=%u,name_hex=%s",
         (uint32_t)card.cid.mfg_id, (uint32_t)card.cid.oem_id,
         (uint32_t)card.cid.revision, (uint32_t)card.cid.serial,
         (uint32_t)card.cid.date, (unsigned)sizeof(card.cid.name), name_hex);
    memset(name_hex, 0, sizeof(name_hex));
}
static int32_t raw_read_lba0(uint8_t *buffer)
{
    if (!buffer || !esp_ptr_dma_capable(buffer) || ((uintptr_t)buffer & 3u) != 0u) return -1;
    raw_read_attempts++;
    if (raw_read_attempts > 3u) return ESP_ERR_INVALID_STATE;
    raw_read_bytes += H38_GUARD_SECTOR_BYTES;
    esp_err_t err = sdmmc_read_sectors(&card, buffer, 0, 1);
    return err == ESP_OK ? 0 : (int32_t)err;
}
static void hex_epoch_disk_id(uint8_t mbr[512])
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 8; i++) {
        char c = H38_RUN_EPOCH[i];
        value = (value << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'a' + 10);
    }
    mbr[440] = (uint8_t)value; mbr[441] = (uint8_t)(value >> 8);
    mbr[442] = (uint8_t)(value >> 16); mbr[443] = (uint8_t)(value >> 24);
}
static void make_expected_mbr(uint8_t mbr[512])
{
    memset(mbr, 0, 512);
    hex_epoch_disk_id(mbr);
    mbr[446] = 0x00; mbr[447] = 0xfe; mbr[448] = 0xff; mbr[449] = 0xff;
    mbr[450] = 0x0c; mbr[451] = 0xfe; mbr[452] = 0xff; mbr[453] = 0xff;
    mbr[454] = 0x00; mbr[455] = 0x80; mbr[456] = 0x00; mbr[457] = 0x00;
    mbr[458] = 0x00; mbr[459] = 0x00; mbr[460] = 0x10; mbr[461] = 0x00;
    mbr[510] = 0x55; mbr[511] = 0xaa;
}
static void emit_transport(void)
{
    EMIT("TRANSPORT", "profile=sdmmc_bounded_fat32_v1,backend=sdmmc,mode=bounded_fat32,console=usb_serial_jtag,usb_host=disabled,slot=0,width_requested=4,max_freq_khz=20000,command_timeout_ms=1000,command_line_max=512,record_max=8192,session_ms=1200000,format_read_limit_bytes=16777216,format_write_limit_bytes=4194304,format_ms=120000,io_read_limit_bytes=67108864,io_write_limit_bytes=33554432,io_ms=900000,file_transaction_ms=30000,capture_wall_ms=1200000,flash_restore_ms=5400000,max_driver_call_sectors=8,dma_buffer_bytes=4096,max_file_bytes=196608,max_generated_logical_bytes=33554432,max_retained_file_bytes=2097152,usb_rx_bytes=512,usb_tx_bytes=8192");
}
static void cleanup(stage_t *stage, int32_t *error)
{
    bool adapter_deinit_failed = false;
    if (guard_ready && guard.phase_active) (void)h38_guard_end_phase(&guard);
    if (fatfs_ready && fatfs.mounted) {
        sd_unmount_attempted = true;
        sd_unmount_error = h38_fatfs_unmount_idle(&fatfs);
        if (sd_unmount_error != ESP_OK && *stage == ST_NONE) { *stage = ST_CLEANUP; *error = sd_unmount_error; }
    }
    if (fatfs_ready) {
        esp_err_t e = h38_fatfs_adapter_deinit(&fatfs);
        fatfs_ready = false;
        if (e != ESP_OK) adapter_deinit_failed = true;
    }
    if (host_initialized) {
        host_deinit_attempted = true;
        host_deinit_error = sdmmc_host_deinit(); host_initialized = false;
        if (host_deinit_error != ESP_OK && *stage == ST_NONE) { *stage = ST_CLEANUP; *error = host_deinit_error; }
    }
    if (power_configured) {
        power_off_attempted = true;
        power_off_error = gpio_set_level(POWER_GPIO, 1);
        if (power_off_error != ESP_OK && *stage == ST_NONE) { *stage = ST_CLEANUP; *error = power_off_error; }
    }
    char cleanup_fields[256];
    snprintf(cleanup_fields, sizeof(cleanup_fields), "sd_unmount_attempted=%u,sd_unmount_error=%" PRId32 ",host_deinit_attempted=%u,host_deinit_error=%" PRId32 ",power_off_attempted=%u,power_off_error=%" PRId32,
         (unsigned)sd_unmount_attempted, sd_unmount_error,
         (unsigned)host_deinit_attempted, host_deinit_error,
         (unsigned)power_off_attempted, power_off_error);
    esp_err_t record_error = emit_record("CLEANUP", cleanup_fields);
    if (record_error != ESP_OK && *stage == ST_NONE) {
        *stage = record_error == ESP_ERR_TIMEOUT ? ST_TIMEOUT : ST_RESOURCES;
        *error = record_error;
    }
    /* The frozen CLEANUP ABI has no adapter-deinit error field. Leave the
       capture incomplete so the host performs its mandatory timeout restore. */
    if (adapter_deinit_failed) for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
static void io_emit(void *context, const char *event, const char *fields)
{
    (void)context;
    /* An unrecorded operation is never allowed to continue mutating media. */
    esp_err_t err = emit_record(event, fields);
    if (err != ESP_OK) fail(err == ESP_ERR_TIMEOUT ? ST_TIMEOUT : ST_RESOURCES, err);
}
static stage_t io_stage(h38_filesystem_io_stage_t stage)
{
    switch (stage) {
    case H38_IO_STAGE_IO_CREATE: return ST_IO_CREATE;
    case H38_IO_STAGE_IO_WRITE: return ST_IO_WRITE;
    case H38_IO_STAGE_IO_FFLUSH: return ST_IO_FFLUSH;
    case H38_IO_STAGE_IO_FSYNC: return ST_IO_FSYNC;
    case H38_IO_STAGE_IO_FCLOSE: return ST_IO_FCLOSE;
    case H38_IO_STAGE_IO_RENAME: return ST_IO_RENAME;
    case H38_IO_STAGE_IO_READBACK: return ST_IO_READBACK;
    case H38_IO_STAGE_IO_CHECKSUM: return ST_IO_CHECKSUM;
    case H38_IO_STAGE_IO_DELETE: return ST_IO_DELETE;
    case H38_IO_STAGE_PROBE: return ST_PROBE;
    case H38_IO_STAGE_REMOUNT: return ST_REMOUNT;
    case H38_IO_STAGE_RECLAIM: return ST_RECLAIM;
    case H38_IO_STAGE_BUDGET: return ST_BUDGET;
    case H38_IO_STAGE_TIMEOUT: return ST_TIMEOUT;
    default: return ST_SEMANTICS;
    }
}
static void terminal(stage_t stage, int32_t error)
{
    h38_fatfs_counts_t counts = {0};
    uint64_t io_write_bytes = guard_ready && guard.io_started ?
        guard.phase_counts.write_bytes : 0u;
    if (guard_ready) {
        counts.read_bytes = guard.total.read_bytes; counts.write_bytes = guard.total.write_bytes;
        counts.trim_requests = guard.total.trim_requests;
        counts.out_of_bounds_attempts = guard.total.out_of_bounds_attempts;
        counts.write_driver_calls = guard.total.write_driver_calls;
    }
    if (stage == ST_NONE) { stage = ST_CLEANUP; if (error == 0) error = ESP_ERR_INVALID_STATE; }
    if (error == 0) error = ESP_FAIL;
    char terminal_fields[2048];
    int terminal_len = snprintf(terminal_fields, sizeof(terminal_fields), "result=failed,failure_stage=%s,error=%" PRId32
         ",command_count=%" PRIu32 ",read_bytes=%" PRIu64 ",write_bytes=%" PRIu64
         ",mbr_write_count=%" PRIu32 ",format_write_bytes=%" PRIu64 ",io_write_bytes=%" PRIu64
         ",trim_requests=%" PRIu64 ",erase_calls=0,out_of_bounds_attempts=%" PRIu64
         ",io_file_count=%" PRIu32 ",retained_file_bytes=%" PRIu64 ",probe_count=%" PRIu32
         ",mount_count=%" PRIu32 ",remount_count=%" PRIu32
         ",retained_check_count=%" PRIu32 ",reclaim_status=failed,sd_unmount_attempted=%u,sd_unmount_error=%" PRId32
         ",host_deinit_attempted=%u,host_deinit_error=%" PRId32 ",power_off_attempted=%u,power_off_error=%" PRId32
         ",bound=%u,scope=bounded_fat32_filesystem_io,media_writes=%" PRIu64,
         stage_name(stage), error, command_count, counts.read_bytes + raw_read_bytes,
         counts.write_bytes + (uint64_t)layout_write_count * 512u, layout_write_count,
         counts.write_bytes - io_write_bytes,
         io_write_bytes, counts.trim_requests,
         counts.out_of_bounds_attempts, io_result.io_file_count, io_result.retained_file_bytes,
         io_result.probe_count, mount_count + io_result.mount_count, io_result.remount_count,
         io_result.retained_check_count, (unsigned)sd_unmount_attempted, sd_unmount_error,
         (unsigned)host_deinit_attempted, host_deinit_error,
         (unsigned)power_off_attempted, power_off_error,
         (unsigned)bound, (uint64_t)layout_write_count + counts.write_driver_calls);
    if (terminal_len > 0 && (size_t)terminal_len < sizeof(terminal_fields))
        (void)emit_record("COMPLETE", terminal_fields);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
static void fail(stage_t stage, int32_t error)
{
    cleanup(&stage, &error);
    terminal(stage, error);
}
static void complete_success(void)
{
    stage_t stage = ST_NONE;
    int32_t error = 0;
    cleanup(&stage, &error);
    if (stage != ST_NONE || error != 0) terminal(stage, error);
    if (io_result.io_file_count != 40u || io_result.probe_count != 5u ||
        io_result.mount_count != 5u || io_result.remount_count != 5u ||
        io_result.retained_check_count != 30u || command_count != 4u ||
        layout_write_count != 1u || !bound || guard.failed ||
        guard.total.out_of_bounds_attempts != 0u ||
        !sd_unmount_attempted || !host_deinit_attempted || !power_off_attempted)
        terminal(ST_SEMANTICS, ESP_ERR_INVALID_STATE);
    char fields[2048];
    int n = snprintf(fields, sizeof(fields),
        "result=io_complete,failure_stage=none,error=0,command_count=%" PRIu32
        ",read_bytes=%" PRIu64 ",write_bytes=%" PRIu64
        ",mbr_write_count=1,format_write_bytes=%" PRIu64 ",io_write_bytes=%" PRIu64
        ",trim_requests=%" PRIu64 ",erase_calls=0,out_of_bounds_attempts=0"
        ",io_file_count=%" PRIu32 ",retained_file_bytes=%" PRIu64
        ",probe_count=%" PRIu32 ",mount_count=%" PRIu32
        ",remount_count=%" PRIu32 ",retained_check_count=%" PRIu32
        ",reclaim_status=ok,sd_unmount_attempted=1,sd_unmount_error=0"
        ",host_deinit_attempted=1,host_deinit_error=0,power_off_attempted=1,power_off_error=0"
        ",bound=1,scope=bounded_fat32_filesystem_io,media_writes=%" PRIu64,
        command_count, guard.total.read_bytes + raw_read_bytes,
        guard.total.write_bytes + 512u,
        guard.total.write_bytes - guard.phase_counts.write_bytes,
        guard.phase_counts.write_bytes, guard.total.trim_requests, io_result.io_file_count,
        io_result.retained_file_bytes, io_result.probe_count,
        mount_count + io_result.mount_count, io_result.remount_count,
        io_result.retained_check_count, 1u + guard.total.write_driver_calls);
    if (n < 0 || (size_t)n >= sizeof(fields) || emit_record("COMPLETE", fields) != ESP_OK)
        terminal(ST_RESOURCES, ESP_FAIL);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
static uint64_t guard_clock_ms(void *context)
{
    (void)context;
    return (uint64_t)esp_timer_get_time() / 1000u;
}
static int guard_backend(void *context, int write, uint64_t physical_lba,
                         uint32_t sectors, uint8_t *buffer)
{
    sdmmc_card_t *c = (sdmmc_card_t *)context;
    if (!c || sectors == 0u || sectors > H38_GUARD_MAX_DRIVER_SECTORS ||
        physical_lba < H38_GUARD_VOLUME_START_LBA ||
        physical_lba > H38_GUARD_VOLUME_START_LBA + H38_GUARD_VOLUME_SECTORS ||
        sectors > H38_GUARD_VOLUME_START_LBA + H38_GUARD_VOLUME_SECTORS - physical_lba ||
        !esp_ptr_dma_capable(buffer) || ((uintptr_t)buffer & 3u) != 0u) return -1;
    esp_err_t err = write ? sdmmc_write_sectors(c, buffer, (size_t)physical_lba, sectors)
                          : sdmmc_read_sectors(c, buffer, (size_t)physical_lba, sectors);
    if (err != ESP_OK) last_backend_error = err;
    return err == ESP_OK ? 0 : (int)err;
}
static esp_err_t verify_dma_capability(void)
{
    esp_dma_mem_info_t info = {0};
    if (!card.host.get_dma_info || card.host.get_dma_info(card.host.slot, &info) != ESP_OK ||
        info.dma_alignment_bytes != 4u || !esp_ptr_dma_capable(dma_buffer) ||
        ((uintptr_t)dma_buffer & 3u) != 0u || sizeof(dma_buffer) != H38_GUARD_DMA_BYTES)
        return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}
static esp_err_t init_usb(void)
{
    usb_serial_jtag_driver_config_t cfg = {.rx_buffer_size = RX_BYTES, .tx_buffer_size = TX_BYTES};
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err == ESP_OK) { usb_installed = true; usb_serial_jtag_vfs_use_driver(); }
    return err;
}
static int32_t verify_old_mbr(void)
{
    char digest[65];
    if (((uintptr_t)mbr_readback & 3u) != 0u) return ESP_ERR_INVALID_ARG;
    int32_t read_error = raw_read_lba0(mbr_readback);
    if (read_error != 0) return read_error;
    sha256_hex(mbr_readback, sizeof(mbr_readback), digest);
    int ok = strcmp(digest, H38_OLD_MBR_SHA256) == 0;
    memset(digest, 0, sizeof(digest));
    return ok ? 0 : ESP_ERR_INVALID_CRC;
}
static bool expect_phase_command(const char *phase, const char *seq)
{
    char *f[8]; size_t n = 0;
    if (!read_fields(f, 8, &n) || n != 6 || strcmp(f[0], "H38C") ||
        strcmp(f[1], "1") || strcmp(f[2], phase) ||
        strcmp(f[3], H38_RUN_EPOCH) || strcmp(f[4], elf_hex) || strcmp(f[5], seq))
        return false;
    return true;
}
static int32_t bind_command(char **f, size_t n)
{
    if (n != 9 || strcmp(f[0], "H38C") || strcmp(f[1], "1") || strcmp(f[2], "BIND") ||
        strcmp(f[3], H38_RUN_EPOCH) || strcmp(f[4], elf_hex) ||
        strcmp(f[5], H35_REFERENCE_EPOCH) || !hex_lower(f[3], 32) ||
        !hex_lower(f[4], 64) || !hex_lower(f[6], 64) || !hex_lower(f[7], 64) ||
        !hex_lower(f[8], 64)) return ESP_ERR_INVALID_ARG;
    int32_t mbr_error = verify_old_mbr();
    if (strcmp(f[6], H38_PRIVATE_CID_SHA256) || strcmp(f[7], H38_OLD_MBR_SHA256) ||
        strcmp(f[8], H38_WRITE_MANIFEST_SHA256) || strcmp(cid_digest_hex, f[6]) || mbr_error != 0) {
        int32_t err = mbr_error != 0 ? mbr_error : ESP_ERR_INVALID_CRC;
        EMIT("IDENTITY_MATCH", "reference_epoch=%s,match=0,error=%" PRId32, H35_REFERENCE_EPOCH, err);
        return err;
    }
    bound = true;
    EMIT("IDENTITY_MATCH", "reference_epoch=%s,match=1,error=0", H35_REFERENCE_EPOCH);
    return ESP_OK;
}
static int32_t do_layout(void)
{
    uint8_t expected[512]; char digest[65], b64[700]; size_t encoded = 0;
    if (!bound || layout_written || strcmp(cid_digest_hex, H38_PRIVATE_CID_SHA256))
        return ESP_ERR_INVALID_STATE;
    int32_t old_mbr_error = verify_old_mbr();
    if (old_mbr_error != 0) return old_mbr_error;
    make_expected_mbr(expected);
    memcpy(dma_buffer, expected, sizeof(expected));
    if (!esp_ptr_dma_capable(dma_buffer) || ((uintptr_t)dma_buffer & 3u) != 0u) return ESP_ERR_INVALID_ARG;
    layout_write_count = 1;
    esp_err_t write_error = sdmmc_write_sectors(&card, dma_buffer, 0, 1);
    if (write_error != ESP_OK) return write_error;
    layout_written = true;
    int32_t readback_error = raw_read_lba0(mbr_readback);
    if (readback_error != 0) return readback_error;
    if (memcmp(expected, mbr_readback, 512) != 0) return ESP_ERR_INVALID_CRC;
    if (raw_read_attempts != 3u) return ESP_ERR_INVALID_STATE;
    sha256_hex(mbr_readback, 512, digest);
    if (mbedtls_base64_encode((unsigned char *)b64, sizeof(b64), &encoded, mbr_readback, 512) != 0 || encoded != 684)
        return ESP_FAIL;
    b64[encoded] = '\0';
    EMIT("LAYOUT_RESULT", "physical_lba=0,write_sectors=1,write_bytes=512,write_count=1,readback_match=1,readback_sha256=%s,readback_base64_private=%s,trim_requests=0,erase_calls=0,status=ok,error=0", digest, b64);
    memset(expected, 0, sizeof(expected)); memset(digest, 0, sizeof(digest)); memset(b64, 0, sizeof(b64));
    return ESP_OK;
}
static void emit_layout_failure(int32_t error)
{
    static const char zero_sha[] =
        "0000000000000000000000000000000000000000000000000000000000000000";
    EMIT("LAYOUT_RESULT", "physical_lba=0,write_sectors=%u,write_bytes=%u,write_count=%u,readback_match=0,readback_sha256=%s,readback_base64_private=unavailable,trim_requests=0,erase_calls=0,status=failed,error=%" PRId32,
         (unsigned)layout_write_count, (unsigned)layout_write_count * 512u,
         (unsigned)layout_write_count, zero_sha, error);
}
static stage_t guard_failure_stage(stage_t fallback)
{
    if (!guard.failed) return fallback;
    if (guard.failure == H38_GUARD_E_TIMEOUT) return ST_TIMEOUT;
    if (guard.failure == H38_GUARD_E_BUDGET) return ST_BUDGET;
    return fallback;
}
static int32_t guard_failure_error(int32_t fallback)
{
    if (!guard.failed) return fallback;
    if (guard.failure == H38_GUARD_E_TIMEOUT) return ESP_ERR_TIMEOUT;
    if (guard.failure == H38_GUARD_E_BUDGET) return ESP_ERR_INVALID_SIZE;
    if (guard.failure == H38_GUARD_E_BACKEND && last_backend_error != 0) return last_backend_error;
    if (guard.failure == H38_GUARD_E_RANGE || guard.failure == H38_GUARD_E_OVERFLOW)
        return ESP_ERR_INVALID_SIZE;
    return fallback != 0 ? fallback : ESP_FAIL;
}
static void emit_mount_result(const char *kind, uint32_t cycle,
                              const h38_fatfs_mount_info_t *info, int64_t elapsed_us,
                              uint64_t read_delta, uint64_t write_delta)
{
    EMIT("MOUNT_RESULT", "kind=%s,cycle=%" PRIu32 ",mounted=1,fs_type=%" PRIu32
         ",sector_bytes=%" PRIu32 ",volume_sectors=%" PRIu32 ",allocation_unit_bytes=%" PRIu32
         ",cluster_count=%" PRIu32 ",total_bytes=%" PRIu64 ",free_bytes=%" PRIu64
         ",read_bytes=%" PRIu64 ",write_bytes=%" PRIu64 ",elapsed_us=%" PRId64 ",status=ok,error=0",
         kind, cycle, info->fs_type, info->sector_bytes, info->volume_sectors,
         info->allocation_unit_bytes, info->cluster_count, info->total_bytes,
         info->free_bytes, read_delta, write_delta, elapsed_us);
}
static void emit_mount_failure(const char *kind, uint32_t cycle,
                               int64_t elapsed_us, uint64_t read_bytes,
                               uint64_t write_bytes, int32_t error)
{
    EMIT("MOUNT_RESULT", "kind=%s,cycle=%" PRIu32
         ",mounted=0,fs_type=0,sector_bytes=512,volume_sectors=1048576"
         ",allocation_unit_bytes=0,cluster_count=0,total_bytes=0,free_bytes=0"
         ",read_bytes=%" PRIu64 ",write_bytes=%" PRIu64 ",elapsed_us=%" PRId64
         ",status=failed,error=%" PRId32,
         kind, cycle, read_bytes, write_bytes, elapsed_us, error);
}
static void emit_format_failure(FRESULT fr, uint64_t elapsed_us,
                                bool formatter_completed, int32_t error)
{
    static const char zero_sha[] =
        "0000000000000000000000000000000000000000000000000000000000000000";
    EMIT("FORMAT_RESULT", "f_result=%u,volume_sectors=1048576,sector_bytes=512"
         ",fat_type=FAT32,fat_count=2,allocation_unit_bytes=4096,cluster_count=0"
         ",fat_sectors=0,root_cluster_sectors=0,read_bytes=%" PRIu64
         ",write_bytes=%" PRIu64 ",write_calls=%" PRIu64 ",max_call_sectors=%" PRIu32
         ",trim_requests=%" PRIu64 ",erase_calls=0,bpb_valid=0,bpb_sha256=%s"
         ",bpb_base64_private=unavailable,elapsed_us=%" PRIu64
         ",status=%s,error=%" PRId32,
         (unsigned)fr, guard.phase_counts.read_bytes, guard.phase_counts.write_bytes,
         guard.phase_counts.write_driver_calls, guard.phase_counts.max_call_sectors,
         guard.phase_counts.trim_requests, zero_sha, elapsed_us,
         formatter_completed ? "ok" : "failed", formatter_completed ? 0 : error);
}
static bool discovery_elapsed(void)
{
    return (esp_timer_get_time() - start_us) >= (int64_t)DISCOVERY_MS * 1000;
}
static esp_err_t init_card(void)
{
    gpio_config_t power = {.pin_bit_mask = 1ULL << POWER_GPIO, .mode = GPIO_MODE_OUTPUT};
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_err_t err;
    init_stage = ST_POWER;
    err = gpio_config(&power);
    if (err == ESP_OK) { power_configured = true; err = gpio_set_level(POWER_GPIO, 0); }
    if (err != ESP_OK) { EMIT("POWER", "gpio=43,active_level=0,error=%d", err); return err; }
    vTaskDelay(pdMS_TO_TICKS(100));
    if (discovery_elapsed()) { init_stage = ST_TIMEOUT; return ESP_ERR_TIMEOUT; }
    host.slot = SDMMC_HOST_SLOT_0; host.max_freq_khz = (int)EXPECTED_FREQ_KHZ;
    host.command_timeout_ms = COMMAND_TIMEOUT_MS;
    slot.clk = BSP_SD_CLK; slot.cmd = BSP_SD_CMD; slot.d0 = BSP_SD_D0;
    slot.d1 = BSP_SD_D1; slot.d2 = BSP_SD_D2; slot.d3 = BSP_SD_D3;
    slot.width = 4; slot.cd = SDMMC_SLOT_NO_CD; slot.wp = SDMMC_SLOT_NO_WP; slot.flags = 0;
    init_stage = ST_HOST;
    err = sdmmc_host_init(); host_initialized = (err == ESP_OK); EMIT("HOST", "error=%d", err);
    if (err != ESP_OK) return err;
    if (discovery_elapsed()) { init_stage = ST_TIMEOUT; return ESP_ERR_TIMEOUT; }
    init_stage = ST_SLOT;
    err = sdmmc_host_init_slot(host.slot, &slot); EMIT("SLOT", "error=%d", err);
    if (err != ESP_OK) return err;
    if (discovery_elapsed()) { init_stage = ST_TIMEOUT; return ESP_ERR_TIMEOUT; }
    init_stage = ST_CARD;
    err = sdmmc_card_init(&host, &card); EMIT("CARD", "error=%d", err);
    if (err != ESP_OK) return err;
    if (discovery_elapsed()) { init_stage = ST_TIMEOUT; return ESP_ERR_TIMEOUT; }
    uint32_t sectors = (uint32_t)card.csd.capacity;
    uint32_t secbytes = (uint32_t)card.csd.sector_size;
    uint64_t capacity = (uint64_t)sectors * secbytes;
    unsigned width = (unsigned)host.get_bus_width(host.slot);
    EMIT("GEOMETRY", "sectors=%" PRIu32 ",sector_bytes=%" PRIu32 ",capacity_bytes=%" PRIu64 ",bus_width=%u,real_freq_khz=%d,ddr=%u",
         sectors, secbytes, capacity, width, card.real_freq_khz, (unsigned)card.is_ddr);
    init_stage = ST_GEOMETRY;
    if (!card.is_mem || card.is_mmc || card.is_sdio || sectors != EXPECTED_SECTORS ||
        secbytes != 512u || width != 4u || card.real_freq_khz != (int)EXPECTED_FREQ_KHZ)
        return ESP_ERR_INVALID_SIZE;
    if (compute_cid_digest(&card, cid_digest_hex) != 0) return ESP_FAIL;
    emit_cid();
    if (discovery_elapsed()) { init_stage = ST_TIMEOUT; return ESP_ERR_TIMEOUT; }
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t err;
    make_elf_hex(); start_us = esp_timer_get_time(); last_activity_us = start_us;
    err = init_usb();
    EMIT("BOOT", "reset_reason=%d", (int)esp_reset_reason());
    emit_transport();
    if (err != ESP_OK) fail(ST_RESOURCES, err);
    esp_reset_reason_t reset_reason = esp_reset_reason();
    if (reset_reason != ESP_RST_POWERON && reset_reason != ESP_RST_SW &&
        reset_reason != ESP_RST_USB) fail(ST_RESET, ESP_FAIL);
    if (esp_timer_get_time() - start_us >= (int64_t)DISCOVERY_MS * 1000)
        fail(ST_TIMEOUT, ESP_ERR_TIMEOUT);
    err = init_card();
    if (err != ESP_OK) fail(init_stage, err);
    last_activity_us = esp_timer_get_time();
    if (discovery_elapsed()) fail(ST_TIMEOUT, ESP_ERR_TIMEOUT);
    EMIT("READY", "accepts=BIND");

    char *fields[12]; size_t count = 0;
    if (!read_fields(fields, 12, &count) || count != 9) fail(ST_BIND, ESP_ERR_INVALID_ARG);
    err = bind_command(fields, count);
    if (err != ESP_OK) fail(ST_BIND, err);

    if (!expect_phase_command("LAYOUT", "1")) fail(ST_LAYOUT, ESP_ERR_INVALID_ARG);
    command_count = 1;
    err = verify_dma_capability();
    if (err != ESP_OK) fail(ST_RESOURCES, err);
    err = do_layout();
    if (err != ESP_OK) { emit_layout_failure(err); fail(ST_LAYOUT, err); }
    if (!expect_phase_command("FORMAT", "2")) fail(ST_FORMAT, ESP_ERR_INVALID_ARG);
    command_count = 2;
    err = h38_guard_init(&guard, dma_buffer, sizeof(dma_buffer), guard_backend,
                         &card, guard_clock_ms, NULL);
    if (err != H38_GUARD_OK) fail(ST_RESOURCES, ESP_FAIL);
    guard_ready = true;
    if (h38_guard_bind(&guard, 1, 1, 1) != H38_GUARD_OK ||
        h38_guard_begin_phase(&guard, H38_GUARD_PHASE_FORMAT) != H38_GUARD_OK)
        fail(ST_BIND, ESP_ERR_INVALID_STATE);
    err = h38_fatfs_adapter_init(&fatfs, &card, &guard, dma_buffer, sizeof(dma_buffer));
    if (err != ESP_OK) fail(ST_RESOURCES, err);
    fatfs_ready = true;
    EMIT("FORMAT_START", "volume_sectors=1048576,sector_bytes=512,fat_type=FAT32,fat_count=2,allocation_unit_bytes=4096,format_flags=10,work_buffer_bytes=4096,write_limit_bytes=4194304,read_limit_bytes=16777216,time_limit_ms=120000");
    h38_fatfs_counts_t before = {0}, after = {0};
    FRESULT f_result = FR_INT_ERR;
    uint64_t format_start = (uint64_t)esp_timer_get_time();
    h38_fatfs_get_counts(&fatfs, &before);
    err = h38_fatfs_format(&fatfs, &f_result);
    if (err != ESP_OK || guard.failed) {
        int32_t format_error = guard_failure_error(err != ESP_OK ? err : ESP_FAIL);
        emit_format_failure(f_result, (uint64_t)esp_timer_get_time() - format_start,
                            false, format_error);
        fail(guard_failure_stage(ST_FORMAT), format_error);
    }
    h38_fatfs_mount_info_t mount_info;
    h38_fatfs_counts_t mount_before = {0}, mount_after = {0};
    h38_fatfs_get_counts(&fatfs, &mount_before);
    int64_t mount_start = esp_timer_get_time();
    err = h38_fatfs_mount_initial(&fatfs, &mount_info);
    if (err != ESP_OK) {
        int32_t mount_error = guard_failure_error(fatfs.last_fresult ? fatfs.last_fresult : err);
        h38_fatfs_get_counts(&fatfs, &mount_after);
        /* f_mkfs completed, but no BPB validation or initial mount claim is made. */
        emit_format_failure(f_result, (uint64_t)esp_timer_get_time() - format_start,
                            true, mount_error);
        emit_mount_failure("initial", 0u, esp_timer_get_time() - mount_start,
                           mount_after.read_bytes - mount_before.read_bytes,
                           mount_after.write_bytes - mount_before.write_bytes, mount_error);
        fail(guard_failure_stage(ST_MOUNT), mount_error);
    }
    mount_count = 1;
    if (h38_guard_read(&guard, 0u, 1u, bpb_sector, sizeof(bpb_sector)) != H38_GUARD_OK) {
        int32_t bpb_error = guard_failure_error(ESP_FAIL);
        h38_fatfs_get_counts(&fatfs, &mount_after);
        emit_format_failure(f_result, (uint64_t)esp_timer_get_time() - format_start,
                            true, bpb_error);
        emit_mount_failure("initial", 0u, esp_timer_get_time() - mount_start,
                           mount_after.read_bytes - mount_before.read_bytes,
                           mount_after.write_bytes - mount_before.write_bytes, bpb_error);
        fail(guard_failure_stage(ST_MOUNT), bpb_error);
    }
    char bpb_hash[65], bpb_b64[700]; size_t bpb_encoded = 0;
    sha256_hex(bpb_sector, sizeof(bpb_sector), bpb_hash);
    if (mbedtls_base64_encode((unsigned char *)bpb_b64, sizeof(bpb_b64),
                              &bpb_encoded, bpb_sector, sizeof(bpb_sector)) != 0 || bpb_encoded != 684)
        fail(ST_RESOURCES, ESP_FAIL);
    bpb_b64[bpb_encoded] = '\0';
    h38_fatfs_get_counts(&fatfs, &mount_after);
    int64_t mount_elapsed = esp_timer_get_time() - mount_start;
    h38_fatfs_get_counts(&fatfs, &after);
    uint64_t elapsed = (uint64_t)esp_timer_get_time() - format_start;
    EMIT("FORMAT_RESULT", "f_result=%u,volume_sectors=1048576,sector_bytes=512,fat_type=FAT32,fat_count=2,allocation_unit_bytes=4096,cluster_count=%" PRIu32 ",fat_sectors=%" PRIu32 ",root_cluster_sectors=8,read_bytes=%" PRIu64 ",write_bytes=%" PRIu64 ",write_calls=%" PRIu64 ",max_call_sectors=%" PRIu32 ",trim_requests=%" PRIu64 ",erase_calls=0,bpb_valid=1,bpb_sha256=%s,bpb_base64_private=%s,elapsed_us=%" PRIu64 ",status=ok,error=0",
         (unsigned)f_result, mount_info.cluster_count, (uint32_t)fatfs.fs->fsize,
         after.read_bytes-before.read_bytes, after.write_bytes-before.write_bytes,
         after.write_driver_calls-before.write_driver_calls, after.max_call_sectors,
         after.trim_requests-before.trim_requests, bpb_hash, bpb_b64, elapsed);
    emit_mount_result("initial", 0u, &mount_info, mount_elapsed,
                      mount_after.read_bytes - mount_before.read_bytes,
                      mount_after.write_bytes - mount_before.write_bytes);
    memset(bpb_hash, 0, sizeof(bpb_hash)); memset(bpb_b64, 0, sizeof(bpb_b64));
    memset(bpb_sector, 0, sizeof(bpb_sector));
    if (h38_guard_end_phase(&guard) != H38_GUARD_OK)
        fail(guard_failure_stage(ST_BUDGET), guard_failure_error(ESP_ERR_TIMEOUT));
    if (!expect_phase_command("IO", "3")) fail(ST_SEMANTICS, ESP_ERR_INVALID_ARG);
    command_count = 3;
    if (h38_guard_begin_phase(&guard, H38_GUARD_PHASE_IO) != H38_GUARD_OK)
        fail(guard_failure_stage(ST_BUDGET), guard_failure_error(ESP_ERR_INVALID_STATE));
    h38_filesystem_io_context_t io_context = {
        .adapter = &fatfs,
        .epoch = H38_RUN_EPOCH,
        .runtime_elf_sha256 = elf_hex,
        .source_revision = H38_SOURCE_REVISION,
        .intent_sha256 = H38_INTENT_SHA256,
        .h35_reference_epoch = H35_REFERENCE_EPOCH,
        .private_cid_sha256 = H38_PRIVATE_CID_SHA256,
        .emit = io_emit,
    };
    err = h38_filesystem_io_run(&io_context, &io_result);
    if (err != ESP_OK || guard.failed)
        fail(guard_failure_stage(io_stage(io_result.failure_stage)),
             guard_failure_error(io_result.failure_error ? io_result.failure_error : err));
    if (h38_guard_end_phase(&guard) != H38_GUARD_OK)
        fail(guard_failure_stage(ST_BUDGET), guard_failure_error(ESP_ERR_TIMEOUT));
    if (!expect_phase_command("FINISH", "4")) fail(ST_SEMANTICS, ESP_ERR_INVALID_ARG);
    command_count = 4;
    complete_success();
}
