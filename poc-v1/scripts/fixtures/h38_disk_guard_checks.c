#include "h38_disk_guard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; \
} } while (0)

typedef struct {
    uint64_t now_ms;
    uint64_t calls;
    uint64_t write_calls;
    uint64_t read_calls;
    uint64_t last_lba;
    uint32_t last_sectors;
    int fail_on_call;
    uint32_t call_limit;
    size_t last_bytes;
    uint8_t last_data[H38_GUARD_DMA_BYTES];
    uint64_t trace_lba[3];
    uint32_t trace_sectors[3];
    uint8_t trace_data[3][H38_GUARD_DMA_BYTES];
    uint64_t latency_ms;
} mock_t;

static uint64_t mock_clock(void *context)
{
    return ((mock_t *)context)->now_ms;
}

static int mock_backend(void *context, int write, uint64_t lba,
                        uint32_t sectors, uint8_t *buffer)
{
    mock_t *mock = (mock_t *)context;
    size_t bytes = (size_t)sectors * H38_GUARD_SECTOR_BYTES;
    uint32_t i;

    mock->calls++;
    if (write) {
        mock->write_calls++;
    } else {
        mock->read_calls++;
    }
    mock->last_lba = lba;
    mock->last_sectors = sectors;
    mock->last_bytes = bytes;
    if (sectors > mock->call_limit || sectors > H38_GUARD_MAX_DRIVER_SECTORS) {
        return -1;
    }
    if (mock->calls <= 3u) {
        mock->trace_lba[mock->calls - 1u] = lba;
        mock->trace_sectors[mock->calls - 1u] = sectors;
    }
    if (write) {
        memcpy(mock->last_data, buffer, bytes);
        if (mock->calls <= 3u) {
            memcpy(mock->trace_data[mock->calls - 1u], buffer, bytes);
        }
    } else {
        for (i = 0; i < bytes; i++) {
            buffer[i] = (uint8_t)((lba + i) & 0xffu);
        }
        if (mock->calls <= 3u) {
            memcpy(mock->trace_data[mock->calls - 1u], buffer, bytes);
        }
    }
    mock->now_ms += mock->latency_ms;
    return mock->fail_on_call == (int)mock->calls ? -1 : 0;
}

static h38_guard_result_t setup(h38_disk_guard_t *guard,
                                uint8_t *dma,
                                mock_t *mock)
{
    h38_guard_result_t result;
    memset(mock, 0, sizeof(*mock));
    mock->call_limit = H38_GUARD_MAX_DRIVER_SECTORS;
    result = h38_guard_init(guard, dma, H38_GUARD_DMA_BYTES,
                            mock_backend, mock, mock_clock, mock);
    if (result != H38_GUARD_OK) return result;
    result = h38_guard_bind(guard, 1, 1, 1);
    if (result != H38_GUARD_OK) return result;
    return h38_guard_begin_phase(guard, H38_GUARD_PHASE_FORMAT);
}

static int test_nine_sector_split(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint8_t data[9u * H38_GUARD_SECTOR_BYTES];
    uint32_t i;
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    mock.call_limit = 8u;
    for (i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i ^ 0xa5u);
    CHECK(h38_guard_write(&guard, 7u, 9u, data, sizeof(data)) == H38_GUARD_OK);
    CHECK(mock.calls == 2u && mock.write_calls == 2u);
    CHECK(guard.total.driver_calls == 2u);
    CHECK(guard.total.write_driver_calls == 2u);
    CHECK(guard.total.driver_sectors == 9u);
    CHECK(guard.total.max_call_sectors == 8u);
    CHECK(guard.phase_counts.write_bytes == sizeof(data));
    CHECK(mock.last_lba == H38_GUARD_VOLUME_START_LBA + 15u);
    CHECK(mock.last_sectors == 1u && mock.last_bytes == 512u);
    CHECK(mock.trace_lba[0] == H38_GUARD_VOLUME_START_LBA + 7u);
    CHECK(mock.trace_sectors[0] == 8u);
    CHECK(mock.trace_lba[1] == H38_GUARD_VOLUME_START_LBA + 15u);
    CHECK(mock.trace_sectors[1] == 1u);
    CHECK(memcmp(mock.trace_data[0], data, 8u * 512u) == 0);
    CHECK(memcmp(mock.trace_data[1], data + 8u * 512u, 512u) == 0);
    CHECK(h38_guard_end_phase(&guard) == H38_GUARD_OK);
    return 0;
}

