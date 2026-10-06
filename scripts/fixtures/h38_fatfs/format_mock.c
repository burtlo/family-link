#define _DEFAULT_SOURCE
#include "ff.h"
#include "diskio.h"
#include "h38_disk_guard.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define DISK_BYTES ((size_t)H38_GUARD_VOLUME_SECTORS * 512u)
#define PHYS_END (H38_GUARD_VOLUME_START_LBA + H38_GUARD_VOLUME_SECTORS)

PARTITION VolToPart[FF_VOLUMES] = {{0, 0}, {0, 0}};
static uint8_t *disk;
static _Alignas(4) uint8_t dma[H38_GUARD_DMA_BYTES];
static h38_disk_guard_t guard;
static uint64_t ticks;
static unsigned calls, writes, reads, max_call, trims;
static unsigned fatfs_write_callbacks, fatfs_read_callbacks;
static unsigned max_fatfs_callback_sectors;
static uint64_t min_lba = UINT64_MAX, max_end;
static unsigned fail_on_call;
static unsigned successful_sectors;

static uint64_t clock_ms(void *unused) { (void)unused; return ticks; }

static int backend(void *unused, int write, uint64_t lba, uint32_t count,
                   uint8_t *buffer)
{
    (void)unused;
    calls++;
    if (count == 0 || count > H38_GUARD_MAX_DRIVER_SECTORS ||
        lba < H38_GUARD_VOLUME_START_LBA || lba + count > PHYS_END ||
        buffer != dma || ((uintptr_t)buffer & 3u) != 0u) return -1;
    if (lba < min_lba) min_lba = lba;
    if (lba + count > max_end) max_end = lba + count;
    if (count > max_call) max_call = count;
    if (write) writes++; else reads++;
    if (fail_on_call && calls == fail_on_call) return -1;
    size_t offset = (size_t)(lba - H38_GUARD_VOLUME_START_LBA) * 512u;
    size_t bytes = (size_t)count * 512u;
    if (write) {
        memcpy(disk + offset, buffer, bytes);
        successful_sectors += count;
    } else memcpy(buffer, disk + offset, bytes);
    return 0;
}

static DRESULT convert(h38_guard_result_t r)
{
    if (r == H38_GUARD_OK) return RES_OK;
    if (r == H38_GUARD_E_RANGE || r == H38_GUARD_E_ARGUMENT ||
        r == H38_GUARD_E_BUDGET) return RES_PARERR;
    return RES_ERROR;
}

DSTATUS disk_status(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    fatfs_read_callbacks++;
    if (count > max_fatfs_callback_sectors) max_fatfs_callback_sectors = count;
    return convert(h38_guard_read(&guard, sector, count, buff,
                                  (size_t)count * 512u));
}
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    fatfs_write_callbacks++;
    if (count > max_fatfs_callback_sectors) max_fatfs_callback_sectors = count;
    return convert(h38_guard_write(&guard, sector, count, buff,
                                   (size_t)count * 512u));
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = H38_GUARD_VOLUME_SECTORS; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    case CTRL_TRIM:
        trims++;
        if (h38_guard_trim(&guard) != H38_GUARD_OK) return RES_ERROR;
        return RES_PARERR;
    default: return RES_PARERR;
    }
}

int ff_mutex_create(int vol) { (void)vol; return 1; }
void ff_mutex_delete(int vol) { (void)vol; }
int ff_mutex_take(int vol) { (void)vol; return 1; }
void ff_mutex_give(int vol) { (void)vol; }
DWORD get_fattime(void) { return ((DWORD)(2026 - 1980) << 25) | ((DWORD)10 << 21) | ((DWORD)6 << 16); }

static void reset_guard(void)
{
    memset(&guard, 0, sizeof guard);
    calls = writes = reads = max_call = trims = 0;
    fatfs_write_callbacks = fatfs_read_callbacks = max_fatfs_callback_sectors = 0;
    min_lba = UINT64_MAX; max_end = 0; fail_on_call = 0; successful_sectors = 0;
    ticks = 1;
    assert(h38_guard_init(&guard, dma, sizeof dma, backend, NULL, clock_ms, NULL) == H38_GUARD_OK);
    assert(h38_guard_bind(&guard, 1, 1, 1) == H38_GUARD_OK);
    assert(h38_guard_begin_phase(&guard, H38_GUARD_PHASE_FORMAT) == H38_GUARD_OK);
}

