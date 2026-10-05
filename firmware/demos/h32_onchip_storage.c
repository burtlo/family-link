/* h32: deterministic, unattended on-chip FAT/WL qualification. */
#include <inttypes.h>
#include <stddef.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_ota_ops.h"
#include "sdkconfig.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wear_levelling.h"

#ifndef H32_SENTINEL_ONLY
#define H32_SENTINEL_ONLY 0
#endif
#ifndef H32_RUN_EPOCH
#error "H32_RUN_EPOCH must be a 32-character lowercase hexadecimal string"
#endif
#ifndef H32_PROFILE
#error "H32_PROFILE is required"
#endif
#ifndef CONFIG_SPI_FLASH_YIELD_DURING_ERASE
#define CONFIG_SPI_FLASH_YIELD_DURING_ERASE 0
#endif
#ifndef CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS
#define CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS 0
#endif
#ifndef CONFIG_SPI_FLASH_ERASE_YIELD_TICKS
#define CONFIG_SPI_FLASH_ERASE_YIELD_TICKS 0
#endif
#ifndef CONFIG_ESP_INT_WDT
#define CONFIG_ESP_INT_WDT 0
#endif
#ifndef CONFIG_ESP_INT_WDT_TIMEOUT_MS
#define CONFIG_ESP_INT_WDT_TIMEOUT_MS 0
#endif
#ifndef CONFIG_ESP_TASK_WDT_EN
#define CONFIG_ESP_TASK_WDT_EN 0
#endif
#ifndef CONFIG_ESP_TASK_WDT_INIT
#define CONFIG_ESP_TASK_WDT_INIT 0
#endif
#ifndef CONFIG_ESP_TASK_WDT_TIMEOUT_S
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 0
#endif
#ifndef CONFIG_FATFS_PER_FILE_CACHE
#define CONFIG_FATFS_PER_FILE_CACHE 0
#endif
#ifndef CONFIG_FATFS_LFN_HEAP
#define CONFIG_FATFS_LFN_HEAP 0
#endif
#define EPOCH H32_RUN_EPOCH
_Static_assert(sizeof(EPOCH) == 33, "H32 epoch must be 128-bit lowercase hex");
#define ROOT "/outbox"
#define IO_ROOT ROOT "/io"
#define OPUS_ROOT ROOT "/opus"
#define PCM_ROOT ROOT "/pcm"
#define FAULT_ROOT ROOT "/fault"
#define FILL_ROOT ROOT "/fill"
#define NS_ROOT ROOT "/namespace"
#define PROBE_PATH ROOT "/remount-probe.bin"
#define SENTINEL 0x48333232u
#define RUN_MAGIC 0x48335233u
#define MARKER_SCHEMA 1u
#define OUTBOX_OFFSET 0x230000u
#define OUTBOX_SIZE 0xDD0000u
#define MARKER_LAYOUT 0x48333201u
#define MOUNT_MAX_FILES 32u
#define IO_ITERS 20u
#define SECONDS 180u
#define CHUNK_SECS 2u
#define CHUNKS (SECONDS / CHUNK_SECS)
#define OPUS_RATE 2000u
#define PCM_RATE 67200u /* greater than 64 KiB/s */
#define FLOOR_CHUNK (PCM_RATE * CHUNK_SECS)
#define FLOOR_TEMP FLOOR_CHUNK
#define FLOOR_MANIFEST 4096u
#define FLOOR_METADATA (64u * 1024u)
#define FLOOR_RECOVERY (192u * 1024u)
#define FLOOR_RAW (FLOOR_CHUNK + FLOOR_TEMP + FLOOR_MANIFEST + FLOOR_METADATA + FLOOR_RECOVERY)
#define FLOOR (((FLOOR_RAW + 65535u) / 65536u) * 65536u)
#define ALLOC_UNIT 4096u
#define TX_METADATA_UNITS 8u
#define TX_RESERVE(bytes) ((((bytes) + ALLOC_UNIT - 1u) / ALLOC_UNIT) * ALLOC_UNIT + FLOOR_MANIFEST + TX_METADATA_UNITS * ALLOC_UNIT)
#define FILL_PIECE (256u * 1024u)
#define PROBE_BYTES (64u * 1024u)
#define PROBE_SEED 0x32a55a23u
#define FAULT_POINTS 9u
#define FAULT_REPEATS 10u

typedef struct {
    uint32_t magic, schema, layout, outbox_offset, outbox_size;
    uint32_t allocation_unit, max_files, disk_status_check;
    char epoch[33];
    uint8_t checksum[32];
} marker_t;
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static nvs_handle_t s_nvs;
static uint32_t s_run;
static bool s_emit_epoch;
static bool prior_commits_valid(void);
#define HEARTBEAT_INTERVAL_MS 20u
#define HEARTBEAT_PRIORITY 2u
#define HEARTBEAT_CORE 0
static TaskHandle_t s_heartbeat_task;
static portMUX_TYPE s_heartbeat_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_heartbeat_last, s_heartbeat_max;
static uint32_t s_heartbeat_samples;
static uint32_t s_watchdog_events;

static void heartbeat_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
        portENTER_CRITICAL(&s_heartbeat_lock);
        int64_t now = esp_timer_get_time();
        int64_t gap = now - s_heartbeat_last;
        if (gap > s_heartbeat_max) s_heartbeat_max = gap;
        s_heartbeat_last = now;
        s_heartbeat_samples++;
        portEXIT_CRITICAL(&s_heartbeat_lock);
    }
}
static void heartbeat_snapshot(int64_t *gap, uint32_t *samples)
{
    portENTER_CRITICAL(&s_heartbeat_lock);
    int64_t now = esp_timer_get_time();
    *gap = s_heartbeat_max;
    if (now - s_heartbeat_last > *gap) *gap = now - s_heartbeat_last;
    *samples = s_heartbeat_samples;
    portEXIT_CRITICAL(&s_heartbeat_lock);
}

