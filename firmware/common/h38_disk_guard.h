#ifndef FAMILY_LINK_H38_DISK_GUARD_H
#define FAMILY_LINK_H38_DISK_GUARD_H

/* SDK-free guard for H38's fixed 512-byte, 512 MiB virtual FAT32 volume. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define H38_GUARD_SECTOR_BYTES 512u
#define H38_GUARD_VOLUME_START_LBA UINT64_C(32768)
#define H38_GUARD_VOLUME_SECTORS UINT64_C(1048576)
#define H38_GUARD_CARD_SECTORS UINT64_C(121503744)
#define H38_GUARD_MAX_DRIVER_SECTORS 8u
#define H38_GUARD_DMA_BYTES 4096u
#define H38_GUARD_FORMAT_READ_LIMIT UINT64_C(16777216)
#define H38_GUARD_FORMAT_WRITE_LIMIT UINT64_C(4194304)
#define H38_GUARD_FORMAT_TIME_MS UINT64_C(120000)
#define H38_GUARD_IO_READ_LIMIT UINT64_C(67108864)
#define H38_GUARD_IO_WRITE_LIMIT UINT64_C(33554432)
#define H38_GUARD_IO_TIME_MS UINT64_C(900000)

typedef enum {
    H38_GUARD_PHASE_NONE = 0,
    H38_GUARD_PHASE_FORMAT = 1,
    H38_GUARD_PHASE_IO = 2
} h38_guard_phase_t;

typedef enum {
    H38_GUARD_OK = 0,
    H38_GUARD_E_ARGUMENT,
    H38_GUARD_E_ALIGNMENT,
    H38_GUARD_E_BIND,
    H38_GUARD_E_PHASE,
    H38_GUARD_E_RANGE,
    H38_GUARD_E_OVERFLOW,
    H38_GUARD_E_BUDGET,
    H38_GUARD_E_BACKEND,
    H38_GUARD_E_TIMEOUT,
    H38_GUARD_E_ALIAS,
    H38_GUARD_E_CLOSED
} h38_guard_result_t;

/* Return 0 for success. The backend receives only physical runs of <= 8 sectors. */
typedef int (*h38_guard_backend_fn)(void *context, int write,
                                    uint64_t physical_lba, uint32_t sectors,
                                    uint8_t *dma_buffer);
typedef uint64_t (*h38_guard_clock_fn)(void *context);

typedef struct {
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t driver_calls;
    uint64_t driver_sectors;
    uint64_t read_driver_calls;
    uint64_t write_driver_calls;
    uint64_t out_of_bounds_attempts;
    uint32_t max_call_sectors;
    uint64_t trim_requests;
} h38_guard_counters_t;

typedef struct {
    uint8_t *dma_buffer;
    size_t dma_buffer_bytes;
    h38_guard_backend_fn backend;
    void *backend_context;
    h38_guard_clock_fn clock_ms;
    void *clock_context;
    uint8_t initialized;
    uint8_t bound;
    uint8_t failed;
    uint8_t format_complete;
    uint8_t io_started;
    uint8_t phase_active;
    h38_guard_phase_t phase;
    h38_guard_result_t failure;
    uint64_t phase_start_ms;
    h38_guard_counters_t total;
    h38_guard_counters_t phase_counts;
} h38_disk_guard_t;

/*
 * The platform supplies its 4 KiB internal-DMA-capable, >=4-byte-aligned buffer.
 * The guard checks size and pointer alignment; SDK/source review must prove the
 * buffer is in internal DMA-capable memory. Caller buffers must not alias it.
 */
h38_guard_result_t h38_guard_init(h38_disk_guard_t *guard,
                                  uint8_t *dma_buffer,
                                  size_t dma_buffer_bytes,
                                  h38_guard_backend_fn backend,
                                  void *backend_context,
                                  h38_guard_clock_fn clock_ms,
                                  void *clock_context);

/* Bind only after private identity, expected old MBR, and intent all match. */
h38_guard_result_t h38_guard_bind(h38_disk_guard_t *guard,
                                  int identity_match,
                                  int old_mbr_match,
                                  int intent_match);

/* The only supported phase order is FORMAT then IO. */
h38_guard_result_t h38_guard_begin_phase(h38_disk_guard_t *guard,
                                         h38_guard_phase_t phase);
h38_guard_result_t h38_guard_end_phase(h38_disk_guard_t *guard);

/* Buffers may be unaligned; transfers are copied through the supplied DMA buffer. */
h38_guard_result_t h38_guard_read(h38_disk_guard_t *guard,
                                  uint64_t virtual_lba,
                                  uint32_t sectors,
                                  void *buffer,
                                  size_t buffer_bytes);
h38_guard_result_t h38_guard_write(h38_disk_guard_t *guard,
                                   uint64_t virtual_lba,
                                   uint32_t sectors,
                                   const void *buffer,
                                   size_t buffer_bytes);

/* FatFs optional trim is intercepted: it never calls a backend or erases media. */
h38_guard_result_t h38_guard_trim(h38_disk_guard_t *guard);

#ifdef __cplusplus
}
#endif

#endif