static void negative_cases(void)
{
    uint8_t *big = mmap(NULL, 16u * 1024u * 1024u + 512u,
                        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(big != MAP_FAILED);
    reset_guard();
    uint8_t rangebuf[1024];
    assert(h38_guard_write(&guard, H38_GUARD_VOLUME_SECTORS - 1, 2, rangebuf, sizeof rangebuf) == H38_GUARD_E_RANGE);
    assert(calls == 0 && guard.total.out_of_bounds_attempts == 1);

    reset_guard();
    assert(h38_guard_write(&guard, 0, 8193, big, 8193u * 512u) == H38_GUARD_E_BUDGET);
    assert(calls == 0 && guard.phase_counts.write_bytes == 0);

    reset_guard();
    assert(h38_guard_read(&guard, 0, 32769, big, 32769u * 512u) == H38_GUARD_E_BUDGET);
    assert(calls == 0 && guard.phase_counts.read_bytes == 0);

    reset_guard();
    uint8_t nine[9u * 512u] = {0};
    assert(h38_guard_write(&guard, 100, 9, nine, sizeof nine) == H38_GUARD_OK);
    assert(calls == 2 && max_call == 8 && writes == 2 && successful_sectors == 9);

    reset_guard();
    fail_on_call = 2;
    assert(h38_guard_write(&guard, 200, 9, nine, sizeof nine) == H38_GUARD_E_BACKEND);
    assert(calls == 2 && successful_sectors == 8 && guard.total.write_bytes == sizeof nine);
    assert(h38_guard_write(&guard, 200, 1, nine, 512) == H38_GUARD_E_CLOSED);
    assert(calls == 2 && successful_sectors == 8);
    assert(munmap(big, 16u * 1024u * 1024u + 512u) == 0);
}

int main(void)
{
    disk = mmap(NULL, DISK_BYTES, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(disk != MAP_FAILED);
    reset_guard();
    MKFS_PARM opt = {FM_FAT32 | FM_SFD, 2, 1, 0, 4096};
    uint8_t work[4096];
    FRESULT fr = f_mkfs("0:", &opt, work, sizeof work);
    if (fr != FR_OK) { fprintf(stderr, "f_mkfs=%d\n", fr); return 2; }
    FATFS fs;
    fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK) { fprintf(stderr, "f_mount=%d\n", fr); return 3; }
    assert(fs.fs_type == FS_FAT32);
    assert(fs.csize == 8);
    uint32_t data_clusters = fs.n_fatent - 2;
    uint32_t fat_sectors = (uint32_t)disk[36] | ((uint32_t)disk[37] << 8) |
                           ((uint32_t)disk[38] << 16) | ((uint32_t)disk[39] << 24);
    uint32_t total_sectors = (uint32_t)disk[32] | ((uint32_t)disk[33] << 8) |
                             ((uint32_t)disk[34] << 16) | ((uint32_t)disk[35] << 24);
    assert(fat_sectors == 1025 && total_sectors == H38_GUARD_VOLUME_SECTORS);
    assert(disk[510] == 0x55 && disk[511] == 0xaa);
    assert(data_clusters == 130811);
    assert(guard.phase_counts.write_bytes == 2062u * 512u);
    assert(guard.phase_counts.max_call_sectors <= H38_GUARD_MAX_DRIVER_SECTORS);
    assert(calls <= 10000 && writes != 0 && reads != 0);
    assert(fatfs_write_callbacks != 0 && fatfs_read_callbacks != 0);
    assert(min_lba == H38_GUARD_VOLUME_START_LBA && max_end <= PHYS_END);
    if (trims != 1 || guard.total.trim_requests != 1) {
        fprintf(stderr, "trim=%u guarded=%llu\n", trims,
                (unsigned long long)guard.total.trim_requests);
        return 4;
    }
    unsigned callback_writes = fatfs_write_callbacks;
    unsigned callback_reads = fatfs_read_callbacks;
    unsigned callback_max = max_fatfs_callback_sectors;
    uint64_t driver_calls = guard.phase_counts.driver_calls;
    uint32_t driver_max = guard.phase_counts.max_call_sectors;
    uint64_t guarded_write_bytes = guard.phase_counts.write_bytes;
    assert(h38_guard_end_phase(&guard) == H38_GUARD_OK);
    (void)f_mount(NULL, "0:", 0);

    negative_cases();
    printf("PASS format_sectors=2062 format_bytes=%u fat_sectors=%u data_clusters=%u max_driver_call=%u callback_write_calls=%u callback_read_calls=%u max_callback_sectors=%u driver_calls=%u trim_intercepted=1 erase_interface=absent negative_bounds=pass negative_budget=pass negative_read_budget=pass split_8_plus_1=pass partial_dispatch=pass physical_bounds=pass production_guard=pass\n",
           (unsigned)guarded_write_bytes, fat_sectors, data_clusters,
           driver_max, callback_writes, callback_reads, callback_max,
           (unsigned)driver_calls);
    munmap(disk, DISK_BYTES);
    return 0;
}