static int test_read_split_and_volume_edge(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint8_t data[9u * H38_GUARD_SECTOR_BYTES];
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_read(&guard, H38_GUARD_VOLUME_SECTORS - 1u,
                         1u, data, 512u) == H38_GUARD_OK);
    CHECK(mock.last_lba == H38_GUARD_VOLUME_START_LBA +
                               H38_GUARD_VOLUME_SECTORS - 1u);
    CHECK(h38_guard_read(&guard, 0u, 9u, data, sizeof(data)) == H38_GUARD_OK);
    CHECK(mock.calls == 3u && mock.read_calls == 3u);
    CHECK(guard.total.read_driver_calls == 3u);
    CHECK(guard.total.max_call_sectors == 8u);
    {
        uint32_t i;
        uint64_t physical_base = H38_GUARD_VOLUME_START_LBA;
        CHECK(mock.trace_lba[1] == physical_base);
        CHECK(mock.trace_sectors[1] == 8u);
        CHECK(mock.trace_lba[2] == physical_base + 8u);
        CHECK(mock.trace_sectors[2] == 1u);
        for (i = 0; i < sizeof(data); i++) {
            CHECK(data[i] == (uint8_t)((physical_base + (i < 8u * 512u ? 0u : 8u) +
                               (i < 8u * 512u ? i : i - 8u * 512u)) & 0xffu));
        }
    }
    return 0;
}

static int test_range_and_overflow_fail_closed(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint8_t data[512];
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_read(&guard, H38_GUARD_VOLUME_SECTORS, 1u,
                         data, sizeof(data)) == H38_GUARD_E_RANGE);
    CHECK(mock.calls == 0u);
    CHECK(guard.total.out_of_bounds_attempts == 1u);
    CHECK(h38_guard_read(&guard, UINT64_MAX, 1u,
                         data, sizeof(data)) == H38_GUARD_E_CLOSED);
    CHECK(mock.calls == 0u);

    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_read(&guard, UINT64_MAX, 1u,
                         data, sizeof(data)) == H38_GUARD_E_RANGE);
    CHECK(mock.calls == 0u && guard.total.out_of_bounds_attempts == 1u);
    return 0;
}

static int test_bind_phase_and_argument_guards(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint8_t data[512];
    CHECK(h38_guard_init(&guard, dma, sizeof(dma), mock_backend, &mock,
                         mock_clock, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_begin_phase(&guard, H38_GUARD_PHASE_FORMAT) ==
          H38_GUARD_E_BIND);

    CHECK(h38_guard_init(&guard, dma, sizeof(dma), mock_backend, &mock,
                         mock_clock, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_bind(&guard, 1, 0, 1) == H38_GUARD_E_BIND);

    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_read(&guard, 0u, 0u, data, 0u) == H38_GUARD_E_ARGUMENT);
    CHECK(mock.calls == 0u);

    CHECK(h38_guard_init(&guard, dma + 1u, H38_GUARD_DMA_BYTES,
                         mock_backend, &mock, mock_clock, &mock) ==
          H38_GUARD_E_ALIGNMENT);
    return 0;
}

static int test_uninitialized_guard_is_rejected(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint8_t data[512];
    memset(&guard, 0, sizeof(guard));
    memset(&mock, 0, sizeof(mock));
    CHECK(h38_guard_bind(&guard, 1, 1, 1) == H38_GUARD_E_ARGUMENT);
    CHECK(h38_guard_begin_phase(&guard, H38_GUARD_PHASE_FORMAT) ==
          H38_GUARD_E_CLOSED);
    memset(&guard, 0, sizeof(guard));
    CHECK(h38_guard_begin_phase(&guard, H38_GUARD_PHASE_FORMAT) ==
          H38_GUARD_E_ARGUMENT);
    memset(&guard, 0, sizeof(guard));
    CHECK(h38_guard_init(&guard, dma, sizeof(dma), mock_backend, &mock,
                         mock_clock, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_read(&guard, 0u, 1u, data, sizeof(data)) ==
          H38_GUARD_E_BIND);
    CHECK(mock.calls == 0u);
    return 0;
}

static int test_caller_buffer_cannot_alias_dma_bounce(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_read(&guard, 0u, 1u, dma + 1u, 512u) ==
          H38_GUARD_E_ALIAS);
    CHECK(mock.calls == 0u);
    CHECK(guard.total.read_bytes == 0u);
    CHECK(guard.phase_counts.read_bytes == 0u);
    CHECK(guard.failed && guard.failure == H38_GUARD_E_ALIAS);

    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_write(&guard, 0u, 8u, dma, 4096u) ==
          H38_GUARD_E_ALIAS);
    CHECK(mock.calls == 0u);
    CHECK(guard.total.write_bytes == 0u);
    return 0;
}

