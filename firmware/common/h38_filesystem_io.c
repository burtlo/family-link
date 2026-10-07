#include "h38_filesystem_io.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "mbedtls/sha256.h"

#define H38_IO_PROFILE "sdmmc_bounded_fat32_v1"
#define H38_IO_H35_REFERENCE "a1617be8cb2d2343e744c31cc7d1b933"
#define H38_IO_SECTOR_BYTES 512u
#define H38_IO_CARD_SECTORS UINT64_C(121503744)
#define H38_IO_VOLUME_START UINT64_C(32768)
#define H38_IO_VOLUME_SECTORS UINT64_C(1048576)
#define H38_IO_CLUSTER_BYTES UINT64_C(4096)
#define H38_IO_MAX_FILE_BYTES UINT64_C(196608)
#define H38_IO_MAX_GENERATED UINT64_C(33554432)
#define H38_IO_MAX_RETAINED UINT64_C(2097152)
#define H38_IO_MAX_MARKER 4096u
#define H38_IO_MAX_PATH 160u
#define H38_IO_BLOCK 4096u
#define H38_IO_MAX_MS UINT64_C(900000)
#define H38_IO_TX_MS UINT64_C(30000)
#define H38_IO_PATTERN_BASE UINT32_C(0x100000)
#define H38_IO_PROBE_SEED UINT32_C(0x32a55a23)
#define H38_IO_RENAME_TARGET_SEED UINT32_C(0x610001)
#define H38_IO_RENAME_SOURCE_SEED UINT32_C(0x610002)
#define H38_IO_STALE_SEED UINT32_C(0x620001)

static const uint32_t s_keep_indices[] = {0u, 10u, 19u, 20u, 30u, 39u};

typedef struct {
    const h38_filesystem_io_context_t *ctx;
    h38_filesystem_io_result_t *result;
    uint8_t *buffer;
    char root[H38_IO_MAX_PATH];
    uint64_t io_start_us;
} io_state_t;

typedef struct {
    h38_filesystem_io_stage_t stage;
    const char *failure_op;
    int32_t error;
    size_t written;
    size_t read;
    int fflush_ok;
    int fsync_ok;
    int close_ok;
    int rename_rc;
    int rename_errno;
    int checksum_match;
    int retained;
    int deleted;
    int64_t open_us;
    int64_t write_us;
    int64_t fflush_us;
    int64_t fsync_us;
    int64_t close_us;
    int64_t rename_us;
    int64_t read_verify_us;
    int64_t delete_us;
    uint64_t free_before;
    uint64_t free_after;
    char expected_sha[65];
    char actual_sha[65];
} transaction_t;

static int32_t saved_error(int fallback)
{
    int value = errno;
    return (int32_t)(value > 0 ? value : fallback);
}

const char *h38_filesystem_io_stage_name(h38_filesystem_io_stage_t stage)
{
    static const char *const names[] = {
        "none", "io_create", "io_write", "io_fflush", "io_fsync",
        "io_fclose", "io_rename", "io_readback", "io_checksum",
        "io_delete", "probe", "semantics", "remount", "reclaim",
        "budget", "timeout"
    };
    return (unsigned)stage < sizeof(names) / sizeof(names[0]) ? names[stage] : "semantics";
}

