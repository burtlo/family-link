#include "h38_disk_guard.h"

#include <limits.h>
#include <string.h>

static h38_guard_result_t fail(h38_disk_guard_t *guard,
                               h38_guard_result_t result)
{
    if (guard != NULL && !guard->failed) {
        guard->failed = 1u;
        guard->failure = result;
    }
    return result;
}

static int add_u64(uint64_t *value, uint64_t amount)
{
    if (UINT64_MAX - *value < amount) {
        return 0;
    }
    *value += amount;
    return 1;
}

static h38_guard_result_t validate_nonoverlap(h38_disk_guard_t *guard,
                                              const void *buffer,
                                              size_t buffer_bytes)
{
    uintmax_t buffer_start = (uintmax_t)(uintptr_t)buffer;
    uintmax_t dma_start = (uintmax_t)(uintptr_t)guard->dma_buffer;
    uintmax_t address_max = (uintmax_t)UINTPTR_MAX;
    uintmax_t buffer_length = (uintmax_t)buffer_bytes;
    uintmax_t dma_length = (uintmax_t)guard->dma_buffer_bytes;
    uintmax_t buffer_end;
    uintmax_t dma_end;

    if (buffer_length > address_max - buffer_start ||
        dma_length > address_max - dma_start) {
        return fail(guard, H38_GUARD_E_OVERFLOW);
    }
    buffer_end = buffer_start + buffer_length;
    dma_end = dma_start + dma_length;
    if (buffer_start < dma_end && dma_start < buffer_end) {
        return fail(guard, H38_GUARD_E_ALIAS);
    }
    return H38_GUARD_OK;
}

static uint64_t phase_limit_bytes(const h38_disk_guard_t *guard, int write)
{
    if (guard->phase == H38_GUARD_PHASE_FORMAT) {
        return write ? H38_GUARD_FORMAT_WRITE_LIMIT : H38_GUARD_FORMAT_READ_LIMIT;
    }
    return write ? H38_GUARD_IO_WRITE_LIMIT : H38_GUARD_IO_READ_LIMIT;
}

static uint64_t phase_limit_ms(const h38_disk_guard_t *guard)
{
    return guard->phase == H38_GUARD_PHASE_FORMAT
               ? H38_GUARD_FORMAT_TIME_MS
               : H38_GUARD_IO_TIME_MS;
}

static h38_guard_result_t check_time(h38_disk_guard_t *guard)
{
    uint64_t now;

    if (guard->clock_ms == NULL) {
        return fail(guard, H38_GUARD_E_ARGUMENT);
    }
    now = guard->clock_ms(guard->clock_context);
    if (now < guard->phase_start_ms ||
        now - guard->phase_start_ms > phase_limit_ms(guard)) {
        return fail(guard, H38_GUARD_E_TIMEOUT);
    }
    return H38_GUARD_OK;
}

static h38_guard_result_t charge_callback(h38_disk_guard_t *guard,
                                          int write,
                                          uint64_t bytes)
{
    uint64_t *total_bytes = write ? &guard->total.write_bytes
                                  : &guard->total.read_bytes;
    uint64_t *phase_bytes = write ? &guard->phase_counts.write_bytes
                                  : &guard->phase_counts.read_bytes;
    uint64_t limit = phase_limit_bytes(guard, write);

    if (bytes > limit || *phase_bytes > limit - bytes) {
        return fail(guard, H38_GUARD_E_BUDGET);
    }
    if (UINT64_MAX - *total_bytes < bytes ||
        UINT64_MAX - *phase_bytes < bytes) {
        return fail(guard, H38_GUARD_E_OVERFLOW);
    }

    /* Charge the complete callback before any driver sub-call can occur. */
    *total_bytes += bytes;
    *phase_bytes += bytes;
    return H38_GUARD_OK;
}

