#ifndef FAMILY_LINK_H38_FILESYSTEM_IO_H
#define FAMILY_LINK_H38_FILESYSTEM_IO_H

#include <stdint.h>
#include "esp_err.h"
#include "h38_fatfs_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*h38_filesystem_io_emit_fn)(void *context,
                                           const char *event,
                                           const char *fields);

typedef enum {
    H38_IO_STAGE_NONE = 0,
    H38_IO_STAGE_IO_CREATE,
    H38_IO_STAGE_IO_WRITE,
    H38_IO_STAGE_IO_FFLUSH,
    H38_IO_STAGE_IO_FSYNC,
    H38_IO_STAGE_IO_FCLOSE,
    H38_IO_STAGE_IO_RENAME,
    H38_IO_STAGE_IO_READBACK,
    H38_IO_STAGE_IO_CHECKSUM,
    H38_IO_STAGE_IO_DELETE,
    H38_IO_STAGE_PROBE,
    H38_IO_STAGE_SEMANTICS,
    H38_IO_STAGE_REMOUNT,
    H38_IO_STAGE_RECLAIM,
    H38_IO_STAGE_BUDGET,
    H38_IO_STAGE_TIMEOUT
} h38_filesystem_io_stage_t;

typedef struct {
    h38_fatfs_adapter_t *adapter;
    const char *epoch;
    const char *runtime_elf_sha256;
    const char *source_revision;
    const char *intent_sha256;
    const char *h35_reference_epoch;
    const char *private_cid_sha256;
    h38_filesystem_io_emit_fn emit;
    void *emit_context;
} h38_filesystem_io_context_t;

typedef struct {
    h38_filesystem_io_stage_t failure_stage;
    int32_t failure_error;
    uint32_t io_file_count;
    uint32_t probe_count;
    uint32_t mount_count;
    uint32_t remount_count;
    uint32_t retained_check_count;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t trim_requests;
    uint64_t out_of_bounds_attempts;
    uint64_t generated_logical_bytes;
    uint64_t retained_file_bytes;
    uint64_t elapsed_us;
} h38_filesystem_io_result_t;

/*
 * Runs only the frozen H38 IO phase. Caller must have completed FORMAT and
 * initial mount and begun the IO disk-guard phase. Emits exact event payloads
 * (the caller binds epoch/runtime ELF in its record envelope); never emits
 * COMPLETE or performs adapter/card teardown.
 */
esp_err_t h38_filesystem_io_run(const h38_filesystem_io_context_t *context,
                                h38_filesystem_io_result_t *result);

const char *h38_filesystem_io_stage_name(h38_filesystem_io_stage_t stage);

#ifdef __cplusplus
}
#endif

#endif