static bool valid_hex(const char *value, size_t length)
{
    if (!value || strlen(value) != length) return false;
    for (size_t i = 0; i < length; i++) {
        char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static bool valid_context(const h38_filesystem_io_context_t *ctx)
{
    return ctx && ctx->adapter && ctx->adapter->mounted && ctx->adapter->fs &&
           ctx->adapter->guard && ctx->adapter->guard->initialized &&
           ctx->adapter->guard->bound && !ctx->adapter->guard->failed &&
           ctx->adapter->guard->phase_active &&
           ctx->adapter->guard->phase == H38_GUARD_PHASE_IO &&
           ctx->adapter->guard->format_complete && ctx->adapter->guard->io_started &&
           ctx->adapter->guard->dma_buffer &&
           ctx->adapter->guard->dma_buffer_bytes == H38_GUARD_DMA_BYTES &&
           valid_hex(ctx->epoch, 32) && valid_hex(ctx->runtime_elf_sha256, 64) &&
           valid_hex(ctx->source_revision, 40) && valid_hex(ctx->intent_sha256, 64) &&
           valid_hex(ctx->h35_reference_epoch, 32) &&
           strcmp(ctx->h35_reference_epoch, H38_IO_H35_REFERENCE) == 0 &&
           valid_hex(ctx->private_cid_sha256, 64) && ctx->emit;
}

static h38_filesystem_io_stage_t map_guard_failure(const h38_disk_guard_t *guard);
static int32_t guard_failure_error(const h38_disk_guard_t *guard);

static bool set_failure(io_state_t *state, h38_filesystem_io_stage_t stage,
                        int32_t error)
{
    if (state->result->failure_stage == H38_IO_STAGE_NONE) {
        if (state->ctx->adapter->guard->failed) {
            stage = map_guard_failure(state->ctx->adapter->guard);
            error = guard_failure_error(state->ctx->adapter->guard);
        }
        state->result->failure_stage = stage;
        state->result->failure_error = error ? error : ESP_FAIL;
    }
    return false;
}

static h38_filesystem_io_stage_t map_guard_failure(const h38_disk_guard_t *guard)
{
    if (guard->failure == H38_GUARD_E_BUDGET) return H38_IO_STAGE_BUDGET;
    if (guard->failure == H38_GUARD_E_TIMEOUT) return H38_IO_STAGE_TIMEOUT;
    return H38_IO_STAGE_SEMANTICS;
}

static int32_t guard_failure_error(const h38_disk_guard_t *guard)
{
    return guard->failure == H38_GUARD_E_TIMEOUT ? ETIMEDOUT : ESP_ERR_INVALID_SIZE;
}

static bool check_limits(io_state_t *state, uint64_t transaction_start_us,
                         bool has_transaction, h38_filesystem_io_stage_t *stage,
                         int32_t *error)
{
    uint64_t now = (uint64_t)esp_timer_get_time();
    if (state->result->failure_stage != H38_IO_STAGE_NONE) {
        *stage = state->result->failure_stage;
        *error = state->result->failure_error;
        return false;
    }
    if (state->ctx->adapter->guard->failed) {
        *stage = map_guard_failure(state->ctx->adapter->guard);
        *error = guard_failure_error(state->ctx->adapter->guard);
        return set_failure(state, *stage, *error);
    }
    if (now < state->io_start_us || now - state->io_start_us > H38_IO_MAX_MS * 1000u) {
        *stage = H38_IO_STAGE_TIMEOUT;
        *error = ETIMEDOUT;
        return set_failure(state, *stage, *error);
    }
    if (has_transaction &&
        (now < transaction_start_us || now - transaction_start_us > H38_IO_TX_MS * 1000u)) {
        *stage = H38_IO_STAGE_TIMEOUT;
        *error = ETIMEDOUT;
        return set_failure(state, *stage, *error);
    }
    return true;
}

static bool check_now(io_state_t *state, uint64_t tx_start, bool in_tx,
                      h38_filesystem_io_stage_t *stage, int32_t *error)
{
    return check_limits(state, tx_start, in_tx, stage, error);
}

static bool path_join(char *out, size_t capacity, const char *root,
                      const char *name)
{
    int n;
    if (!out || !root || !name || strchr(name, '/') || strstr(name, "..")) return false;
    n = snprintf(out, capacity, "%s/%s", root, name);
    return n > 0 && (size_t)n < capacity;
}

static bool ends_with(const char *text, const char *suffix)
{
    size_t text_len = strlen(text), suffix_len = strlen(suffix);
    return text_len >= suffix_len &&
           memcmp(text + text_len - suffix_len, suffix, suffix_len) == 0;
}

static uint8_t byte_at(uint32_t seed, size_t at)
{
    uint32_t x = seed ^ (uint32_t)at * UINT32_C(2654435761);
    x ^= x >> 13;
    x *= UINT32_C(1274126177);
    x ^= x >> 16;
    return (uint8_t)x;
}

static bool digest_pattern(uint32_t seed, size_t bytes, char out[65])
{
    mbedtls_sha256_context sha;
    uint8_t digest[32];
    uint8_t block[H38_IO_BLOCK];
    size_t offset = 0;
    mbedtls_sha256_init(&sha);
    if (mbedtls_sha256_starts(&sha, 0) != 0) {
        mbedtls_sha256_free(&sha);
        return false;
    }
    while (offset < bytes) {
        size_t n = bytes - offset < sizeof(block) ? bytes - offset : sizeof(block);
        for (size_t i = 0; i < n; i++) block[i] = byte_at(seed, offset + i);
        if (mbedtls_sha256_update(&sha, block, n) != 0) {
            mbedtls_sha256_free(&sha);
            memset(block, 0, sizeof(block));
            return false;
        }
        offset += n;
    }
    if (mbedtls_sha256_finish(&sha, digest) != 0) {
        mbedtls_sha256_free(&sha);
        memset(block, 0, sizeof(block));
        memset(digest, 0, sizeof(digest));
        return false;
    }
    for (size_t i = 0; i < sizeof(digest); i++) snprintf(out + 2u * i, 3, "%02x", digest[i]);
    out[64] = '\0';
    mbedtls_sha256_free(&sha);
    memset(block, 0, sizeof(block));
    memset(digest, 0, sizeof(digest));
    return true;
}

static bool write_pattern(io_state_t *state, FILE *file, size_t bytes,
                          uint32_t seed, uint64_t tx_start,
                          size_t *written, h38_filesystem_io_stage_t *stage,
                          int32_t *error)
{
    *written = 0;
    for (size_t offset = 0; offset < bytes;) {
        size_t n = bytes - offset < H38_IO_BLOCK ? bytes - offset : H38_IO_BLOCK;
        for (size_t i = 0; i < n; i++) state->buffer[i] = byte_at(seed, offset + i);
        errno = 0;
        size_t actual = fwrite(state->buffer, 1, n, file);
        *written += actual;
        if (actual != n) {
            if (state->ctx->adapter->guard->failed) {
                *stage = map_guard_failure(state->ctx->adapter->guard);
                *error = guard_failure_error(state->ctx->adapter->guard);
            } else {
                *stage = H38_IO_STAGE_IO_WRITE;
                *error = saved_error(EIO);
            }
            return false;
        }
        offset += n;
        if (!check_now(state, tx_start, true, stage, error)) return false;
    }
    return true;
}

static bool verify_file(io_state_t *state, const char *path, size_t bytes,
                        uint32_t seed, uint64_t tx_start, size_t *read_bytes,
                        char actual_sha[65], h38_filesystem_io_stage_t *stage,
                        int32_t *error)
{
    FILE *file;
    mbedtls_sha256_context sha;
    uint8_t digest[32];
    size_t offset = 0;
    bool bytes_match = true;
    *read_bytes = 0;
    actual_sha[0] = '\0';
    errno = 0;
    file = fopen(path, "rb");
    if (!file) {
        *stage = H38_IO_STAGE_IO_READBACK;
        *error = saved_error(EIO);
        return false;
    }
    mbedtls_sha256_init(&sha);
    if (mbedtls_sha256_starts(&sha, 0) != 0) {
        mbedtls_sha256_free(&sha);
        (void)fclose(file);
        *stage = H38_IO_STAGE_IO_READBACK;
        *error = EIO;
        return false;
    }
    while (offset < bytes) {
        size_t n = bytes - offset < H38_IO_BLOCK ? bytes - offset : H38_IO_BLOCK;
        errno = 0;
        size_t got = fread(state->buffer, 1, n, file);
        *read_bytes += got;
        if (got != n) {
            if (state->ctx->adapter->guard->failed) {
                *stage = map_guard_failure(state->ctx->adapter->guard);
                *error = guard_failure_error(state->ctx->adapter->guard);
            } else {
                *stage = H38_IO_STAGE_IO_READBACK;
                *error = saved_error(EIO);
            }
            bytes_match = false;
            break;
        }
        for (size_t i = 0; i < n; i++) {
            if (state->buffer[i] != byte_at(seed, offset + i)) {
                bytes_match = false;
                break;
            }
        }
        if (mbedtls_sha256_update(&sha, state->buffer, n) != 0) {
            *stage = H38_IO_STAGE_IO_READBACK;
            *error = EIO;
            bytes_match = false;
            break;
        }
        offset += n;
        if (!check_now(state, tx_start, true, stage, error)) {
            bytes_match = false;
            break;
        }
        if (!bytes_match) break;
    }
    if (bytes_match) {
        errno = 0;
        int extra = fgetc(file);
        if (ferror(file)) {
            *stage = state->ctx->adapter->guard->failed ?
                map_guard_failure(state->ctx->adapter->guard) : H38_IO_STAGE_IO_READBACK;
            *error = state->ctx->adapter->guard->failed ?
                guard_failure_error(state->ctx->adapter->guard) : saved_error(EIO);
            bytes_match = false;
        } else if (extra != EOF) {
            bytes_match = false;
        }
    }
    if (mbedtls_sha256_finish(&sha, digest) == 0) {
        for (size_t i = 0; i < sizeof(digest); i++) snprintf(actual_sha + 2u * i, 3, "%02x", digest[i]);
        actual_sha[64] = '\0';
    } else {
        *stage = H38_IO_STAGE_IO_READBACK;
        *error = EIO;
        bytes_match = false;
    }
    mbedtls_sha256_free(&sha);
    memset(digest, 0, sizeof(digest));
    errno = 0;
    int close_result = fclose(file);
    if (close_result != 0 && *stage == H38_IO_STAGE_NONE) {
        *stage = H38_IO_STAGE_IO_READBACK;
        *error = saved_error(EIO);
        bytes_match = false;
    }
    if (!bytes_match && *stage == H38_IO_STAGE_NONE) {
        *stage = H38_IO_STAGE_IO_CHECKSUM;
        *error = EILSEQ;
    }
    if (offset != bytes && *stage == H38_IO_STAGE_NONE) {
        *stage = H38_IO_STAGE_IO_READBACK;
        *error = EIO;
    }
    return *stage == H38_IO_STAGE_NONE && bytes_match && offset == bytes;
}

static bool sample_space(io_state_t *state, uint64_t *total, uint64_t *free_bytes)
{
    if (esp_vfs_fat_info(state->root, total, free_bytes) != ESP_OK) return false;
    return *free_bytes <= *total;
}

static bool add_generated(io_state_t *state, uint64_t bytes, bool retained)
{
    if (bytes > H38_IO_MAX_GENERATED - state->result->generated_logical_bytes) {
        return set_failure(state, H38_IO_STAGE_BUDGET, ESP_ERR_INVALID_SIZE);
    }
    if (retained && bytes > H38_IO_MAX_RETAINED - state->result->retained_file_bytes) {
        return set_failure(state, H38_IO_STAGE_BUDGET, ESP_ERR_INVALID_SIZE);
    }
    state->result->generated_logical_bytes += bytes;
    if (retained) state->result->retained_file_bytes += bytes;
    return true;
}

static void emit_io_result(io_state_t *state, uint32_t index, uint32_t size,
                           uint32_t seed, const transaction_t *tx)
{
    char fields[1800];
    int n = snprintf(fields, sizeof(fields),
        "index=%" PRIu32 ",size_bytes=%" PRIu32 ",seed=%" PRIu32
        ",expected_sha256=%s,actual_sha256=%s,payload_write_bytes=%u,payload_read_bytes=%u,total_bytes=%u"
        ",free_before=%" PRIu64 ",free_after=%" PRIu64
        ",fflush_ok=%d,fsync_ok=%d,close_ok=%d,rename_rc=%d,rename_errno=%d"
        ",checksum_match=%d,retained=%d,deleted=%d,failure_op=%s"
        ",open_us=%" PRId64 ",write_us=%" PRId64 ",fflush_us=%" PRId64
        ",fsync_us=%" PRId64 ",close_us=%" PRId64 ",rename_us=%" PRId64
        ",read_verify_us=%" PRId64 ",delete_us=%" PRId64 ",status=%s,error=%" PRId32,
        index, size, seed, tx->expected_sha, tx->actual_sha,
        (unsigned)tx->written, (unsigned)tx->read,
        (unsigned)(tx->written + tx->read), tx->free_before, tx->free_after,
        tx->fflush_ok, tx->fsync_ok, tx->close_ok, tx->rename_rc,
        tx->rename_errno, tx->checksum_match, tx->retained, tx->deleted,
        tx->failure_op ? tx->failure_op : "none", tx->open_us, tx->write_us,
        tx->fflush_us, tx->fsync_us, tx->close_us, tx->rename_us,
        tx->read_verify_us, tx->delete_us,
        tx->stage == H38_IO_STAGE_NONE ? "ok" : "failed", tx->error);
    if (n < 0 || (size_t)n >= sizeof(fields)) {
        (void)set_failure(state, H38_IO_STAGE_SEMANTICS, ESP_ERR_INVALID_SIZE);
        return;
    }
    state->ctx->emit(state->ctx->emit_context, "IO_RESULT", fields);
}

static bool matrix_transaction(io_state_t *state, uint32_t index,
                               uint32_t bytes, uint32_t seed, bool retain)
{
    char part_name[40], final_name[40], part_path[H38_IO_MAX_PATH], final_path[H38_IO_MAX_PATH];
    transaction_t tx;
    h38_filesystem_io_stage_t deadline_stage = H38_IO_STAGE_NONE;
    int32_t deadline_error = 0;
    uint64_t tx_start = (uint64_t)esp_timer_get_time(), total_space = 0;
    struct stat st;
    FILE *file = NULL;
    memset(&tx, 0, sizeof(tx));
    tx.failure_op = "none";
    tx.retained = retain ? 1 : 0;
    tx.deleted = 0;
    tx.stage = H38_IO_STAGE_NONE;
    memset(tx.expected_sha, '0', 64); tx.expected_sha[64] = '\0';
    memset(tx.actual_sha, '0', 64); tx.actual_sha[64] = '\0';
    (void)snprintf(part_name, sizeof(part_name), "matrix-%02" PRIu32 ".part", index);
    (void)snprintf(final_name, sizeof(final_name), "matrix-%02" PRIu32 ".bin", index);
    if (!path_join(part_path, sizeof(part_path), state->root, part_name) ||
        !path_join(final_path, sizeof(final_path), state->root, final_name)) {
        tx.stage = H38_IO_STAGE_IO_CREATE; tx.failure_op = "create"; tx.error = ENAMETOOLONG;
        goto done;
    }
    errno = 0;
    if (stat(part_path, &st) == 0 || errno != ENOENT) {
        tx.stage = H38_IO_STAGE_IO_CREATE; tx.failure_op = "create"; tx.error = EEXIST;
        goto done;
    }
    errno = 0;
    if (stat(final_path, &st) == 0 || errno != ENOENT) {
        tx.stage = H38_IO_STAGE_IO_CREATE; tx.failure_op = "create"; tx.error = EEXIST;
        goto done;
    }
    if (bytes > H38_IO_MAX_FILE_BYTES ||
        state->result->generated_logical_bytes > H38_IO_MAX_GENERATED - bytes) {
        tx.stage = H38_IO_STAGE_BUDGET; tx.error = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    if (!digest_pattern(seed, bytes, tx.expected_sha)) {
        tx.stage = H38_IO_STAGE_SEMANTICS; tx.error = EIO;
        goto done;
    }
    if (!sample_space(state, &total_space, &tx.free_before)) {
        tx.stage = H38_IO_STAGE_SEMANTICS; tx.failure_op = "free_space"; tx.error = saved_error(EIO);
        goto done;
    }
    int64_t open_start = esp_timer_get_time();
    errno = 0;
    int fd = open(part_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    tx.open_us = esp_timer_get_time() - open_start;
    if (fd < 0) {
        tx.stage = H38_IO_STAGE_IO_CREATE; tx.failure_op = "create"; tx.error = saved_error(EIO);
        goto done;
    }
    file = fdopen(fd, "wb");
    if (!file) {
        tx.stage = H38_IO_STAGE_IO_CREATE; tx.failure_op = "create"; tx.error = saved_error(EIO);
        (void)close(fd);
        file = NULL;
        goto done;
    }
    if (!check_now(state, tx_start, true, &deadline_stage, &deadline_error)) {
        tx.stage = deadline_stage; tx.error = deadline_error;
        goto close_writer;
    }
    {
        int64_t started = esp_timer_get_time();
        bool ok = write_pattern(state, file, bytes, seed, tx_start, &tx.written,
                                &deadline_stage, &deadline_error);
        tx.write_us = esp_timer_get_time() - started;
        if (!ok) {
            tx.stage = deadline_stage != H38_IO_STAGE_NONE ? deadline_stage : H38_IO_STAGE_IO_WRITE;
            tx.failure_op = (tx.stage == H38_IO_STAGE_IO_WRITE) ? "write" : "none";
            tx.error = deadline_error ? deadline_error : EIO;
            goto close_writer;
        }
    }
    {
        int64_t started = esp_timer_get_time();
        errno = 0;
        int ret = fflush(file);
        tx.fflush_us = esp_timer_get_time() - started;
        tx.fflush_ok = ret == 0;
        if (ret != 0) {
            tx.stage = H38_IO_STAGE_IO_FFLUSH; tx.failure_op = "fflush"; tx.error = saved_error(EIO);
            goto close_writer;
        }
        if (!check_now(state, tx_start, true, &deadline_stage, &deadline_error)) {
            tx.stage = deadline_stage; tx.error = deadline_error;
            goto close_writer;
        }
    }
    {
        int64_t started = esp_timer_get_time();
        errno = 0;
        int ret = fsync(fileno(file));
        tx.fsync_us = esp_timer_get_time() - started;
        tx.fsync_ok = ret == 0;
        if (ret != 0) {
            tx.stage = H38_IO_STAGE_IO_FSYNC; tx.failure_op = "fsync"; tx.error = saved_error(EIO);
            goto close_writer;
        }
        if (!check_now(state, tx_start, true, &deadline_stage, &deadline_error)) {
            tx.stage = deadline_stage; tx.error = deadline_error;
            goto close_writer;
        }
    }
close_writer:
    if (file) {
        int64_t started = esp_timer_get_time();
        errno = 0;
        int ret = fclose(file);
        tx.close_us = esp_timer_get_time() - started;
        tx.close_ok = ret == 0;
        file = NULL;
        if (ret != 0 && tx.stage == H38_IO_STAGE_NONE) {
            tx.stage = H38_IO_STAGE_IO_FCLOSE; tx.failure_op = "fclose"; tx.error = saved_error(EIO);
        }
    }
    if (tx.stage != H38_IO_STAGE_NONE) goto done;
    {
        int64_t started = esp_timer_get_time();
        errno = 0;
        tx.rename_rc = rename(part_path, final_path);
        tx.rename_errno = tx.rename_rc == 0 ? 0 : saved_error(EIO);
        tx.rename_us = esp_timer_get_time() - started;
        if (tx.rename_rc != 0) {
            tx.stage = H38_IO_STAGE_IO_RENAME; tx.failure_op = "rename"; tx.error = tx.rename_errno;
            goto done;
        }
        if (!check_now(state, tx_start, true, &deadline_stage, &deadline_error)) {
            tx.stage = deadline_stage; tx.error = deadline_error;
            goto done;
        }
    }
    {
        int64_t started = esp_timer_get_time();
        h38_filesystem_io_stage_t verify_stage = H38_IO_STAGE_NONE;
        int32_t verify_error = 0;
        bool ok = verify_file(state, final_path, bytes, seed, tx_start, &tx.read,
                              tx.actual_sha, &verify_stage, &verify_error);
        tx.read_verify_us = esp_timer_get_time() - started;
        tx.checksum_match = ok ? 1 : 0;
        if (!ok) {
            tx.stage = verify_stage != H38_IO_STAGE_NONE ? verify_stage : H38_IO_STAGE_IO_READBACK;
            tx.failure_op = tx.stage == H38_IO_STAGE_IO_CHECKSUM ? "checksum" : "readback";
            tx.error = verify_error ? verify_error : EIO;
            goto done;
        }
    }
    if (!check_now(state, tx_start, true, &deadline_stage, &deadline_error)) {
        tx.stage = deadline_stage; tx.failure_op = "none"; tx.error = deadline_error;
        goto done;
    }
    if (retain) {
        if (!add_generated(state, bytes, true)) {
            tx.stage = H38_IO_STAGE_BUDGET; tx.error = ESP_ERR_INVALID_SIZE;
            goto done;
        }
    } else {
        int64_t started = esp_timer_get_time();
        errno = 0;
        int ret = unlink(final_path);
        tx.delete_us = esp_timer_get_time() - started;
        if (ret != 0) {
            tx.stage = H38_IO_STAGE_IO_DELETE; tx.failure_op = "delete"; tx.error = saved_error(EIO);
            goto done;
        }
        tx.deleted = 1;
        if (!check_now(state, tx_start, true, &deadline_stage, &deadline_error)) {
            tx.stage = deadline_stage; tx.failure_op = "none"; tx.error = deadline_error;
            goto done;
        }
        if (bytes > H38_IO_MAX_GENERATED - state->result->generated_logical_bytes) {
            tx.stage = H38_IO_STAGE_BUDGET; tx.error = ESP_ERR_INVALID_SIZE;
            goto done;
        }
        state->result->generated_logical_bytes += bytes;
    }
    if (!sample_space(state, &total_space, &tx.free_after)) {
        tx.stage = H38_IO_STAGE_SEMANTICS; tx.failure_op = "free_space"; tx.error = saved_error(EIO);
        goto done;
    }
done:
    if (state->ctx->adapter->guard->failed) {
        tx.stage = map_guard_failure(state->ctx->adapter->guard);
        tx.failure_op = "none";
        tx.error = guard_failure_error(state->ctx->adapter->guard);
    }
    if (tx.stage != H38_IO_STAGE_NONE && state->result->failure_stage == H38_IO_STAGE_NONE)
        (void)set_failure(state, tx.stage, tx.error);
    emit_io_result(state, index, bytes, seed, &tx);
    state->result->io_file_count++;
    if (tx.written > UINT64_MAX - state->result->write_bytes ||
        tx.read > UINT64_MAX - state->result->read_bytes) {
        (void)set_failure(state, H38_IO_STAGE_BUDGET, EOVERFLOW);
        return false;
    }
    /* Guard counters below are authoritative; these totals describe payload only. */
    state->result->write_bytes += tx.written;
    state->result->read_bytes += tx.read;
    return tx.stage == H38_IO_STAGE_NONE &&
           state->result->failure_stage == H38_IO_STAGE_NONE;
}

static bool create_owned_file(io_state_t *state, const char *name, uint32_t bytes,
                              uint32_t seed, bool retained, bool extension,
                              char hash[65], h38_filesystem_io_stage_t on_failure)
{
    char final_path[H38_IO_MAX_PATH], part_name[H38_IO_MAX_PATH], part_path[H38_IO_MAX_PATH];
    uint64_t tx_start = (uint64_t)esp_timer_get_time();
    size_t written = 0, read = 0;
    FILE *file = NULL;
    int fd = -1;
    struct stat st;
    h38_filesystem_io_stage_t stage = H38_IO_STAGE_NONE, deadline_stage = H38_IO_STAGE_NONE;
    int32_t error = 0, deadline_error = 0;
    bool renamed = false;
    if (!path_join(final_path, sizeof(final_path), state->root, name)) {
        stage = on_failure; error = ENAMETOOLONG; goto failed;
    }
    if (extension) {
        int n = snprintf(part_name, sizeof(part_name), "%s.part", name);
        if (n < 0 || (size_t)n >= sizeof(part_name) ||
            !path_join(part_path, sizeof(part_path), state->root, part_name)) {
            stage = on_failure; error = ENAMETOOLONG; goto failed;
        }
    } else {
        if (strlen(name) >= sizeof(part_name)) { stage = on_failure; error = ENAMETOOLONG; goto failed; }
        (void)strcpy(part_name, name);
        (void)strcpy(part_path, final_path);
    }
    errno = 0;
    if (stat(final_path, &st) == 0 || errno != ENOENT) { stage = on_failure; error = EEXIST; goto failed; }
    if (extension) {
        errno = 0;
        if (stat(part_path, &st) == 0 || errno != ENOENT) { stage = on_failure; error = EEXIST; goto failed; }
    }
    if (bytes > H38_IO_MAX_FILE_BYTES || bytes > H38_IO_MAX_GENERATED - state->result->generated_logical_bytes ||
        (retained && bytes > H38_IO_MAX_RETAINED - state->result->retained_file_bytes)) {
        stage = H38_IO_STAGE_BUDGET; error = ESP_ERR_INVALID_SIZE; goto failed;
    }
    if (!digest_pattern(seed, bytes, hash)) { stage = H38_IO_STAGE_SEMANTICS; error = EIO; goto failed; }
    errno = 0;
    fd = open(part_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { stage = on_failure; error = saved_error(EIO); goto failed; }
    file = fdopen(fd, "wb");
    if (!file) { stage = on_failure; error = saved_error(EIO); (void)close(fd); fd = -1; goto failed; }
    fd = -1;
    if (!write_pattern(state, file, bytes, seed, tx_start, &written, &deadline_stage, &deadline_error)) {
        stage = deadline_stage != H38_IO_STAGE_NONE ? deadline_stage : on_failure;
        error = deadline_error ? deadline_error : EIO;
        goto close_file;
    }
    errno = 0;
    if (fflush(file) != 0) { stage = on_failure; error = saved_error(EIO); goto close_file; }
    if (!check_now(state, tx_start, true, &deadline_stage, &deadline_error)) {
        stage = deadline_stage; error = deadline_error; goto close_file;
    }
    errno = 0;
    if (fsync(fileno(file)) != 0) { stage = on_failure; error = saved_error(EIO); goto close_file; }
    if (!check_now(state, tx_start, true, &deadline_stage, &deadline_error)) {
        stage = deadline_stage; error = deadline_error; goto close_file;
    }
close_file:
    if (file) {
        errno = 0;
        if (fclose(file) != 0 && stage == H38_IO_STAGE_NONE) { stage = on_failure; error = saved_error(EIO); }
        file = NULL;
    }
    if (fd >= 0) { (void)close(fd); fd = -1; }
    if (stage != H38_IO_STAGE_NONE) goto failed;
    if (extension) {
        errno = 0;
        if (rename(part_path, final_path) != 0) { stage = on_failure; error = saved_error(EIO); goto failed; }
        renamed = true;
    } else renamed = true;
    if (!check_now(state, tx_start, true, &deadline_stage, &deadline_error)) {
        stage = deadline_stage; error = deadline_error; goto failed;
    }
    if (!verify_file(state, final_path, bytes, seed, tx_start, &read, hash,
                     &stage, &error)) {
        if (stage == H38_IO_STAGE_IO_CHECKSUM) stage = on_failure;
        goto failed;
    }
    if (!check_now(state, tx_start, true, &deadline_stage, &deadline_error)) {
        stage = deadline_stage; error = deadline_error; goto failed;
    }
    state->result->generated_logical_bytes += bytes;
    if (retained) state->result->retained_file_bytes += bytes;
    return true;
failed:
    if (file) (void)fclose(file);
    if (fd >= 0) (void)close(fd);
    if (stage == H38_IO_STAGE_NONE) stage = on_failure;
    (void)set_failure(state, stage, error ? error : EIO);
    (void)renamed;
    return false;
}

static bool marker_contents(io_state_t *state, char *out, size_t capacity,
                            size_t *length)
{
    int n = snprintf(out, capacity,
        "schema=h38-marker-v1\nepoch=%s\nruntime_elf_sha256=%s\nsource_revision=%s\n"
        "intent_sha256=%s\nh35_reference_epoch=%s\nprivate_cid_sha256=%s\n"
        "profile=%s\ncard_sector_bytes=512\ncard_sector_count=%" PRIu64 "\n"
        "volume_start_lba=%" PRIu64 "\nvolume_sector_count=%" PRIu64 "\n"
        "sector_bytes=512\nfat_type=FAT32\nfat_count=2\nallocation_unit_bytes=4096\n",
        state->ctx->epoch, state->ctx->runtime_elf_sha256,
        state->ctx->source_revision, state->ctx->intent_sha256,
        state->ctx->h35_reference_epoch, state->ctx->private_cid_sha256,
        H38_IO_PROFILE, H38_IO_CARD_SECTORS, H38_IO_VOLUME_START,
        H38_IO_VOLUME_SECTORS);
    if (n < 0 || (size_t)n >= capacity || (unsigned)n > H38_IO_MAX_MARKER) return false;
    *length = (size_t)n;
    return true;
}

static bool write_marker(io_state_t *state)
{
    char content[512], part[H38_IO_MAX_PATH], final[H38_IO_MAX_PATH];
    size_t length = 0;
    FILE *file = NULL;
    int fd = -1;
    struct stat st;
    uint64_t start = (uint64_t)esp_timer_get_time();
    h38_filesystem_io_stage_t stage = H38_IO_STAGE_NONE, deadline_stage = H38_IO_STAGE_NONE;
    int32_t error = 0, deadline_error = 0;
    if (!marker_contents(state, content, sizeof(content), &length) || length > H38_IO_MAX_MARKER ||
        !path_join(part, sizeof(part), state->root, "marker.txt.part") ||
        !path_join(final, sizeof(final), state->root, "marker.txt")) {
        (void)set_failure(state, H38_IO_STAGE_SEMANTICS, ESP_ERR_INVALID_SIZE);
        return false;
    }
    if (length > H38_IO_MAX_GENERATED || length > H38_IO_MAX_RETAINED) {
        (void)set_failure(state, H38_IO_STAGE_BUDGET, ESP_ERR_INVALID_SIZE);
        return false;
    }
    errno = 0;
    if (stat(part, &st) == 0 || errno != ENOENT || stat(final, &st) == 0 || errno != ENOENT) {
        (void)set_failure(state, H38_IO_STAGE_IO_CREATE, EEXIST);
        return false;
    }
    errno = 0;
    fd = open(part, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { (void)set_failure(state, H38_IO_STAGE_IO_CREATE, saved_error(EIO)); return false; }
    file = fdopen(fd, "wb");
    if (!file) { (void)set_failure(state, H38_IO_STAGE_IO_CREATE, saved_error(EIO)); (void)close(fd); return false; }
    fd = -1;
    errno = 0;
    if (fwrite(content, 1, length, file) != length) {
        stage = H38_IO_STAGE_IO_WRITE; error = saved_error(EIO);
    } else if (fflush(file) != 0) {
        stage = H38_IO_STAGE_IO_FFLUSH; error = saved_error(EIO);
    } else if (!check_now(state, start, true, &deadline_stage, &deadline_error)) {
        stage = deadline_stage; error = deadline_error;
    } else if (fsync(fileno(file)) != 0) {
        stage = H38_IO_STAGE_IO_FSYNC; error = saved_error(EIO);
    } else if (!check_now(state, start, true, &deadline_stage, &deadline_error)) {
        stage = deadline_stage; error = deadline_error;
    }
    errno = 0;
    if (fclose(file) != 0 && stage == H38_IO_STAGE_NONE) {
        stage = H38_IO_STAGE_IO_FCLOSE; error = saved_error(EIO);
    }
    if (stage != H38_IO_STAGE_NONE) {
        (void)set_failure(state, stage, error ? error : EIO);
        return false;
    }
    errno = 0;
    if (rename(part, final) != 0) {
        (void)set_failure(state, H38_IO_STAGE_IO_RENAME, saved_error(EIO));
        return false;
    }
    state->result->generated_logical_bytes += length;
    state->result->retained_file_bytes += length;
    return true;
}

static bool verify_marker(io_state_t *state)
{
    char path[H38_IO_MAX_PATH], expected[512], actual[512];
    size_t expected_length = 0;
    uint64_t tx_start = (uint64_t)esp_timer_get_time();
    h38_filesystem_io_stage_t stage = H38_IO_STAGE_NONE;
    int32_t error = 0;
    if (!path_join(path, sizeof(path), state->root, "marker.txt") ||
        !marker_contents(state, expected, sizeof(expected), &expected_length)) {
        return set_failure(state, H38_IO_STAGE_SEMANTICS, ESP_ERR_INVALID_SIZE);
    }
    FILE *file = fopen(path, "rb");
    if (!file) return set_failure(state, H38_IO_STAGE_SEMANTICS, saved_error(EIO));
    size_t got = fread(actual, 1, sizeof(actual), file);
    int extra = fgetc(file);
    bool ok = got == expected_length && extra == EOF && !ferror(file) &&
              memcmp(actual, expected, expected_length) == 0;
    errno = 0;
    if (fclose(file) != 0) ok = false;
    if (!check_now(state, tx_start, true, &stage, &error)) return false;
    if (!ok) return set_failure(state, H38_IO_STAGE_SEMANTICS, EILSEQ);
    memset(actual, 0, sizeof(actual));
    return true;
}

static bool create_epoch_root(io_state_t *state)
{
    const char *base = h38_fatfs_base_path(state->ctx->adapter);
    struct stat st;
    DIR *dir;
    struct dirent *entry;
    int n;
    if (!base || stat(base, &st) != 0 || !S_ISDIR(st.st_mode))
        return set_failure(state, H38_IO_STAGE_SEMANTICS, saved_error(ENOENT));
    n = snprintf(state->root, sizeof(state->root), "%s/%s", base, state->ctx->epoch);
    if (n < 0 || (size_t)n >= sizeof(state->root))
        return set_failure(state, H38_IO_STAGE_SEMANTICS, ENAMETOOLONG);
    errno = 0;
    dir = opendir(base);
    if (!dir) return set_failure(state, H38_IO_STAGE_SEMANTICS, saved_error(EIO));
    bool collision = false;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, state->ctx->epoch) == 0) { collision = true; break; }
    }
    closedir(dir);
    if (collision) return set_failure(state, H38_IO_STAGE_SEMANTICS, EEXIST);
    errno = 0;
    if (mkdir(state->root, 0700) != 0)
        return set_failure(state, H38_IO_STAGE_SEMANTICS, saved_error(EIO));
    dir = opendir(state->root);
    if (!dir) return set_failure(state, H38_IO_STAGE_SEMANTICS, saved_error(EIO));
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) collision = true;
    }
    closedir(dir);
    if (collision) return set_failure(state, H38_IO_STAGE_SEMANTICS, EEXIST);
    return true;
}

typedef struct {
    int32_t rename_rc;
    int32_t rename_errno;
    bool target_valid;
    bool source_present;
    bool old_target_preserved;
    char target_sha[65];
    char source_sha[65];
} rename_result_t;

static bool verify_named(io_state_t *state, const char *name, uint32_t bytes,
                         uint32_t seed, char hash[65],
                         h38_filesystem_io_stage_t on_failure)
{
    char path[H38_IO_MAX_PATH];
    size_t read = 0;
    uint64_t tx_start = (uint64_t)esp_timer_get_time();
    h38_filesystem_io_stage_t stage = H38_IO_STAGE_NONE;
    int32_t error = 0;
    if (!path_join(path, sizeof(path), state->root, name))
        return set_failure(state, on_failure, ENAMETOOLONG);
    if (!verify_file(state, path, bytes, seed, tx_start, &read, hash, &stage, &error))
        return set_failure(state, stage == H38_IO_STAGE_NONE ? on_failure : stage,
                           error ? error : EIO);
    if (!check_now(state, tx_start, true, &stage, &error)) return false;
    return true;
}

static bool emit_probe_result(io_state_t *state, uint32_t cycle)
{
    char path[H38_IO_MAX_PATH], hash[65], fields[256];
    size_t read = 0;
    int64_t start = esp_timer_get_time();
    h38_filesystem_io_stage_t stage = H38_IO_STAGE_NONE;
    int32_t error = 0;
    bool ok = path_join(path, sizeof(path), state->root, "probe.bin") &&
              verify_file(state, path, 65536u, H38_IO_PROBE_SEED,
                          (uint64_t)start, &read, hash, &stage, &error);
    if (ok && !check_now(state, (uint64_t)start, true, &stage, &error)) ok = false;
    if (!ok) {
        if (stage == H38_IO_STAGE_NONE) { stage = H38_IO_STAGE_PROBE; error = EILSEQ; }
        if (stage != H38_IO_STAGE_TIMEOUT && stage != H38_IO_STAGE_BUDGET)
            stage = H38_IO_STAGE_PROBE;
        (void)set_failure(state, stage, error ? error : EIO);
    }
    int64_t elapsed = esp_timer_get_time() - start;
    int n = snprintf(fields, sizeof(fields),
        "cycle=%" PRIu32 ",size_bytes=65536,seed=%" PRIu32 ",verified=%u,elapsed_us=%" PRId64 ",status=%s,error=%" PRId32,
        cycle, H38_IO_PROBE_SEED, ok ? 1u : 0u, elapsed,
        ok ? "ok" : "failed", ok ? 0 : (error ? error : EIO));
    if (n < 0 || (size_t)n >= sizeof(fields))
        return set_failure(state, H38_IO_STAGE_SEMANTICS, ESP_ERR_INVALID_SIZE);
    state->ctx->emit(state->ctx->emit_context, "PROBE_RESULT", fields);
    state->result->probe_count++;
    return ok;
}

static bool emit_retained_result(io_state_t *state, uint32_t cycle,
                                 uint32_t index)
{
    uint32_t size = index < 20u ? 65536u : 196608u;
    uint32_t iteration = index < 20u ? index : index - 20u;
    uint32_t seed = H38_IO_PATTERN_BASE + size + iteration;
    char name[40], path[H38_IO_MAX_PATH], expected[65], actual[65], fields[512];
    size_t read = 0;
    uint64_t start = (uint64_t)esp_timer_get_time();
    h38_filesystem_io_stage_t stage = H38_IO_STAGE_NONE;
    int32_t error = 0;
    memset(expected, '0', 64); expected[64] = '\0';
    memset(actual, '0', 64); actual[64] = '\0';
    (void)snprintf(name, sizeof(name), "matrix-%02" PRIu32 ".bin", index);
    bool ok = digest_pattern(seed, size, expected) &&
              path_join(path, sizeof(path), state->root, name) &&
              verify_file(state, path, size, seed, start, &read, actual, &stage, &error);
    if (ok && !check_now(state, start, true, &stage, &error)) ok = false;
    if (!ok) {
        if (stage == H38_IO_STAGE_NONE) { stage = H38_IO_STAGE_IO_CHECKSUM; error = EILSEQ; }
        (void)set_failure(state, stage, error ? error : EIO);
    }
    int n = snprintf(fields, sizeof(fields),
        "cycle=%" PRIu32 ",index=%" PRIu32 ",size_bytes=%" PRIu32 ",seed=%" PRIu32
        ",expected_sha256=%s,actual_sha256=%s,checksum_match=%u,status=%s,error=%" PRId32,
        cycle, index, size, seed, expected, actual, ok ? 1u : 0u,
        ok ? "ok" : "failed", ok ? 0 : (error ? error : EIO));
    if (n < 0 || (size_t)n >= sizeof(fields))
        return set_failure(state, H38_IO_STAGE_SEMANTICS, ESP_ERR_INVALID_SIZE);
    state->ctx->emit(state->ctx->emit_context, "RETAINED_RESULT", fields);
    state->result->retained_check_count++;
    return ok;
}

static bool mount_info_valid(const h38_fatfs_mount_info_t *info)
{
    return info && info->fs_type == FS_FAT32 &&
           info->sector_bytes == H38_IO_SECTOR_BYTES &&
           info->volume_sectors == H38_IO_VOLUME_SECTORS &&
           info->allocation_unit_bytes == H38_IO_CLUSTER_BYTES &&
           info->cluster_count == 130811u &&
           info->total_bytes == (uint64_t)info->cluster_count * info->allocation_unit_bytes &&
           info->free_bytes <= info->total_bytes;
}

static bool emit_mount_result(io_state_t *state, uint32_t cycle,
                              const h38_fatfs_mount_info_t *info,
                              uint64_t read_bytes, uint64_t write_bytes,
                              int64_t elapsed)
{
    char fields[512];
    int n = snprintf(fields, sizeof(fields),
        "kind=remount,cycle=%" PRIu32 ",mounted=1,fs_type=%" PRIu32
        ",sector_bytes=%" PRIu32 ",volume_sectors=%" PRIu32
        ",allocation_unit_bytes=%" PRIu32 ",cluster_count=%" PRIu32
        ",total_bytes=%" PRIu64 ",free_bytes=%" PRIu64
        ",read_bytes=%" PRIu64 ",write_bytes=%" PRIu64
        ",elapsed_us=%" PRId64 ",status=ok,error=0",
        cycle, info->fs_type, info->sector_bytes, info->volume_sectors,
        info->allocation_unit_bytes, info->cluster_count, info->total_bytes,
        info->free_bytes, read_bytes, write_bytes, elapsed);
    if (n < 0 || (size_t)n >= sizeof(fields))
        return set_failure(state, H38_IO_STAGE_SEMANTICS, ESP_ERR_INVALID_SIZE);
    state->ctx->emit(state->ctx->emit_context, "MOUNT_RESULT", fields);
    state->result->mount_count++;
    state->result->remount_count++;
    return true;
}
static bool emit_failed_remount(io_state_t *state, uint32_t cycle,
                                uint64_t read_bytes, uint64_t write_bytes,
                                int64_t elapsed, int32_t error)
{
    char fields[512];
    if (state->ctx->adapter->guard->failed)
        error = guard_failure_error(state->ctx->adapter->guard);
    int n = snprintf(fields, sizeof(fields),
        "kind=remount,cycle=%" PRIu32 ",mounted=%u,fs_type=0,sector_bytes=512"
        ",volume_sectors=1048576,allocation_unit_bytes=0,cluster_count=0"
        ",total_bytes=0,free_bytes=0,read_bytes=%" PRIu64
        ",write_bytes=%" PRIu64 ",elapsed_us=%" PRId64
        ",status=failed,error=%" PRId32,
        cycle, (unsigned)state->ctx->adapter->mounted,
        read_bytes, write_bytes, elapsed, error);
    if (n < 0 || (size_t)n >= sizeof(fields))
        return set_failure(state, H38_IO_STAGE_REMOUNT, ESP_ERR_INVALID_SIZE);
    state->ctx->emit(state->ctx->emit_context, "MOUNT_RESULT", fields);
    return set_failure(state, H38_IO_STAGE_REMOUNT, error);
}

static bool remount_cycles(io_state_t *state)
{
    const h38_filesystem_io_context_t *ctx = state->ctx;
    for (uint32_t cycle = 0; cycle < 5u; cycle++) {
        h38_fatfs_counts_t before = {0}, after = {0};
        h38_fatfs_mount_info_t info = {0};
        int64_t started = esp_timer_get_time();
        h38_fatfs_get_counts(ctx->adapter, &before);
        esp_err_t err = h38_fatfs_unmount_cycle(ctx->adapter);
        if (err != ESP_OK)
            return emit_failed_remount(state, cycle, 0u, 0u,
                                       esp_timer_get_time() - started, (int32_t)err);
        err = h38_fatfs_remount_existing(ctx->adapter, &info);
        if (err != ESP_OK || !ctx->adapter->mounted || !mount_info_valid(&info)) {
            h38_fatfs_get_counts(ctx->adapter, &after);
            int32_t failure = err != ESP_OK ? (int32_t)err : ESP_ERR_INVALID_RESPONSE;
            return emit_failed_remount(state, cycle,
                after.read_bytes >= before.read_bytes ? after.read_bytes - before.read_bytes : 0u,
                after.write_bytes >= before.write_bytes ? after.write_bytes - before.write_bytes : 0u,
                esp_timer_get_time() - started, failure);
        }
        h38_fatfs_get_counts(ctx->adapter, &after);
        if (after.read_bytes < before.read_bytes || after.write_bytes < before.write_bytes)
            return set_failure(state, H38_IO_STAGE_SEMANTICS, EOVERFLOW);
        if (!verify_marker(state)) return false;
        if (!check_now(state, (uint64_t)started, false,
                       &(h38_filesystem_io_stage_t){H38_IO_STAGE_NONE},
                       &(int32_t){0})) return false;
        if (!emit_mount_result(state, cycle, &info,
                               after.read_bytes - before.read_bytes,
                               after.write_bytes - before.write_bytes,
                               esp_timer_get_time() - started)) return false;
        if (!emit_probe_result(state, cycle)) return false;
        for (size_t k = 0; k < sizeof(s_keep_indices) / sizeof(s_keep_indices[0]); k++) {
            if (!emit_retained_result(state, cycle, s_keep_indices[k])) return false;
        }
    }
    return true;
}

static bool create_rename_fixtures(io_state_t *state, rename_result_t *result)
{
    memset(result, 0, sizeof(*result));
    if (!create_owned_file(state, "rename-target.bin", 16384u,
                           H38_IO_RENAME_TARGET_SEED, true, false,
                           result->target_sha, H38_IO_STAGE_SEMANTICS) ||
        !create_owned_file(state, "rename-source.bin", 24576u,
                           H38_IO_RENAME_SOURCE_SEED, true, false,
                           result->source_sha, H38_IO_STAGE_SEMANTICS)) return false;
    char target[H38_IO_MAX_PATH], source[H38_IO_MAX_PATH];
    if (!path_join(target, sizeof(target), state->root, "rename-target.bin") ||
        !path_join(source, sizeof(source), state->root, "rename-source.bin"))
        return set_failure(state, H38_IO_STAGE_SEMANTICS, ENAMETOOLONG);
    errno = 0;
    result->rename_rc = rename(source, target);
    result->rename_errno = result->rename_rc == 0 ? 0 : saved_error(EIO);
    if (result->rename_rc != -1 || result->rename_errno != EEXIST)
        return set_failure(state, H38_IO_STAGE_SEMANTICS,
                           result->rename_errno ? result->rename_errno : EIO);
    result->source_present = access(source, F_OK) == 0;
    h38_filesystem_io_stage_t stage = H38_IO_STAGE_NONE;
    int32_t error = 0;
    size_t read = 0;
    uint64_t verify_start = (uint64_t)esp_timer_get_time();
    if (!verify_file(state, target, 16384u, H38_IO_RENAME_TARGET_SEED,
                     verify_start, &read, result->target_sha, &stage, &error) ||
        !verify_file(state, source, 24576u, H38_IO_RENAME_SOURCE_SEED,
                     verify_start, &read, result->source_sha, &stage, &error))
        return set_failure(state, stage == H38_IO_STAGE_NONE ? H38_IO_STAGE_SEMANTICS : stage,
                            error ? error : EILSEQ);
    if (!check_now(state, verify_start, true, &stage, &error)) return false;
    result->target_valid = true;
    result->old_target_preserved = true;
    if (!result->source_present)
        return set_failure(state, H38_IO_STAGE_SEMANTICS, ENOENT);
    return true;
}

static bool reverify_rename_fixtures(io_state_t *state, rename_result_t *result)
{
    if (!verify_named(state, "rename-target.bin", 16384u,
                      H38_IO_RENAME_TARGET_SEED, result->target_sha,
                      H38_IO_STAGE_SEMANTICS) ||
        !verify_named(state, "rename-source.bin", 24576u,
                      H38_IO_RENAME_SOURCE_SEED, result->source_sha,
                      H38_IO_STAGE_SEMANTICS)) return false;
    return true;
}

static bool emit_rename_result(io_state_t *state, const rename_result_t *r)
{
    char fields[512];
    int n = snprintf(fields, sizeof(fields),
        "old_target_valid=%u,source_present=%u,rename_rc=%" PRId32
        ",rename_errno=%" PRId32 ",expected_errno=%d,old_target_preserved=%u"
        ",target_sha256=%s,source_sha256=%s,status=ok,error=0",
        r->target_valid ? 1u : 0u, r->source_present ? 1u : 0u,
        r->rename_rc, r->rename_errno, EEXIST,
        r->old_target_preserved ? 1u : 0u, r->target_sha, r->source_sha);
    if (n < 0 || (size_t)n >= sizeof(fields))
        return set_failure(state, H38_IO_STAGE_SEMANTICS, ESP_ERR_INVALID_SIZE);
    state->ctx->emit(state->ctx->emit_context, "RENAME_RESULT", fields);
    return true;
}

static bool directory_inventory(io_state_t *state, uint32_t *entries,
                                uint32_t *parts, uint32_t *unknown,
                                uint32_t *cleanup_count)
{
    DIR *dir = opendir(state->root);
    struct dirent *entry;
    uint32_t count = 0, part_count = 0, unknown_count = 0;
    bool stale_valid = false;
    if (!dir) return set_failure(state, H38_IO_STAGE_SEMANTICS, saved_error(EIO));
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char path[H38_IO_MAX_PATH];
        struct stat st;
        bool recognized = false;
        uint64_t expected_size = 0;
        if (ends_with(entry->d_name, ".part")) part_count++;
        if (!path_join(path, sizeof(path), state->root, entry->d_name) ||
            stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            unknown_count++;
            continue;
        }
        count++;
        if (!strcmp(entry->d_name, "marker.txt")) {
            recognized = true;
            char marker[512]; size_t marker_len = 0;
            if (!marker_contents(state, marker, sizeof(marker), &marker_len) || st.st_size != (off_t)marker_len)
                unknown_count++;
        } else if (!strcmp(entry->d_name, "probe.bin")) {
            recognized = true; expected_size = 65536u;
        } else if (!strcmp(entry->d_name, "rename-target.bin")) {
            recognized = true; expected_size = 16384u;
        } else if (!strcmp(entry->d_name, "rename-source.bin")) {
            recognized = true; expected_size = 24576u;
        } else if (!strcmp(entry->d_name, "cleanup.stale.part")) {
            recognized = true; expected_size = 4096u;
            char hash[65];
            if (st.st_size == (off_t)expected_size &&
                verify_named(state, entry->d_name, (uint32_t)expected_size,
                             H38_IO_STALE_SEED, hash, H38_IO_STAGE_SEMANTICS)) stale_valid = true;
        } else {
            for (size_t i = 0; i < sizeof(s_keep_indices) / sizeof(s_keep_indices[0]); i++) {
                char expected[40];
                (void)snprintf(expected, sizeof(expected), "matrix-%02" PRIu32 ".bin", s_keep_indices[i]);
                if (!strcmp(entry->d_name, expected)) {
                    recognized = true;
                    expected_size = s_keep_indices[i] < 20u ? 65536u : 196608u;
                    break;
                }
            }
        }
        if (!recognized || (expected_size && st.st_size != (off_t)expected_size)) unknown_count++;
    }
    closedir(dir);
    *entries = count; *parts = part_count; *unknown = unknown_count; *cleanup_count = 1u;
    return count == 11u && part_count == 1u && unknown_count == 0u && stale_valid;
}