static h38_guard_result_t transfer(h38_disk_guard_t *guard,
                                   int write,
                                   uint64_t virtual_lba,
                                   uint32_t sectors,
                                   void *buffer,
                                   size_t buffer_bytes)
{
    uint64_t end_lba;
    uint64_t total_bytes;
    uint64_t physical_lba;
    uint64_t remaining;
    uint32_t offset_sectors = 0u;
    h38_guard_result_t result;

    if (guard == NULL) {
        return H38_GUARD_E_ARGUMENT;
    }
    if (guard->failed) {
        return H38_GUARD_E_CLOSED;
    }
    if (!guard->initialized) {
        return fail(guard, H38_GUARD_E_ARGUMENT);
    }
    if (!guard->bound) {
        return fail(guard, H38_GUARD_E_BIND);
    }
    if (!guard->phase_active ||
        (guard->phase != H38_GUARD_PHASE_FORMAT &&
         guard->phase != H38_GUARD_PHASE_IO)) {
        return fail(guard, H38_GUARD_E_PHASE);
    }
    if (buffer == NULL || sectors == 0u) {
        return fail(guard, H38_GUARD_E_ARGUMENT);
    }
    if (virtual_lba > UINT64_MAX - (uint64_t)sectors) {
        if (!add_u64(&guard->total.out_of_bounds_attempts, 1u)) {
            return fail(guard, H38_GUARD_E_OVERFLOW);
        }
        return fail(guard, H38_GUARD_E_RANGE);
    }
    end_lba = virtual_lba + (uint64_t)sectors;
    if (end_lba > H38_GUARD_VOLUME_SECTORS) {
        if (!add_u64(&guard->total.out_of_bounds_attempts, 1u)) {
            return fail(guard, H38_GUARD_E_OVERFLOW);
        }
        return fail(guard, H38_GUARD_E_RANGE);
    }
    if ((uint64_t)sectors > UINT64_MAX / H38_GUARD_SECTOR_BYTES) {
        return fail(guard, H38_GUARD_E_OVERFLOW);
    }
    total_bytes = (uint64_t)sectors * H38_GUARD_SECTOR_BYTES;
    if (total_bytes > SIZE_MAX || (size_t)total_bytes != buffer_bytes) {
        return fail(guard, H38_GUARD_E_ARGUMENT);
    }
    result = validate_nonoverlap(guard, buffer, buffer_bytes);
    if (result != H38_GUARD_OK) {
        return result;
    }
    if (virtual_lba > UINT64_MAX - H38_GUARD_VOLUME_START_LBA) {
        if (!add_u64(&guard->total.out_of_bounds_attempts, 1u)) {
            return fail(guard, H38_GUARD_E_OVERFLOW);
        }
        return fail(guard, H38_GUARD_E_RANGE);
    }
    physical_lba = H38_GUARD_VOLUME_START_LBA + virtual_lba;
    if (physical_lba > H38_GUARD_CARD_SECTORS ||
        (uint64_t)sectors > H38_GUARD_CARD_SECTORS - physical_lba) {
        if (!add_u64(&guard->total.out_of_bounds_attempts, 1u)) {
            return fail(guard, H38_GUARD_E_OVERFLOW);
        }
        return fail(guard, H38_GUARD_E_RANGE);
    }
    result = check_time(guard);
    if (result != H38_GUARD_OK) {
        return result;
    }
    result = charge_callback(guard, write, total_bytes);
    if (result != H38_GUARD_OK) {
        return result;
    }

    remaining = sectors;
    while (remaining != 0u) {
        uint32_t chunk = remaining > H38_GUARD_MAX_DRIVER_SECTORS
                             ? H38_GUARD_MAX_DRIVER_SECTORS
                             : (uint32_t)remaining;
        size_t chunk_bytes = (size_t)chunk * H38_GUARD_SECTOR_BYTES;
        uint64_t chunk_lba = physical_lba + offset_sectors;
        uint64_t calls_before = guard->total.driver_calls;
        uint64_t sectors_before = guard->total.driver_sectors;
        uint64_t *direction_calls = write ? &guard->total.write_driver_calls
                                           : &guard->total.read_driver_calls;
        uint64_t *phase_direction_calls = write
                                             ? &guard->phase_counts.write_driver_calls
                                             : &guard->phase_counts.read_driver_calls;

        if (chunk == 0u || chunk > H38_GUARD_MAX_DRIVER_SECTORS ||
            guard->dma_buffer == NULL ||
            guard->dma_buffer_bytes != H38_GUARD_DMA_BYTES ||
            ((uintptr_t)guard->dma_buffer & 3u) != 0u) {
            return fail(guard, H38_GUARD_E_ALIGNMENT);
        }
        if (guard->total.driver_calls == UINT64_MAX ||
            guard->total.driver_sectors > UINT64_MAX - chunk ||
            *direction_calls == UINT64_MAX ||
            guard->phase_counts.driver_calls == UINT64_MAX ||
            guard->phase_counts.driver_sectors > UINT64_MAX - chunk ||
            *phase_direction_calls == UINT64_MAX) {
            return fail(guard, H38_GUARD_E_OVERFLOW);
        }

        /* Driver-call/sector counters count attempts, including failed calls. */
        guard->total.driver_calls++;
        guard->total.driver_sectors += chunk;
        (*direction_calls)++;
        if (chunk > guard->total.max_call_sectors) {
            guard->total.max_call_sectors = chunk;
        }
        guard->phase_counts.driver_calls++;
        guard->phase_counts.driver_sectors += chunk;
        (*phase_direction_calls)++;
        if (chunk > guard->phase_counts.max_call_sectors) {
            guard->phase_counts.max_call_sectors = chunk;
        }

        if (write) {
            memcpy(guard->dma_buffer,
                   (const uint8_t *)buffer +
                       ((size_t)offset_sectors * H38_GUARD_SECTOR_BYTES),
                   chunk_bytes);
        }
        if (guard->backend(guard->backend_context, write, chunk_lba,
                           chunk, guard->dma_buffer) != 0) {
            return fail(guard, H38_GUARD_E_BACKEND);
        }
        result = check_time(guard);
        if (result != H38_GUARD_OK) {
            return result;
        }
        if (!write) {
            memcpy((uint8_t *)buffer +
                       ((size_t)offset_sectors * H38_GUARD_SECTOR_BYTES),
                   guard->dma_buffer, chunk_bytes);
        }

        /* Defensive invariants: attempted counters only move forward. */
        if (guard->total.driver_calls != calls_before + 1u ||
            guard->total.driver_sectors != sectors_before + chunk) {
            return fail(guard, H38_GUARD_E_OVERFLOW);
        }
        offset_sectors += chunk;
        remaining -= chunk;
    }
    return H38_GUARD_OK;
}