static void rec(const char *kind, const char *fmt, ...)
{
    va_list ap;
    printf("H32,%s,", kind);
    if (s_emit_epoch) printf("epoch=%s,", EPOCH);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n"); fflush(stdout);
}
static void monitor_report(const char *workload, const char *event, int64_t gap, uint32_t samples)
{
    rec("HEARTBEAT", "schema=2,workload=%s,event=%s,priority=%u,core=%d,storage_priority=%u,storage_core=%d,interval_ms=%u,timer_resolution_us=1,max_gap_us=%"PRId64",samples=%u,stack_bytes=%u,watchdog_events=%u",
        workload,event,HEARTBEAT_PRIORITY,HEARTBEAT_CORE,(unsigned)uxTaskPriorityGet(NULL),xPortGetCoreID(),HEARTBEAT_INTERVAL_MS,gap,samples,
        (unsigned)uxTaskGetStackHighWaterMark(s_heartbeat_task),s_watchdog_events);
    rec("MEMORY", "schema=2,workload=%s,event=%s,heap=%u,heap_min=%u,heap_largest=%u,psram=%u,psram_min=%u,storage_stack_bytes=%u,heartbeat_stack_bytes=%u",
        workload,event,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)uxTaskGetStackHighWaterMark(NULL),(unsigned)uxTaskGetStackHighWaterMark(s_heartbeat_task));
}
static void monitor(const char *workload, const char *event)
{
    if (!strcmp(event, "start")) {
        portENTER_CRITICAL(&s_heartbeat_lock);
        s_heartbeat_last = esp_timer_get_time();
        s_heartbeat_max = 0; s_heartbeat_samples = 0;
        portEXIT_CRITICAL(&s_heartbeat_lock);
    }
    int64_t gap; uint32_t samples;
    heartbeat_snapshot(&gap, &samples);
    monitor_report(workload,event,gap,samples);
}
/* Monotonic absolute schedule; tick rounding must never begin a chunk early. */
static void wait_until_us(int64_t deadline)
{
    while (true) {
        int64_t remaining=deadline-esp_timer_get_time();
        if (remaining<=0) return;
        int64_t tick_us=1000000LL/configTICK_RATE_HZ;
        TickType_t ticks=(TickType_t)((remaining+tick_us-1)/tick_us);
        vTaskDelay(ticks?ticks:1);
    }
}
static bool profile_record(void)
{
    const esp_app_desc_t *app=esp_app_get_description();
    char elf_sha256[65];
    for(unsigned i=0;i<32;i++)sprintf(elf_sha256+2*i,"%02x",app->app_elf_sha256[i]);
    elf_sha256[64]=0;
    bool yield = CONFIG_SPI_FLASH_YIELD_DURING_ERASE;
    bool valid = CONFIG_FREERTOS_HZ == 100 &&
        ((!strcmp(H32_PROFILE,"baseline") && yield && CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS == 20 && CONFIG_SPI_FLASH_ERASE_YIELD_TICKS == 1) ||
         (!strcmp(H32_PROFILE,"no_yield") && !yield) ||
         (!strcmp(H32_PROFILE,"bounded_yield") && yield && CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS == 100 && CONFIG_SPI_FLASH_ERASE_YIELD_TICKS == 1));
    rec("PROFILE", "schema=2,profile=%s,elf_sha256=%s,erase_yield=%u,erase_yield_duration_ms=%u,erase_yield_ticks=%u,tick_hz=%u,int_wdt=%u,int_wdt_timeout_ms=%u,task_wdt=%u,task_wdt_init=%u,task_wdt_timeout_s=%u,wl_sector_bytes=%u,fatfs_per_file_cache=%u,fatfs_lfn_heap=%u,fatfs_max_lfn=%u,fatfs_timeout_ms=%u,status=%s",
        H32_PROFILE,elf_sha256,(unsigned)yield,(unsigned)CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS,(unsigned)CONFIG_SPI_FLASH_ERASE_YIELD_TICKS,
        (unsigned)CONFIG_FREERTOS_HZ,(unsigned)CONFIG_ESP_INT_WDT,(unsigned)CONFIG_ESP_INT_WDT_TIMEOUT_MS,
        (unsigned)CONFIG_ESP_TASK_WDT_EN,(unsigned)CONFIG_ESP_TASK_WDT_INIT,(unsigned)CONFIG_ESP_TASK_WDT_TIMEOUT_S,
        (unsigned)CONFIG_WL_SECTOR_SIZE,(unsigned)CONFIG_FATFS_PER_FILE_CACHE,(unsigned)CONFIG_FATFS_LFN_HEAP,
        (unsigned)CONFIG_FATFS_MAX_LFN,(unsigned)CONFIG_FATFS_TIMEOUT_MS,valid?"pass":"fail");
    return valid;
}
static void fail(const char *stage, const char *why)
{
    rec("FAIL", "run=%08"PRIx32",stage=%s,reason=%s,action=halt", s_run, stage, why);
    while (true) vTaskDelay(portMAX_DELAY);
}
static uint32_t get32(const char *key, uint32_t fallback)
{
    (void)fallback;
    uint32_t v;
    if(nvs_get_u32(s_nvs,key,&v)!=ESP_OK)fail("progress","missing_or_invalid_key");
    return v;
}
static bool put32(const char *key, uint32_t v)
{
    return nvs_set_u32(s_nvs, key, v) == ESP_OK && nvs_commit(s_nvs) == ESP_OK;
}
static uint8_t byte_at(uint32_t seed, size_t at)
{
    uint32_t x = seed ^ (uint32_t)at * 2654435761u;
    x ^= x >> 13; x *= 1274126177u; x ^= x >> 16;
    return (uint8_t)x;
}
static bool write_pattern(FILE *f, size_t bytes, uint32_t seed)
{
    uint8_t b[4096];
    for (size_t off = 0; off < bytes;) {
        size_t n = bytes - off < sizeof(b) ? bytes - off : sizeof(b);
        for (size_t i = 0; i < n; i++) b[i] = byte_at(seed, off + i);
        if (fwrite(b, 1, n, f) != n) return false;
        off += n;
    }
    return true;
}
static bool sync_close(FILE *f)
{
    bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    return fclose(f) == 0 && ok;
}
static bool verify(const char *path, size_t bytes, uint32_t seed, char hex[65])
{
    FILE *f = fopen(path, "rb");
    if (!f) { strcpy(hex, "unavailable"); return false; }
    uint8_t b[4096], digest[32];
    size_t off = 0;
    bool ok = true;
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
    while (off < bytes) {
        size_t n = bytes - off < sizeof(b) ? bytes - off : sizeof(b);
        if (fread(b, 1, n, f) != n) { ok = false; break; }
        for (size_t i = 0; i < n; i++) if (b[i] != byte_at(seed, off + i)) { ok = false; break; }
        if (!ok) break;
        mbedtls_sha256_update(&c, b, n); off += n;
    }
    if (fgetc(f) != EOF) ok = false;
    mbedtls_sha256_finish(&c, digest); mbedtls_sha256_free(&c); fclose(f);
    for (unsigned i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", digest[i]);
    hex[64] = 0;
    return ok && off == bytes;
}
static bool space(uint64_t *total, uint64_t *free_bytes)
{
    return esp_vfs_fat_info(ROOT, total, free_bytes) == ESP_OK;
}
/* Complete bounded read, with the digest calculated over the exact bytes read. */
static bool outbox_erased_scan(const esp_partition_t *p, bool *all_erased)
{
    uint8_t b[4096], digest[32]; char hex[65];
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
    int64_t start = esp_timer_get_time();
    *all_erased = true;
    for (size_t off = 0; off < p->size; off += sizeof(b)) {
        size_t n = p->size - off < sizeof(b) ? p->size - off : sizeof(b);
        if (esp_partition_read(p, off, b, n) != ESP_OK) {
            mbedtls_sha256_free(&c);
            rec("ERASE_SCAN", "bytes=%u,elapsed_us=%"PRId64",all_erased=unknown,sha256=unavailable,status=fail",
                (unsigned)off, esp_timer_get_time() - start);
            return false;
        }
        for (size_t i = 0; i < n; i++) if (b[i] != 0xff) *all_erased = false;
        mbedtls_sha256_update(&c, b, n);
    }
    mbedtls_sha256_finish(&c, digest); mbedtls_sha256_free(&c);
    for (unsigned i = 0; i < sizeof(digest); i++) sprintf(hex + 2*i, "%02x", digest[i]);
    hex[64] = 0;
    rec("ERASE_SCAN", "bytes=%u,elapsed_us=%"PRId64",all_erased=%s,sha256=%s,status=pass",
        (unsigned)p->size, esp_timer_get_time() - start, *all_erased ? "yes" : "no", hex);
    return true;
}
static bool layout(const esp_partition_t **out)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *f = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
    const esp_partition_t *o = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_FAT, "outbox");
    bool ok = f && o && running && running->address == f->address &&
        running->size == f->size && f->address == 0x10000 && f->size == 0x220000 &&
        o->address == OUTBOX_OFFSET && o->size == OUTBOX_SIZE &&
        f->address + f->size == o->address && o->address + o->size == 0x1000000;
    rec("PARTITION", "factory_offset=0x%lx,factory_size=0x%lx,running_offset=0x%lx,running_size=0x%lx,outbox_offset=0x%lx,outbox_size=0x%lx,status=%s",
        f ? (unsigned long)f->address : 0ul, f ? (unsigned long)f->size : 0ul,
        running ? (unsigned long)running->address : 0ul, running ? (unsigned long)running->size : 0ul,
        o ? (unsigned long)o->address : 0ul, o ? (unsigned long)o->size : 0ul,
        ok ? "pass" : "fail");
    *out = o; return ok;
}
static bool marker_write(void)
{
    marker_t m = {0};
    m.magic = RUN_MAGIC; m.schema = MARKER_SCHEMA; m.layout = MARKER_LAYOUT;
    m.outbox_offset = OUTBOX_OFFSET; m.outbox_size = OUTBOX_SIZE;
    m.allocation_unit = ALLOC_UNIT; m.max_files = MOUNT_MAX_FILES;
    m.disk_status_check = 1;
    memcpy(m.epoch, EPOCH, sizeof(m.epoch));
    mbedtls_sha256((const unsigned char *)&m, offsetof(marker_t, checksum), m.checksum, 0);
    FILE *f = fopen(ROOT "/run.marker.part", "wb");
    if (!f) return false;
    if (fwrite(&m, 1, sizeof(m), f) != sizeof(m)) { fclose(f); return false; }
    if (!sync_close(f)) return false;
    return rename(ROOT "/run.marker.part", ROOT "/run.marker") == 0;
}
static bool ensure_dirs(void)
{
    const char *dirs[]={IO_ROOT,OPUS_ROOT,PCM_ROOT,FAULT_ROOT,FILL_ROOT,NS_ROOT};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++)
        if(mkdir(dirs[i],0775)!=0&&errno!=EEXIST)return false;
    return true;
}
static bool marker_read(marker_t *m)
{
    FILE *f = fopen(ROOT "/run.marker", "rb");
    if (!f) return false;
    memset(m, 0, sizeof(*m));
    bool ok = fread(m, 1, sizeof(*m), f) == sizeof(*m) && fgetc(f) == EOF;
    fclose(f);
    uint8_t digest[32];
    mbedtls_sha256((const unsigned char *)m, offsetof(marker_t, checksum), digest, 0);
    return ok && m->magic == RUN_MAGIC && m->schema == MARKER_SCHEMA &&
        m->layout == MARKER_LAYOUT && m->outbox_offset == OUTBOX_OFFSET &&
        m->outbox_size == OUTBOX_SIZE && m->allocation_unit == ALLOC_UNIT &&
        m->max_files == MOUNT_MAX_FILES && m->disk_status_check == 1 &&
        memcmp(m->epoch, EPOCH, sizeof(m->epoch)) == 0 &&
        memcmp(m->checksum, digest, sizeof(digest)) == 0;
}
static bool state_reset(uint32_t run)
{
    if(nvs_erase_all(s_nvs)!=ESP_OK||nvs_set_str(s_nvs,"epoch",EPOCH)!=ESP_OK||
        nvs_set_u32(s_nvs,"run_id",run)!=ESP_OK)return false;
    const char*keys[]={"phase","remount","remount_seen","fault_cur","fault_active","fill_bytes","fill_count"};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)
        if(nvs_set_u32(s_nvs,keys[i],0)!=ESP_OK)return false;
    return nvs_commit(s_nvs)==ESP_OK;
}
static bool mount_fs(const esp_partition_t *outbox)
{
    rec("MOUNT", "event=enter");
    esp_vfs_fat_mount_config_t cfg = {.format_if_mount_failed=false, .max_files=32,
        .allocation_unit_size=4096, .disk_status_check_enable=true};
    bool blank = false;
    if (!outbox_erased_scan(outbox, &blank)) return false;
    int64_t format_us = 0;
    if (blank) {
        /* The exact runtime-discovered partition has already passed a full scan. */
        cfg.format_if_mount_failed = true;
        int64_t ft = esp_timer_get_time();
        esp_err_t fe = esp_vfs_fat_spiflash_format_cfg_rw_wl(ROOT, "outbox", &cfg);
        format_us = esp_timer_get_time() - ft;
        rec("FORMAT", "partition=outbox,format_us=%"PRId64",status=%s", format_us,
            fe == ESP_OK ? "pass" : "fail");
        if (fe != ESP_OK) return false;
        cfg.format_if_mount_failed = false;
    }
    int64_t t = esp_timer_get_time();
    esp_err_t e = esp_vfs_fat_spiflash_mount_rw_wl(ROOT, "outbox", &cfg, &s_wl);
    int64_t elapsed = esp_timer_get_time()-t;
    if (e != ESP_OK) return false;
    uint64_t total, free_bytes;
    if (!space(&total, &free_bytes)) return false;
    rec("MOUNT", "kind=%s,mount_us=%"PRId64",format_us=%"PRId64",total=%"PRIu64",free=%"PRIu64",wl_size=%u,wl_sector=%u",
        blank?"cold_after_format":"cold_existing", elapsed, format_us,
        total, free_bytes, (unsigned)wl_size(s_wl), (unsigned)wl_sector_size(s_wl));
    if (blank) {
        s_run = 0;
        for (unsigned i = 0; i < 8; i++) {
            char c = EPOCH[i];
            s_run = (s_run << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        if (!s_run) s_run = 1;
        if (!ensure_dirs() || !marker_write()) return false;
    }
    int64_t wt = esp_timer_get_time();
    if (esp_vfs_fat_spiflash_unmount_rw_wl(ROOT, s_wl) != ESP_OK) return false;
    s_wl = WL_INVALID_HANDLE;
    if (esp_vfs_fat_spiflash_mount_rw_wl(ROOT, "outbox", &cfg, &s_wl) != ESP_OK) return false;
    rec("MOUNT", "kind=warm_remount,mount_us=%"PRId64",format_us=0", esp_timer_get_time()-wt);
    marker_t m;
    if (!marker_read(&m)) return false;
    rec("MARKER", "schema=%u,layout=%08x,verify=pass", MARKER_SCHEMA, MARKER_LAYOUT);
    if (blank) {
        if (!state_reset(s_run)) return false;
        rec("RUN", "run=%08"PRIx32",event=new,blank_authority=full_scan,nvs_action=reset,status=pass", s_run);
    } else {
        char state_epoch[sizeof(m.epoch)] = {0}; size_t n = sizeof(state_epoch);
        if (nvs_get_str(s_nvs, "epoch", state_epoch, &n) != ESP_OK ||
            n != sizeof(state_epoch) || memcmp(state_epoch, EPOCH, sizeof(state_epoch)) != 0)
            return false;
        s_run = get32("run_id", 0);
        if (!s_run) return false;
        rec("RUN", "run=%08"PRIx32",event=resume,blank_authority=marker,nvs_action=preserve,status=pass", s_run);
    }
    if (!ensure_dirs()) return false;
    return true;
}
static bool write_file(const char *path,size_t bytes,uint32_t seed)
{
    FILE *f=fopen(path,"wb");
    if(!f)return false;
    if(!write_pattern(f,bytes,seed)){fclose(f);return false;}
    return sync_close(f);
}
static bool probe_create(void)
{
    char hash[65];
    unlink(PROBE_PATH ".part");unlink(PROBE_PATH);
    bool ok=write_file(PROBE_PATH ".part",PROBE_BYTES,PROBE_SEED)&&rename(PROBE_PATH ".part",PROBE_PATH)==0&&
        verify(PROBE_PATH,PROBE_BYTES,PROBE_SEED,hash);
    rec("PROBE","run=%08"PRIx32",event=created,path=remount-probe.bin,bytes=%u,seed=%08x,sha256=%s,verify=%s",
        s_run,PROBE_BYTES,PROBE_SEED,ok?hash:"unavailable",ok?"pass":"fail");
    return ok;
}
static bool probe_verify(unsigned cycle, bool initial, unsigned phase)
{
    char hash[65];bool ok=verify(PROBE_PATH,PROBE_BYTES,PROBE_SEED,hash);
    if (initial)
        rec("REMOUNT","run=%08"PRIx32",cycle=%u,formatted=no,probe_bytes=%u,probe_sha256=%s,verify=%s",
            s_run,cycle,PROBE_BYTES,ok?hash:"unavailable",ok?"pass":"fail");
    else
        rec("PROBE_VERIFY","schema=2,run=%08"PRIx32",phase=%u,fault_cur=%u,fault_active=%u,cycle=%u,identity=retained,formatted=no,probe_bytes=%u,probe_sha256=%s,verify=%s",
            s_run,phase,get32("fault_cur",0),get32("fault_active",0),cycle,PROBE_BYTES,ok?hash:"unavailable",ok?"pass":"fail");
    return ok;
}
static bool commit(const char *stem, size_t bytes, uint32_t seed, bool retain)
{
    char p[96], fpath[96], hash[65];
    snprintf(p,sizeof(p),IO_ROOT"/%s.part",stem); snprintf(fpath,sizeof(fpath),IO_ROOT"/%s.bin",stem);
    unlink(p); unlink(fpath);
    uint64_t total, free_before = 0, free_after = 0;
    if(!space(&total,&free_before))return false;
    int64_t t0=esp_timer_get_time(); FILE *f=fopen(p,"wb"); int64_t t1=esp_timer_get_time();
    if(!f)return false;
    if(!write_pattern(f,bytes,seed)){fclose(f);return false;} int64_t t2=esp_timer_get_time();
    if(fflush(f)||fsync(fileno(f))){fclose(f);return false;} int64_t t3=esp_timer_get_time();
    if(fclose(f))return false;
    int64_t t4=esp_timer_get_time();
    if(rename(p,fpath))return false;
    int64_t t5=esp_timer_get_time();
    bool ok=verify(fpath,bytes,seed,hash); int64_t t6=esp_timer_get_time();
    int64_t delete_us=0;
    if(!retain){int64_t td=esp_timer_get_time();if(unlink(fpath))return false;delete_us=esp_timer_get_time()-td;}
    if(!space(&total,&free_after))return false;
    rec("IO","run=%08"PRIx32",file=%s,bytes=%u,open_us=%"PRId64",write_us=%"PRId64",flush_us=%"PRId64",close_us=%"PRId64",rename_us=%"PRId64",read_verify_us=%"PRId64",delete_us=%"PRId64",free_before=%"PRIu64",free_after=%"PRIu64",verify=%s,sha256=%s,retained=%s",
        s_run,stem,(unsigned)bytes,t1-t0,t2-t1,t3-t2,t4-t3,t5-t4,t6-t5,delete_us,
        free_before,free_after,ok?"pass":"fail",hash,retain?"yes":"no");
    return ok;
}
static bool io_write(void)
{
    for(unsigned si=0;si<2;si++){size_t bytes=si?192*1024:64*1024;
        for(unsigned i=0;i<IO_ITERS;i++){char stem[40];bool keep=i==0||i==10||i==19;
            snprintf(stem,sizeof(stem),"io-%u-%02u",(unsigned)(bytes/1024),i);
            if(!commit(stem,bytes,0x100000u+(uint32_t)bytes+i,keep))return false;}}
    return true;
}
static bool io_validate(void)
{
    const unsigned reps[]={0,10,19};
    for(unsigned si=0;si<2;si++){size_t bytes=si?192*1024:64*1024;
        for(unsigned j=0;j<3;j++){char p[80],h[65];snprintf(p,sizeof(p),IO_ROOT"/io-%u-%02u.bin",(unsigned)(bytes/1024),reps[j]);
            bool ok=verify(p,bytes,0x100000u+(uint32_t)bytes+reps[j],h);
            rec("IO_REMOUNT","run=%08"PRIx32",bytes=%u,index=%u,verify=%s,sha256=%s",s_run,(unsigned)bytes,reps[j],ok?"pass":"fail",h);
            if(!ok)return false;}}
    return true;
}
static bool ends_with(const char *s,const char *suffix)
{
    size_t a=strlen(s),b=strlen(suffix);return a>=b&&!strcmp(s+a-b,suffix);
}
static bool replacement_test(void)
{
    const char *part=NS_ROOT "/replace.part",*final=NS_ROOT "/replace.bin";
    char h[65];unlink(part);unlink(final);
    if(!write_file(final,16384,0x610001u)||!verify(final,16384,0x610001u,h)||!write_file(part,24576,0x610002u))return false;
    errno=0;int rr=rename(part,final),saved=rr?errno:0;
    bool replaced=rr==0,new_valid=replaced&&verify(final,24576,0x610002u,h);
    bool old_preserved=!replaced&&verify(final,16384,0x610001u,h);
    bool part_present=access(part,F_OK)==0;
    bool safe=(replaced&&new_valid&&!part_present)||(!replaced&&old_preserved&&part_present);
    if(part_present&&unlink(part))safe=false;
    rec("RENAME","run=%08"PRIx32",case=replace_existing,same_directory=yes,outcome=%s,errno=%d,old_preserved=%s,new_valid=%s,part_present=%s,status=%s",
        s_run,replaced?"replaced":"not_supported",saved,old_preserved?"yes":"no",new_valid?"yes":"no",part_present?"yes":"no",safe?"pass":"fail");
    return safe;
}
static bool scan_namespace(const char *event,unsigned *entries,unsigned *parts,bool cleanup)
{
    DIR*d=opendir(NS_ROOT);if(!d)return false;struct dirent*de;unsigned seen=0,stale=0,removed=0,unknown=0;
    bool final_seen=false,ok=true;char path[280],h[65];
    while((de=readdir(d))){if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;seen++;
        if(!strcmp(de->d_name,"replace.bin"))final_seen=true;
        if(ends_with(de->d_name,".part")){stale++;if(cleanup){snprintf(path,sizeof(path),NS_ROOT"/%s",de->d_name);
            if(unlink(path))ok=false;else removed++;}}
        else if(strcmp(de->d_name,"replace.bin"))unknown++;}
    closedir(d);bool final_valid=final_seen&&(verify(NS_ROOT"/replace.bin",24576,0x610002u,h)||verify(NS_ROOT"/replace.bin",16384,0x610001u,h));
    bool prior=prior_commits_valid();
    rec("DIR_SCAN","run=%08"PRIx32",case=stale_part,event=%s,entries=%u,stale_seen=%u,final_seen=%s,final_valid=%s,removed=%u,unknown_removed=%u,prior_valid=%s,status=%s",
        s_run,event,seen,stale,final_seen?"yes":"no",final_valid?"yes":"no",removed,unknown,prior?"yes":"no",ok&&final_valid&&!unknown&&prior?"pass":"fail");
    if (entries) *entries = seen;
    if (parts) *parts = stale;
    return ok && final_valid && !unknown && prior;
}
static bool namespace_test(void)
{
    if(!replacement_test())return false;
    if(!write_file(NS_ROOT"/stale.part",8192,0x620001u))return false;
    unsigned before=0,parts=0,after=0,after_parts=0;
    if(!scan_namespace("before_cleanup",&before,&parts,true)||parts!=1)return false;
    if(!scan_namespace("after_cleanup",&after,&after_parts,false)||after_parts||after+1!=before)return false;
    return true;
}
static bool manifest(const char *name,unsigned i,size_t bytes,uint32_t seed,const char *hash)
{
    const char *dir=!strcmp(name,"opus")?OPUS_ROOT:!strcmp(name,"pcm")?PCM_ROOT:FAULT_ROOT;
    char p[112],fpath[112];snprintf(p,sizeof(p),"%s/%03u.manifest.part",dir,i);
    snprintf(fpath,sizeof(fpath),"%s/%03u.manifest",dir,i);
    FILE*f=fopen(p,"wb");if(!f)return false;
    if(fprintf(f,"epoch=%s\nrun=%08"PRIx32"\nindex=%u\nbytes=%u\nseed=%08"PRIx32"\nsha256=%s\n",EPOCH,s_run,i,(unsigned)bytes,seed,hash)<=0){fclose(f);return false;}
    if(!sync_close(f))return false;
    unlink(fpath);return rename(p,fpath)==0;
}
static bool manifest_valid(const char *path,unsigned i,size_t bytes,uint32_t seed,const char *hash)
{
    char expected[256],actual[256];
    int len=snprintf(expected,sizeof(expected),"epoch=%s\nrun=%08"PRIx32"\nindex=%u\nbytes=%u\nseed=%08"PRIx32"\nsha256=%s\n",EPOCH,s_run,i,(unsigned)bytes,seed,hash);
    if(len<=0||(size_t)len>=sizeof(expected))return false;
    FILE*f=fopen(path,"rb");if(!f)return false;
    size_t got=fread(actual,1,sizeof(actual),f);
    bool ok=got==(size_t)len&&!ferror(f)&&fgetc(f)==EOF&&!memcmp(actual,expected,got);
    fclose(f);return ok;
}
static bool cadence(const char *name,size_t rate,uint32_t base)
{
    const int64_t period = CHUNK_SECS * 1000000LL;
    int64_t epoch,mw=0,mf=0,mr=0,mm=0,max_late=0,max_finish_late=0;
    unsigned miss=0,service_miss=0; uint64_t transaction_us=0;
    uint64_t total,free_bytes,minfree=UINT64_MAX;
    uint32_t minheap=UINT32_MAX,minpsram=UINT32_MAX,minstack=UINT32_MAX,backlog_high=0;
    mbedtls_sha256_context aggregate;uint8_t aggregate_digest[32];char aggregate_hex[65];
    mbedtls_sha256_init(&aggregate);mbedtls_sha256_starts(&aggregate,0);
    monitor(name,"start");
    epoch=esp_timer_get_time();
    const char *dir=!strcmp(name,"opus")?OPUS_ROOT:PCM_ROOT;
    for(unsigned i=0;i<CHUNKS;i++) {
        char p[96],fpath[96],h[65];size_t bytes=rate*CHUNK_SECS;
        snprintf(p,sizeof(p),"%s/%03u.part",dir,i);snprintf(fpath,sizeof(fpath),"%s/%03u.bin",dir,i);
        int64_t scheduled=epoch+(int64_t)i*period;
        wait_until_us(scheduled);
        int64_t a=esp_timer_get_time();FILE*f=fopen(p,"wb");int64_t opened=esp_timer_get_time();
        if(!f)return false;
        if(!write_pattern(f,bytes,base+i)){fclose(f);return false;}
        int64_t b=esp_timer_get_time();
        if(fflush(f)||fsync(fileno(f))){fclose(f);return false;}int64_t c=esp_timer_get_time();
        if(fclose(f))return false;
        int64_t closed=esp_timer_get_time();
        if(rename(p,fpath))return false;
        int64_t d=esp_timer_get_time();if(!verify(fpath,bytes,base+i,h))return false;
        int64_t e=esp_timer_get_time();if(!manifest(name,i,bytes,base+i,h))return false;
        int64_t z=esp_timer_get_time();
        mbedtls_sha256_update(&aggregate,(const unsigned char*)h,64);
        int64_t late=a>scheduled?a-scheduled:0;
        int64_t finish_late=z>scheduled+period?z-scheduled-period:0;
        uint32_t backlog=(uint32_t)(late/period);
        uint32_t missed_periods=(uint32_t)(late/period);
        if(backlog>backlog_high)backlog_high=backlog;
        if(late>max_late)max_late=late;
        if(finish_late>max_finish_late)max_finish_late=finish_late;
        if(b-opened>mw)mw=b-opened;
        if(c-b>mf)mf=c-b;
        if(d-closed>mr)mr=d-closed;
        if(z-e>mm)mm=z-e;
        if(finish_late)miss++;
        if(z-a>=period)service_miss++;
        transaction_us+=(uint64_t)(z-a);
        if(!space(&total,&free_bytes)){mbedtls_sha256_free(&aggregate);return false;}
        uint32_t heap=heap_caps_get_free_size(MALLOC_CAP_INTERNAL),psram=heap_caps_get_free_size(MALLOC_CAP_SPIRAM),stack=uxTaskGetStackHighWaterMark(NULL);
        if(free_bytes<minfree)minfree=free_bytes;
        if(heap<minheap)minheap=heap;
        if(psram<minpsram)minpsram=psram;
        if(stack<minstack)minstack=stack;
        int64_t heartbeat_gap;uint32_t heartbeat_samples;
        heartbeat_snapshot(&heartbeat_gap,&heartbeat_samples);
        rec("CHUNK","schema=2,run=%08"PRIx32",stream=%s,index=%u,bytes=%u,open_us=%"PRId64",write_us=%"PRId64",flush_us=%"PRId64",close_us=%"PRId64",rename_us=%"PRId64",read_verify_us=%"PRId64",manifest_us=%"PRId64",total_us=%"PRId64",scheduled_start_us=%"PRId64",actual_start_us=%"PRId64",finish_us=%"PRId64",lateness_us=%"PRId64",finish_lateness_us=%"PRId64",max_lateness_us=%"PRId64",missed_periods=%u,backlog=%u,heartbeat_max_gap_us=%"PRId64",heartbeat_samples=%u,watchdog_events=%u,free=%"PRIu64",heap=%u,psram=%u,stack_words=%u,stack_unit=bytes,stack_bytes=%u,sha256=%s",
            s_run,name,i,(unsigned)bytes,opened-a,b-opened,c-b,closed-c,d-closed,e-d,z-e,z-a,
            scheduled,a,z,late,finish_late,max_late,missed_periods,backlog,heartbeat_gap,heartbeat_samples,s_watchdog_events,free_bytes,heap,psram,stack,stack,h);
    }
    wait_until_us(epoch+CHUNKS*period);
    int64_t elapsed=esp_timer_get_time()-epoch,heartbeat_gap;uint32_t heartbeat_samples;
    heartbeat_snapshot(&heartbeat_gap,&heartbeat_samples);
    mbedtls_sha256_finish(&aggregate,aggregate_digest);mbedtls_sha256_free(&aggregate);
    for(unsigned i=0;i<32;i++)sprintf(aggregate_hex+2*i,"%02x",aggregate_digest[i]);
    aggregate_hex[64]=0;
    rec("CADENCE","schema=2,run=%08"PRIx32",stream=%s,seconds=%u,rate=%u,chunk_seconds=%u,chunks=%u,deadline_miss=%u,scheduled_deadline_miss=%u,service_deadline_miss=%u,backlog_high=%u,max_write_us=%"PRId64",max_flush_us=%"PRId64",max_rename_us=%"PRId64",max_manifest_us=%"PRId64",actual_elapsed_us=%"PRId64",transaction_us=%"PRIu64",max_lateness_us=%"PRId64",max_finish_lateness_us=%"PRId64",heartbeat_max_gap_us=%"PRId64",heartbeat_samples=%u,watchdog_events=%u,min_free=%"PRIu64",min_heap=%u,min_psram=%u,min_stack_words=%u,stack_unit=bytes,min_stack_bytes=%u,aggregate_kind=ordered_chunk_sha256_hex,aggregate_sha256=%s",
        s_run,name,SECONDS,(unsigned)rate,CHUNK_SECS,CHUNKS,miss,miss,service_miss,backlog_high,mw,mf,mr,mm,elapsed,transaction_us,max_late,max_finish_late,heartbeat_gap,heartbeat_samples,s_watchdog_events,minfree,minheap,minpsram,minstack,minstack,aggregate_hex);
    monitor_report(name,"end",heartbeat_gap,heartbeat_samples);
    return miss==0&&service_miss==0;
}
static bool cadence_validate(const char*name,size_t rate,uint32_t base)
{
    const char *dir=!strcmp(name,"opus")?OPUS_ROOT:PCM_ROOT;
    for(unsigned i=0;i<CHUNKS;i++){char d[96],m[112],h[65];snprintf(d,sizeof(d),"%s/%03u.bin",dir,i);snprintf(m,sizeof(m),"%s/%03u.manifest",dir,i);
        bool ok=verify(d,rate*CHUNK_SECS,base+i,h)&&manifest_valid(m,i,rate*CHUNK_SECS,base+i,h);if(!ok)return false;
        bool keep=i==0||i==CHUNKS/2||i==CHUNKS-1;if(!keep&&(unlink(d)||unlink(m)))return false;
        if(keep)rec("CHUNK_REMOUNT","run=%08"PRIx32",stream=%s,index=%u,verify=pass,sha256=%s,retained=yes",s_run,name,i,h);}
    uint64_t t,f;if(!space(&t,&f))return false;rec("BACKLOG","run=%08"PRIx32",stream=%s,validated=%u,reclaimed=%u,retained=3,free_after=%"PRIu64,s_run,name,CHUNKS,CHUNKS-3,f);return true;
}
static void fault_paths(unsigned point,char p[80],char f[80],char m[96])
{snprintf(p,80,FAULT_ROOT"/%u.part",point);snprintf(f,80,FAULT_ROOT"/%u.bin",point);snprintf(m,96,FAULT_ROOT"/%03u.manifest",point);}
static bool prior_commits_valid(void)
{
    char h[65],p[112],m[112];marker_t marker;
    const unsigned io_reps[]={0,10,19},chunk_reps[]={0,CHUNKS/2,CHUNKS-1};
    if(!marker_read(&marker)||!verify(PROBE_PATH,PROBE_BYTES,PROBE_SEED,h))return false;
    for(unsigned si=0;si<2;si++)for(unsigned j=0;j<3;j++){
        size_t bytes=si?192*1024:64*1024;
        snprintf(p,sizeof(p),IO_ROOT"/io-%u-%02u.bin",(unsigned)(bytes/1024),io_reps[j]);
        if(!verify(p,bytes,0x100000u+(uint32_t)bytes+io_reps[j],h))return false;
    }
    if(!verify(NS_ROOT"/replace.bin",24576,0x610002u,h)&&
        !verify(NS_ROOT"/replace.bin",16384,0x610001u,h))return false;
    uint32_t phase=get32("phase",0);
    for(unsigned si=0;si<2;si++){
        if(phase<(si?3u:2u))continue;
        const char*dir=si?PCM_ROOT:OPUS_ROOT;size_t bytes=(si?PCM_RATE:OPUS_RATE)*CHUNK_SECS;
        uint32_t base=si?0x520000u:0x510000u;
        for(unsigned j=0;j<3;j++){
            unsigned i=chunk_reps[j];
            snprintf(p,sizeof(p),"%s/%03u.bin",dir,i);snprintf(m,sizeof(m),"%s/%03u.manifest",dir,i);
            if(!verify(p,bytes,base+i,h)||!manifest_valid(m,i,bytes,base+i,h))return false;
        }
    }
    return true;
}
static bool fault_recover(unsigned cursor)
{
    unsigned point=cursor/FAULT_REPEATS,cycle=cursor%FAULT_REPEATS;char p[80],f[80],m[96],h[65];fault_paths(point,p,f,m);
    bool pe=access(p,F_OK)==0,fe=access(f,F_OK)==0,me=access(m,F_OK)==0,fv=fe&&verify(f,65536,0x700000u+cursor,h);
    bool metadata_valid=me&&fv&&manifest_valid(m,point,65536,0x700000u+cursor,h);
    bool prior=prior_commits_valid();
    bool deleted_manifest_valid=point==7&&me&&!fe;
    if(deleted_manifest_valid){
        /* The final payload is gone; its expected digest comes from the
         * deterministic fixture, never from the manifest being validated. */
        uint8_t b[4096],digest[32];mbedtls_sha256_context c;
        mbedtls_sha256_init(&c);mbedtls_sha256_starts(&c,0);
        for(size_t off=0;off<65536;off+=sizeof(b)){
            for(size_t i=0;i<sizeof(b);i++)b[i]=byte_at(0x700000u+cursor,off+i);
            mbedtls_sha256_update(&c,b,sizeof(b));
        }
        mbedtls_sha256_finish(&c,digest);mbedtls_sha256_free(&c);
        for(unsigned i=0;i<32;i++)sprintf(h+2*i,"%02x",digest[i]);
        h[64]=0;
        deleted_manifest_valid=manifest_valid(m,point,65536,0x700000u+cursor,h);
    }
    bool ok=point<=4?(!fe&&!me):(point==5?(fv&&!me):(point==6?(fv&&metadata_valid):point==7?deleted_manifest_valid:(!fe&&!me)));
    rec("RECOVERY","run=%08"PRIx32",point=%u,cycle=%u,part=%s,final=%s,manifest=%s,final_valid=%s,prior_valid=%s,status=%s",
        s_run,point,cycle,pe?"present":"absent",fe?"present":"absent",me?"present":"absent",fv?"yes":"no",prior?"yes":"no",ok&&prior?"pass":"fail");
    if(!ok||!prior)return false;
    if((pe&&unlink(p))||(fe&&unlink(f))||(me&&unlink(m)))return false;
    return put32("fault_active",0)&&put32("fault_cur",cursor+1);
}
static void fault_begin(unsigned cursor)
{
    unsigned point=cursor/FAULT_REPEATS,cycle=cursor%FAULT_REPEATS;char p[80],f[80],m[96];fault_paths(point,p,f,m);unlink(p);unlink(f);unlink(m);
    if(!put32("fault_active",1))fail("fault_arm","nvs");
    FILE*fp=fopen(p,"wb");if(!fp)fail("fault_open","io");
    size_t bytes=point==0?128:point==1?32768:point==2?65535:65536;
    if(!write_pattern(fp,bytes,0x700000u+cursor))fail("fault_write","io");
    if(point>=3&&(fflush(fp)||fsync(fileno(fp))))fail("fault_flush","io");
    if(point>=4&&fclose(fp))fail("fault_close","io");
    if(point>=5&&rename(p,f))fail("fault_rename","io");
    if(point>=6){char hash[65];if(!verify(f,65536,0x700000u+cursor,hash)||!manifest("fault",point,65536,0x700000u+cursor,hash))fail("fault_manifest","io");}
    if(point>=7&&unlink(f))fail("fault_delete","final_io");
    if(point>=8&&unlink(m))fail("fault_delete","manifest_io");
    rec("FAULT","run=%08"PRIx32",point=%u,cycle=%u,boundary=%s,mechanism=esp_restart,fault_model=software_reset_no_power_cut,bytes_written=%u,flushed=%s,closed=%s,renamed=%s,metadata_committed=%s,delete_attempted=%s",
        s_run,point,cycle,
        point==0?"after_buffered_128_before_flush":point==1?"after_buffered_half_before_flush":point==2?"after_buffered_65535_before_flush":point==3?"after_fsync_before_close":point==4?"after_close_before_rename":point==5?"after_rename_before_metadata":point==6?"after_metadata_before_delete":point==7?"after_final_delete_before_manifest_delete":"after_delete",
        (unsigned)bytes,point>=3?"yes":"no",point>=4?"yes":"no",point>=5?"yes":"no",point>=6?"yes":"no",point>=7?"yes":"no");
    monitor("fault","end");
    esp_restart();
}
static bool faults(void)
{
    unsigned c=get32("fault_cur",0);if(c>=FAULT_POINTS*FAULT_REPEATS)return true;
    if(get32("fault_active",0)){if(!fault_recover(c))return false;c++;if(c>=FAULT_POINTS*FAULT_REPEATS)return true;}
    fault_begin(c);return false;
}
static size_t rounded(size_t bytes){return ((bytes+ALLOC_UNIT-1u)/ALLOC_UNIT)*ALLOC_UNIT;}
static bool admits(uint64_t free_bytes,size_t bytes,size_t overhead)
{return free_bytes>=FLOOR+rounded(bytes)+FLOOR_MANIFEST+overhead;}
static bool fill_piece(unsigned index,size_t bytes)
{
    char p[80],f[80],h[65];snprintf(p,sizeof(p),FILL_ROOT"/%03u.part",index);snprintf(f,sizeof(f),FILL_ROOT"/%03u.bin",index);
    FILE*fp=fopen(p,"wb");if(!fp||!write_pattern(fp,bytes,0xF10000u+index)){if(fp)fclose(fp);return false;}
    if(!sync_close(fp)||rename(p,f))return false;
    return verify(f,bytes,0xF10000u+index,h);
}
static bool fill_create(void)
{
    uint64_t t,cal_before,cal_after,cal_reclaimed,f;if(!space(&t,&cal_before))return false;
    const char *cal=FILL_ROOT"/overhead-cal.bin";unlink(cal);
    if(!write_file(cal,PROBE_BYTES,0xF100ffu)||!space(&t,&cal_after)||cal_after>cal_before||unlink(cal)||!space(&t,&cal_reclaimed))return false;
    size_t allocated=(size_t)(cal_before-cal_after),measured=allocated>PROBE_BYTES?allocated-PROBE_BYTES:0;
    size_t overhead=measured>TX_METADATA_UNITS*ALLOC_UNIT?measured:TX_METADATA_UNITS*ALLOC_UNIT;
    size_t request_cost=rounded(FLOOR_CHUNK)+FLOOR_MANIFEST+overhead;
    if(cal_reclaimed<cal_before)return false;
    f=cal_reclaimed;size_t bytes=0;unsigned count=0;uint64_t min_after=f;
    rec("FLOOR","run=%08"PRIx32",event=derivation,largest_chunk=%u,temp_sibling=%u,manifest=%u,metadata=%u,recovery=%u,raw=%u,configured=%u,allocation_unit=%u,calibration_payload=%u,calibration_allocated=%u,measured_max_overhead=%u,guard_overhead=%u,request_cost=%u,status=pass",
        s_run,FLOOR_CHUNK,FLOOR_TEMP,FLOOR_MANIFEST,FLOOR_METADATA,FLOOR_RECOVERY,FLOOR_RAW,FLOOR,ALLOC_UNIT,PROBE_BYTES,(unsigned)allocated,(unsigned)measured,(unsigned)overhead,(unsigned)request_cost);
    while(admits(f,ALLOC_UNIT,overhead)){
        uint64_t budget=f-FLOOR-FLOOR_MANIFEST-overhead;
        size_t piece=budget>FILL_PIECE?FILL_PIECE:(size_t)(budget/ALLOC_UNIT)*ALLOC_UNIT;
        if(piece<ALLOC_UNIT)break;
        uint64_t before=f;if(!fill_piece(count,piece)||!space(&t,&f))return false;
        uint64_t used=before>=f?before-f:0;uint64_t extra=used>piece?used-piece:0;
        bool held=f>=FLOOR;
        rec("FLOOR","run=%08"PRIx32",event=fill,step=%u,configured=%u,free_before=%"PRIu64",payload=%u,estimated_cost=%u,allocated=%"PRIu64",overhead=%"PRIu64",free_after=%"PRIu64",floor_held=%s,status=%s",
            s_run,count,FLOOR,before,(unsigned)piece,(unsigned)(rounded(piece)+overhead),used,extra,f,held?"yes":"no",held?"pass":"fail");
        if (!held) return false;
        if (f < min_after) min_after = f;
        bytes += piece;
        count++;
    }
    if(!put32("fill_bytes",(uint32_t)bytes)||!put32("fill_count",count))return false;
    bool reject=!admits(f,FLOOR_CHUNK,overhead),prior=prior_commits_valid();
    bool bounded=f>=FLOOR&&f<FLOOR+request_cost;
    rec("FLOOR","run=%08"PRIx32",event=admission,configured=%u,free=%"PRIu64",post_write_min=%"PRIu64",fill_bytes=%u,files=%u,request=%u,request_cost=%u,decision=%s,prior_valid=%s,status=%s",
        s_run,FLOOR,f,min_after,(unsigned)bytes,count,FLOOR_CHUNK,(unsigned)request_cost,reject?"reject":"accept",prior?"yes":"no",reject&&bounded&&prior?"pass":"fail");return reject&&bounded&&prior;
}
static bool fill_validate(void)
{
    uint32_t bytes=get32("fill_bytes",0),count=get32("fill_count",0),actual=0;uint64_t t,before,after;bool ok=bytes&&count;
    for(unsigned i=0;i<count&&ok;i++){char f[80],h[65];struct stat st;snprintf(f,sizeof(f),FILL_ROOT"/%03u.bin",i);
        if(stat(f,&st)||st.st_size<=0||!verify(f,(size_t)st.st_size,0xF10000u+i,h))ok=false;else actual+=(uint32_t)st.st_size;}
    bool prior=prior_commits_valid();ok=ok&&actual==bytes&&prior;if(!space(&t,&before))return false;ok=ok&&before>=FLOOR;
    rec("FLOOR","run=%08"PRIx32",event=post_remount,configured=%u,expected=%u,actual=%u,files=%u,prior_valid=%s,floor_held=%s,status=%s,free=%"PRIu64,s_run,FLOOR,bytes,actual,count,prior?"yes":"no",before>=FLOOR?"yes":"no",ok?"pass":"fail",before);
    if(!ok)return false;
    for(unsigned i=0;i<count;i++){char f[80];snprintf(f,sizeof(f),FILL_ROOT"/%03u.bin",i);if(unlink(f))return false;}
    if(!space(&t,&after))return false;
    DIR*d=opendir(FILL_ROOT);if(!d)return false;struct dirent*de;unsigned residual=0;while((de=readdir(d)))if(strcmp(de->d_name,".")&&strcmp(de->d_name,".."))residual++;closedir(d);
    rec("FLOOR","run=%08"PRIx32",event=reclaim,configured=%u,free_before=%"PRIu64",free_after=%"PRIu64",reclaimed=%"PRIu64",residual_fill_files=%u,status=%s",s_run,FLOOR,before,after,after-before,residual,after>before&&!residual?"pass":"fail");
    return after>before&&!residual;
}
static void next(uint32_t phase){if(!put32("phase",phase))fail("phase","nvs");rec("PHASE","run=%08"PRIx32",next=%u,action=restart",s_run,phase);esp_restart();}

void app_main(void)
{
    rec("RUN_REQUEST", "epoch=%s,mode=%s,status=pass", EPOCH,
        H32_SENTINEL_ONLY ? "sentinel_only" : "experiment");
    s_emit_epoch = true;
    if(!profile_record())fail("profile","effective_config_mismatch");
    esp_err_t e=nvs_flash_init();
    if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND)fail("nvs_init","erase_required");
    nvs_handle_t sentinel_nvs;
    if(e!=ESP_OK||nvs_open("h32_pres",H32_SENTINEL_ONLY?NVS_READWRITE:NVS_READONLY,&sentinel_nvs)!=ESP_OK)
        fail("nvs_init","sentinel_open");
    uint32_t sentinel=0;bool created=false;
    esp_err_t se=nvs_get_u32(sentinel_nvs,"sentinel",&sentinel);
    if(H32_SENTINEL_ONLY&&se==ESP_ERR_NVS_NOT_FOUND){
        created=nvs_set_u32(sentinel_nvs,"sentinel",SENTINEL)==ESP_OK&&nvs_commit(sentinel_nvs)==ESP_OK;
        if(created) se=nvs_get_u32(sentinel_nvs,"sentinel",&sentinel);
    }
    rec("SENTINEL","mode=%s,value=%08"PRIx32",created=%s,status=%s",H32_SENTINEL_ONLY?"sentinel_only":"experiment",sentinel,created?"yes":"no",sentinel==SENTINEL?"pass":"fail");
    if(se!=ESP_OK||sentinel!=SENTINEL)fail("sentinel","invalid");
    if(H32_SENTINEL_ONLY){rec("SENTINEL_READY","mode=sentinel_only,storage_access=no,action=halt");while(true)vTaskDelay(portMAX_DELAY);}
    if(created)fail("sentinel","experiment_requires_existing");
    nvs_close(sentinel_nvs);
    esp_reset_reason_t reset_reason=esp_reset_reason();
    s_watchdog_events=reset_reason==ESP_RST_INT_WDT||reset_reason==ESP_RST_TASK_WDT||reset_reason==ESP_RST_WDT;
    if(s_watchdog_events)fail("watchdog","reset_observed");
    s_heartbeat_last=esp_timer_get_time();
    if(xTaskCreatePinnedToCore(heartbeat_task,"h32_heartbeat",3072,NULL,HEARTBEAT_PRIORITY,&s_heartbeat_task,HEARTBEAT_CORE)!=pdPASS)
        fail("heartbeat","task_create");
    monitor("boot","start");
    if(nvs_open("h32_run",NVS_READWRITE,&s_nvs)!=ESP_OK)fail("nvs_init","run_open");
#if !CONFIG_ESPTOOLPY_FLASHSIZE_16MB
#error "h32 requires 16 MiB flash (CONFIG_ESPTOOLPY_FLASHSIZE_16MB)"
#endif
    uint32_t flash_size=0;
    if(esp_flash_get_size(NULL,&flash_size)!=ESP_OK||flash_size!=16u*1024u*1024u)
        fail("hardware","flash_size");
    rec("HARDWARE","flash_bytes=%u,flash_id=%s,psram_bytes=%u,reset=%d,idf=%s",flash_size,"host_verified",
        (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),esp_reset_reason(),esp_get_idf_version());
    const esp_partition_t*outbox=NULL;if(!layout(&outbox))fail("partition","layout");
    monitor("mount","start");
    if(!mount_fs(outbox))fail("mount","unsafe_or_io");
    monitor("mount","end");
    uint32_t r=get32("remount",0);
    if(r==0){if(!probe_create())fail("remount_probe","create");}
    else if(r<=5) {
        uint32_t seen=get32("remount_seen",0),phase=get32("phase",0);
        bool initial=seen<r;
        if(!probe_verify(r,initial,phase))fail("remount_probe","verify");
        if(initial&&!put32("remount_seen",r))fail("remount_probe","nvs");
    }
    if(r<5){if(!put32("remount",r+1))fail("remount","nvs");rec("REBOOT","run=%08"PRIx32",cycle=%u,mechanism=esp_restart,status=scheduled",s_run,r+1);esp_restart();}
    uint32_t p=get32("phase",0);rec("PHASE","run=%08"PRIx32",current=%u,event=enter",s_run,p);
    if(p==0){monitor("io","start");if(!io_write())fail("io","measurement");if(!namespace_test())fail("namespace","semantics");monitor("io","end");next(1);}
    if(p==1){monitor("io_validate","start");if(!io_validate())fail("io","post_remount");monitor("io_validate","end");if(!cadence("opus",OPUS_RATE,0x510000u))fail("opus","cadence");next(2);}
    if(p==2){monitor("opus_validate","start");if(!cadence_validate("opus",OPUS_RATE,0x510000u))fail("opus","post_remount");monitor("opus_validate","end");if(!cadence("pcm",PCM_RATE,0x520000u))fail("pcm","cadence");next(3);}
    if(p==3){monitor("pcm_validate","start");if(!cadence_validate("pcm",PCM_RATE,0x520000u))fail("pcm","post_remount");monitor("pcm_validate","end");next(4);}
    if(p==4){monitor("fault","start");if(!faults())fail("fault","recovery");monitor("fault","end");next(5);}
    if(p==5){monitor("fill","start");if(!fill_create())fail("floor","admission");monitor("fill","end");next(6);}
    if(p==6){monitor("fill_validate","start");if(!fill_validate())fail("floor","post_remount");monitor("fill_validate","end");if(!put32("phase",7))fail("complete","nvs");
        rec("COMPLETE","run=%08"PRIx32",io_iterations=%u,cadence_seconds=%u,fault_cycles=%u,power_loss=unproven,safe_floor=%u",s_run,IO_ITERS,SECONDS,FAULT_POINTS*FAULT_REPEATS,FLOOR);}
    else if(p>=7)rec("COMPLETE","run=%08"PRIx32",event=already_complete,action=halt",s_run);
    while(true)vTaskDelay(portMAX_DELAY);
}