static bool emit_directory_result(io_state_t *state)
{
    uint32_t entries = 0, parts = 0, unknown = 0, cleanup = 0;
    bool ok = directory_inventory(state, &entries, &parts, &unknown, &cleanup);
    if (!ok && state->result->failure_stage == H38_IO_STAGE_NONE)
        (void)set_failure(state, H38_IO_STAGE_SEMANTICS, EILSEQ);
    char fields[192];
    int n = snprintf(fields, sizeof(fields),
        "entries=%" PRIu32 ",owned_parts=%" PRIu32 ",unknown_entries=%" PRIu32
        ",cleanup_count=%" PRIu32 ",status=%s,error=%" PRId32,
        entries, parts, unknown, cleanup, ok ? "ok" : "failed",
        ok ? 0 : state->result->failure_error);
    if (n < 0 || (size_t)n >= sizeof(fields))
        return set_failure(state, H38_IO_STAGE_SEMANTICS, ESP_ERR_INVALID_SIZE);
    state->ctx->emit(state->ctx->emit_context, "DIRECTORY_RESULT", fields);
    return ok;
}

static uint32_t count_regular_files(io_state_t *state)
{
    DIR *dir = opendir(state->root);
    struct dirent *entry;
    uint32_t count = 0;
    if (!dir) return UINT32_MAX;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char path[H38_IO_MAX_PATH];
        struct stat st;
        if (!path_join(path, sizeof(path), state->root, entry->d_name) ||
            stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            count = UINT32_MAX;
            break;
        }
        count++;
    }
    closedir(dir);
    return count;
}