h38_guard_result_t h38_guard_init(h38_disk_guard_t *guard,
                                  uint8_t *dma_buffer,
                                  size_t dma_buffer_bytes,
                                  h38_guard_backend_fn backend,
                                  void *backend_context,
                                  h38_guard_clock_fn clock_ms,
                                  void *clock_context)
{
    if (guard == NULL) {
        return H38_GUARD_E_ARGUMENT;
    }
    memset(guard, 0, sizeof(*guard));
    if (dma_buffer == NULL || dma_buffer_bytes != H38_GUARD_DMA_BYTES ||
        ((uintptr_t)dma_buffer & 3u) != 0u) {
        return fail(guard, H38_GUARD_E_ALIGNMENT);
    }
    if ((uintmax_t)dma_buffer_bytes >
        (uintmax_t)UINTPTR_MAX - (uintmax_t)(uintptr_t)dma_buffer) {
        return fail(guard, H38_GUARD_E_OVERFLOW);
    }
    if (backend == NULL || clock_ms == NULL) {
        return fail(guard, H38_GUARD_E_ARGUMENT);
    }
    guard->dma_buffer = dma_buffer;
    guard->dma_buffer_bytes = dma_buffer_bytes;
    guard->backend = backend;
    guard->backend_context = backend_context;
    guard->clock_ms = clock_ms;
    guard->clock_context = clock_context;
    guard->initialized = 1u;
    return H38_GUARD_OK;
}

h38_guard_result_t h38_guard_bind(h38_disk_guard_t *guard,
                                  int identity_match,
                                  int old_mbr_match,
                                  int intent_match)
{
    if (guard == NULL) {
        return H38_GUARD_E_ARGUMENT;
    }
    if (guard->failed) {
        return H38_GUARD_E_CLOSED;
    }
    if (!guard->initialized) {
        return fail(guard, H38_GUARD_E_ARGUMENT);
    }
    if (guard->bound || !identity_match || !old_mbr_match || !intent_match) {
        return fail(guard, H38_GUARD_E_BIND);
    }
    guard->bound = 1u;
    return H38_GUARD_OK;
}