static int test_caller_pointer_end_overflow(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    void *near_max = (void *)(uintptr_t)(UINTPTR_MAX - 127u);
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_read(&guard, 0u, 1u, near_max, 512u) ==
          H38_GUARD_E_OVERFLOW);
    CHECK(mock.calls == 0u);
    CHECK(guard.total.read_bytes == 0u);
    return 0;
}

static int test_budget_rejects_before_dispatch(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint64_t bytes = H38_GUARD_FORMAT_WRITE_LIMIT + 512u;
    uint64_t sectors = bytes / 512u;
    uint8_t *rejected_buffer = (uint8_t *)malloc((size_t)bytes);
    CHECK(rejected_buffer != NULL);
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_write(&guard, 0u, (uint32_t)sectors,
                          rejected_buffer, (size_t)bytes) == H38_GUARD_E_BUDGET);
    CHECK(mock.calls == 0u);
    CHECK(guard.total.write_bytes == 0u);
    CHECK(guard.phase_counts.write_bytes == 0u);
    free(rejected_buffer);
    return 0;
}

static int test_exact_format_budget_and_io_phase(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint8_t *data = (uint8_t *)malloc((size_t)H38_GUARD_FORMAT_WRITE_LIMIT);
    uint8_t small_data[512];
    /* 4 MiB exactly; every dispatched call is checked by the mock as <=8. */
    CHECK(data != NULL);
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_write(&guard, 0u, 8192u, data,
                          (size_t)H38_GUARD_FORMAT_WRITE_LIMIT) == H38_GUARD_OK);
    CHECK(guard.phase_counts.write_bytes == H38_GUARD_FORMAT_WRITE_LIMIT);
    CHECK(guard.phase_counts.driver_calls == 1024u);
    CHECK(guard.phase_counts.write_driver_calls == 1024u);
    CHECK(guard.phase_counts.driver_sectors == 8192u);
    CHECK(guard.phase_counts.max_call_sectors == 8u);
    CHECK(h38_guard_end_phase(&guard) == H38_GUARD_OK);
    CHECK(h38_guard_begin_phase(&guard, H38_GUARD_PHASE_IO) == H38_GUARD_OK);
    memset(small_data, 0x27, sizeof(small_data));
    CHECK(h38_guard_write(&guard, 0u, 1u, small_data,
                          sizeof(small_data)) == H38_GUARD_OK);
    CHECK(guard.total.write_bytes == H38_GUARD_FORMAT_WRITE_LIMIT + 512u);
    CHECK(guard.phase_counts.write_bytes == 512u);
    CHECK(h38_guard_end_phase(&guard) == H38_GUARD_OK);
    CHECK(h38_guard_begin_phase(&guard, H38_GUARD_PHASE_IO) ==
          H38_GUARD_E_PHASE);
    free(data);
    return 0;
}

static int test_partial_backend_failure_charges_whole_request(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint8_t data[9u * 512u];
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    mock.fail_on_call = 2;
    memset(data, 0x5a, sizeof(data));
    CHECK(h38_guard_write(&guard, 10u, 9u, data, sizeof(data)) ==
          H38_GUARD_E_BACKEND);
    CHECK(mock.calls == 2u);
    CHECK(guard.total.write_bytes == sizeof(data));
    CHECK(guard.phase_counts.write_bytes == sizeof(data));
    CHECK(guard.total.driver_calls == 2u);
    CHECK(guard.total.driver_sectors == 9u);
    CHECK(guard.failed && guard.failure == H38_GUARD_E_BACKEND);
    CHECK(h38_guard_write(&guard, 30u, 1u, data, 512u) ==
          H38_GUARD_E_CLOSED);
    CHECK(mock.calls == 2u);
    return 0;
}

static int test_trim_never_reaches_backend(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_trim(&guard) == H38_GUARD_OK);
    CHECK(guard.total.trim_requests == 1u);
    CHECK(mock.calls == 0u);
    CHECK(h38_guard_end_phase(&guard) == H38_GUARD_OK);
    return 0;
}

static int test_timeout_before_and_after_dispatch(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint8_t data[512];
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    mock.now_ms = H38_GUARD_FORMAT_TIME_MS + 1u;
    CHECK(h38_guard_read(&guard, 0u, 1u, data, sizeof(data)) ==
          H38_GUARD_E_TIMEOUT);
    CHECK(mock.calls == 0u && guard.total.read_bytes == 0u);

    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    mock.latency_ms = 1u;
    CHECK(h38_guard_read(&guard, 0u, 1u, data, sizeof(data)) == H38_GUARD_OK);
    mock.now_ms = H38_GUARD_FORMAT_TIME_MS;
    mock.latency_ms = 1u;
    CHECK(h38_guard_read(&guard, 1u, 1u, data, sizeof(data)) ==
          H38_GUARD_E_TIMEOUT);
    CHECK(mock.calls == 2u);
    CHECK(guard.total.read_bytes == 1024u);
    CHECK(guard.failed && guard.failure == H38_GUARD_E_TIMEOUT);
    return 0;
}