static bool emit_reclaim_result(io_state_t *state)
{
    char stale[H38_IO_MAX_PATH], fields[256];
    uint64_t total = 0, before = 0, after = 0;
    uint32_t residual = 0;
    int32_t error = 0;
    h38_filesystem_io_stage_t stage = H38_IO_STAGE_NONE;
    bool ok = path_join(stale, sizeof(stale), state->root, "cleanup.stale.part");
    if (!ok) { stage = H38_IO_STAGE_RECLAIM; error = ENAMETOOLONG; }
    if (ok && !sample_space(state, &total, &before)) {
        stage = H38_IO_STAGE_RECLAIM; error = saved_error(EIO); ok = false;
    }
    if (ok) {
        errno = 0;
        if (unlink(stale) != 0) { stage = H38_IO_STAGE_RECLAIM; error = saved_error(EIO); ok = false; }
    }
    if (ok) {
        /* VFS unlink delegates to FatFs f_unlink, which runs sync_fs/CTRL_SYNC
         * before returning success. This is API-level completion only; there
         * is no directory-fsync or card-cache durability guarantee. */
        if (!check_now(state, state->io_start_us, false, &stage, &error)) ok = false;
    }
    if (ok && !sample_space(state, &total, &after)) {
        stage = H38_IO_STAGE_RECLAIM; error = saved_error(EIO); ok = false;
    }
    if (ok) {
        residual = count_regular_files(state);
        if (residual != 10u || after < before || after - before != 4096u) {
            stage = H38_IO_STAGE_RECLAIM; error = EILSEQ; ok = false;
        }
    }
    if (ok && state->result->retained_file_bytes < 4096u) {
        stage = H38_IO_STAGE_RECLAIM; error = EOVERFLOW; ok = false;
    }
    if (ok) {
        state->result->retained_file_bytes -= 4096u;
    }
    if (!ok) (void)set_failure(state, stage == H38_IO_STAGE_NONE ? H38_IO_STAGE_RECLAIM : stage,
                               error ? error : EIO);
    uint64_t reclaimed = after >= before ? after - before : 0;
    int n = snprintf(fields, sizeof(fields),
        "bytes_before=%" PRIu64 ",bytes_after=%" PRIu64 ",reclaimed_bytes=%" PRIu64
        ",expected_bytes=4096,residual_owned_files=%" PRIu32 ",status=%s,error=%" PRId32,
        before, after, reclaimed, residual, ok ? "ok" : "failed",
        ok ? 0 : (error ? error : EIO));
    if (n < 0 || (size_t)n >= sizeof(fields))
        return set_failure(state, H38_IO_STAGE_RECLAIM, ESP_ERR_INVALID_SIZE);
    state->ctx->emit(state->ctx->emit_context, "RECLAIM_RESULT", fields);
    return ok;
}