h38_guard_result_t h38_guard_begin_phase(h38_disk_guard_t *guard,
                                         h38_guard_phase_t phase)
{
    if (guard == NULL) {
        return H38_GUARD_E_ARGUMENT;
    }
    if (guard->failed) {
        return H38_GUARD_E_CLOSED;
    }
    if (!guard->initialized) {
        return fail(guard, H38_GUARD_E_ARGUMENT);
    }
    if (!guard->bound) {
        return fail(guard, H38_GUARD_E_BIND);
    }
    if (guard->phase_active ||
        (phase != H38_GUARD_PHASE_FORMAT && phase != H38_GUARD_PHASE_IO) ||
        (phase == H38_GUARD_PHASE_FORMAT && guard->format_complete) ||
        (phase == H38_GUARD_PHASE_IO &&
         (!guard->format_complete || guard->io_started))) {
        return fail(guard, H38_GUARD_E_PHASE);
    }
    memset(&guard->phase_counts, 0, sizeof(guard->phase_counts));
    guard->phase = phase;
    guard->phase_active = 1u;
    if (phase == H38_GUARD_PHASE_IO) {
        guard->io_started = 1u;
    }
    guard->phase_start_ms = guard->clock_ms(guard->clock_context);
    return H38_GUARD_OK;
}

h38_guard_result_t h38_guard_end_phase(h38_disk_guard_t *guard)
{
    h38_guard_result_t result;

    if (guard == NULL) {
        return H38_GUARD_E_ARGUMENT;
    }
    if (guard->failed) {
        return H38_GUARD_E_CLOSED;
    }
    if (!guard->initialized) {
        return fail(guard, H38_GUARD_E_ARGUMENT);
    }
    if (!guard->phase_active) {
        return fail(guard, H38_GUARD_E_PHASE);
    }
    result = check_time(guard);
    if (result != H38_GUARD_OK) {
        return result;
    }
    if (guard->phase == H38_GUARD_PHASE_FORMAT) {
        guard->format_complete = 1u;
    }
    guard->phase_active = 0u;
    guard->phase = H38_GUARD_PHASE_NONE;
    return H38_GUARD_OK;
}

h38_guard_result_t h38_guard_read(h38_disk_guard_t *guard,
                                  uint64_t virtual_lba,
                                  uint32_t sectors,
                                  void *buffer,
                                  size_t buffer_bytes)
{
    return transfer(guard, 0, virtual_lba, sectors, buffer, buffer_bytes);
}

h38_guard_result_t h38_guard_write(h38_disk_guard_t *guard,
                                   uint64_t virtual_lba,
                                   uint32_t sectors,
                                   const void *buffer,
                                   size_t buffer_bytes)
{
    return transfer(guard, 1, virtual_lba, sectors, (void *)buffer,
                    buffer_bytes);
}

h38_guard_result_t h38_guard_trim(h38_disk_guard_t *guard)
{
    h38_guard_result_t result;
    if (guard == NULL) {
        return H38_GUARD_E_ARGUMENT;
    }
    if (guard->failed) {
        return H38_GUARD_E_CLOSED;
    }
    if (!guard->initialized) {
        return fail(guard, H38_GUARD_E_ARGUMENT);
    }
    if (!guard->bound || !guard->phase_active) {
        return fail(guard, !guard->bound ? H38_GUARD_E_BIND
                                         : H38_GUARD_E_PHASE);
    }
    result = check_time(guard);
    if (result != H38_GUARD_OK) {
        return result;
    }
    if (check_time(guard) != H38_GUARD_OK) {
        return guard->failure;
    }
    if (guard->total.trim_requests == UINT64_MAX ||
        guard->phase_counts.trim_requests == UINT64_MAX) {
        return fail(guard, H38_GUARD_E_OVERFLOW);
    }
    guard->total.trim_requests++;
    guard->phase_counts.trim_requests++;
    /* RES_PARERR is mapped by the FatFs adapter; there is deliberately no erase API. */
    return H38_GUARD_OK;
}