static int test_io_budgets_reject_without_partial_dispatch(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint8_t *data = (uint8_t *)malloc((size_t)H38_GUARD_IO_READ_LIMIT);
    uint64_t calls_after_limit;
    CHECK(data != NULL);
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_end_phase(&guard) == H38_GUARD_OK);
    CHECK(h38_guard_begin_phase(&guard, H38_GUARD_PHASE_IO) == H38_GUARD_OK);
    CHECK(h38_guard_read(&guard, 0u,
                         (uint32_t)(H38_GUARD_IO_READ_LIMIT / 512u), data,
                         (size_t)H38_GUARD_IO_READ_LIMIT) == H38_GUARD_OK);
    calls_after_limit = mock.calls;
    CHECK(guard.phase_counts.read_bytes == H38_GUARD_IO_READ_LIMIT);
    CHECK(guard.phase_counts.read_driver_calls ==
          H38_GUARD_IO_READ_LIMIT / H38_GUARD_DMA_BYTES);
    CHECK(h38_guard_read(&guard, 0u, 1u, data, 512u) == H38_GUARD_E_BUDGET);
    CHECK(mock.calls == calls_after_limit);
    CHECK(guard.phase_counts.read_bytes == H38_GUARD_IO_READ_LIMIT);
    free(data);

    data = (uint8_t *)malloc((size_t)H38_GUARD_IO_WRITE_LIMIT);
    CHECK(data != NULL);
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_end_phase(&guard) == H38_GUARD_OK);
    CHECK(h38_guard_begin_phase(&guard, H38_GUARD_PHASE_IO) == H38_GUARD_OK);
    CHECK(h38_guard_write(&guard, 0u,
                          (uint32_t)(H38_GUARD_IO_WRITE_LIMIT / 512u), data,
                          (size_t)H38_GUARD_IO_WRITE_LIMIT) == H38_GUARD_OK);
    calls_after_limit = mock.calls;
    CHECK(guard.phase_counts.write_bytes == H38_GUARD_IO_WRITE_LIMIT);
    CHECK(guard.phase_counts.write_driver_calls ==
          H38_GUARD_IO_WRITE_LIMIT / H38_GUARD_DMA_BYTES);
    CHECK(h38_guard_write(&guard, 0u, 1u, data, 512u) == H38_GUARD_E_BUDGET);
    CHECK(mock.calls == calls_after_limit);
    CHECK(guard.phase_counts.write_bytes == H38_GUARD_IO_WRITE_LIMIT);
    free(data);
    return 0;
}

static int test_io_time_budget_is_post_call_checked(void)
{
    h38_disk_guard_t guard;
    mock_t mock;
    _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
    uint8_t data[512];
    CHECK(setup(&guard, dma, &mock) == H38_GUARD_OK);
    CHECK(h38_guard_end_phase(&guard) == H38_GUARD_OK);
    CHECK(h38_guard_begin_phase(&guard, H38_GUARD_PHASE_IO) == H38_GUARD_OK);
    mock.latency_ms = H38_GUARD_IO_TIME_MS + 1u;
    CHECK(h38_guard_read(&guard, 0u, 1u, data, sizeof(data)) ==
          H38_GUARD_E_TIMEOUT);
    CHECK(mock.calls == 1u);
    CHECK(guard.total.read_bytes == sizeof(data));
    CHECK(guard.total.driver_calls == 1u);
    CHECK(guard.failed && guard.failure == H38_GUARD_E_TIMEOUT);
    return 0;
}

int main(void)
{
    int (*const tests[])(void) = {
        test_nine_sector_split,
        test_read_split_and_volume_edge,
        test_range_and_overflow_fail_closed,
        test_bind_phase_and_argument_guards,
        test_uninitialized_guard_is_rejected,
        test_caller_buffer_cannot_alias_dma_bounce,
        test_caller_pointer_end_overflow,
        test_budget_rejects_before_dispatch,
        test_exact_format_budget_and_io_phase,
        test_partial_backend_failure_charges_whole_request,
        test_trim_never_reaches_backend,
        test_timeout_before_and_after_dispatch,
        test_io_budgets_reject_without_partial_dispatch,
        test_io_time_budget_is_post_call_checked,
    };
    size_t i;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (tests[i]() != 0) return 1;
    }
    printf("H38 disk guard: %zu C fixture groups passed\n",
           sizeof(tests) / sizeof(tests[0]));
    return 0;
}