static void snapshot_result(io_state_t *state, const h38_guard_counters_t *baseline)
{
    const h38_guard_counters_t *counts = &state->ctx->adapter->guard->phase_counts;
    const h38_guard_counters_t *total = &state->ctx->adapter->guard->total;
    state->result->read_bytes = counts->read_bytes;
    state->result->write_bytes = counts->write_bytes;
    state->result->trim_requests = counts->trim_requests;
    state->result->out_of_bounds_attempts =
        total->out_of_bounds_attempts >= baseline->out_of_bounds_attempts ?
        total->out_of_bounds_attempts - baseline->out_of_bounds_attempts : 0;
    uint64_t now = (uint64_t)esp_timer_get_time();
    state->result->elapsed_us = now >= state->io_start_us ? now - state->io_start_us : 0;
}

esp_err_t h38_filesystem_io_run(const h38_filesystem_io_context_t *context,
                                h38_filesystem_io_result_t *result)
{
    io_state_t state;
    h38_fatfs_mount_info_t initial = {0};
    h38_guard_counters_t baseline;
    rename_result_t rename_result;
    h38_filesystem_io_stage_t deadline_stage = H38_IO_STAGE_NONE;
    int32_t deadline_error = 0;
    if (!result) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));
    if (!valid_context(context)) {
        result->failure_stage = H38_IO_STAGE_SEMANTICS;
        result->failure_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }
    memset(&state, 0, sizeof(state));
    state.ctx = context;
    state.result = result;
    state.io_start_us = (uint64_t)esp_timer_get_time();
    baseline = context->adapter->guard->total;
    state.buffer = (uint8_t *)malloc(H38_IO_BLOCK);
    if (!state.buffer) {
        result->failure_stage = H38_IO_STAGE_IO_CREATE;
        result->failure_error = ENOMEM;
        return ESP_ERR_NO_MEM;
    }
    if (h38_fatfs_mount_info(context->adapter, &initial) != ESP_OK ||
        !mount_info_valid(&initial) || !create_epoch_root(&state) || !write_marker(&state))
        goto failed;
    if (!verify_marker(&state)) goto failed;

    for (uint32_t index = 0; index < 40u; index++) {
        uint32_t size = index < 20u ? 65536u : 196608u;
        uint32_t iteration = index < 20u ? index : index - 20u;
        uint32_t seed = H38_IO_PATTERN_BASE + size + iteration;
        bool keep = false;
        for (size_t k = 0; k < sizeof(s_keep_indices) / sizeof(s_keep_indices[0]); k++)
            if (s_keep_indices[k] == index) keep = true;
        if (!matrix_transaction(&state, index, size, seed, keep)) goto failed;
    }

    {
        char hash[65];
        if (!create_owned_file(&state, "probe.bin", 65536u, H38_IO_PROBE_SEED,
                               true, true, hash, H38_IO_STAGE_PROBE)) goto failed;
    }
    if (!create_rename_fixtures(&state, &rename_result)) goto failed;
    {
        char hash[65];
        if (!create_owned_file(&state, "cleanup.stale.part", 4096u,
                               H38_IO_STALE_SEED, true, false, hash,
                               H38_IO_STAGE_SEMANTICS)) goto failed;
    }
    if (!remount_cycles(&state) || !verify_marker(&state) ||
        !reverify_rename_fixtures(&state, &rename_result)) goto failed;
    if (!emit_directory_result(&state)) goto failed;
    if (!emit_rename_result(&state, &rename_result)) goto failed;
    if (!emit_reclaim_result(&state)) goto failed;
    if (!check_now(&state, state.io_start_us, false, &deadline_stage, &deadline_error)) goto failed;
    snapshot_result(&state, &baseline);
    free(state.buffer);
    return ESP_OK;

failed:
    if (result->failure_stage == H38_IO_STAGE_NONE)
        (void)set_failure(&state, H38_IO_STAGE_SEMANTICS, ESP_FAIL);
    snapshot_result(&state, &baseline);
    free(state.buffer);
    return ESP_FAIL;
}
