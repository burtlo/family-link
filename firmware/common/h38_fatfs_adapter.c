#include "h38_fatfs_adapter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diskio_impl.h"
#include "esp_vfs_fat.h"
#include "esp_heap_caps.h"

#define H38_BASE_PATH "/h38fs"
#define H38_MAX_OPEN_FILES 2u

static h38_fatfs_adapter_t *s_adapter;
static h38_disk_guard_t *s_guard;
static sdmmc_card_t *s_card;

static bool phase_ready(void)
{
    if (!s_guard || !s_guard->initialized || !s_guard->bound ||
        s_guard->failed || !s_guard->phase_active || !s_guard->clock_ms)
        return false;
    uint64_t limit = s_guard->phase == H38_GUARD_PHASE_FORMAT ?
        H38_GUARD_FORMAT_TIME_MS : H38_GUARD_IO_TIME_MS;
    if (s_guard->phase != H38_GUARD_PHASE_FORMAT &&
        s_guard->phase != H38_GUARD_PHASE_IO) return false;
    uint64_t now = s_guard->clock_ms(s_guard->clock_context);
    if (now < s_guard->phase_start_ms || now - s_guard->phase_start_ms > limit) {
        s_guard->failed = 1u;
        s_guard->failure = H38_GUARD_E_TIMEOUT;
        return false;
    }
    return true;
}

static DSTATUS mapped_init(BYTE pdrv)
{
    return (s_adapter && pdrv == s_adapter->drive && phase_ready()) ? 0 : STA_NOINIT;
}

static DSTATUS mapped_status(BYTE pdrv)
{
    if (!s_adapter || pdrv != s_adapter->drive || !phase_ready() || !s_card)
        return STA_NOINIT;
    return sdmmc_get_status(s_card) == ESP_OK ? 0 : STA_NOINIT;
}

static DRESULT mapped_read(BYTE pdrv, BYTE *buff, uint32_t sector, unsigned count)
{
    if (!s_adapter || pdrv != s_adapter->drive || !buff || count == 0u) return RES_PARERR;
    return h38_guard_read(s_guard, sector, count, buff,
                          (size_t)count * H38_GUARD_SECTOR_BYTES) == H38_GUARD_OK ? RES_OK : RES_ERROR;
}

static DRESULT mapped_write(BYTE pdrv, const BYTE *buff, uint32_t sector, unsigned count)
{
    if (!s_adapter || pdrv != s_adapter->drive || !buff || count == 0u) return RES_PARERR;
    return h38_guard_write(s_guard, sector, count, buff,
                           (size_t)count * H38_GUARD_SECTOR_BYTES) == H38_GUARD_OK ? RES_OK : RES_ERROR;
}

static DRESULT mapped_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (!s_adapter || pdrv != s_adapter->drive || !phase_ready()) return RES_PARERR;
    switch (cmd) {
    case CTRL_SYNC:
        /* FatFs API-level completion only; no card cache flush is implied. */
        return s_guard->bound && !s_guard->failed ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        if (!buff) return RES_PARERR;
        *(DWORD *)buff = (DWORD)H38_GUARD_VOLUME_SECTORS;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (!buff) return RES_PARERR;
        *(WORD *)buff = (WORD)H38_GUARD_SECTOR_BYTES;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (!buff) return RES_PARERR;
        *(DWORD *)buff = 8u;
        return RES_OK;
#if FF_USE_TRIM
    case CTRL_TRIM:
        (void)buff;
        (void)h38_guard_trim(s_guard);
        return RES_PARERR;
#endif
    default:
        return RES_PARERR;
    }
}

static const ff_diskio_impl_t s_mapped_disk = {
    .init = mapped_init,
    .status = mapped_status,
    .read = mapped_read,
    .write = mapped_write,
    .ioctl = mapped_ioctl,
};

static esp_err_t ensure_registered(h38_fatfs_adapter_t *adapter)
{
    esp_vfs_fat_conf_t vfs_conf;
    esp_err_t err;
    if (!adapter || !adapter->disk_registered || adapter->vfs_registered) return ESP_ERR_INVALID_STATE;
    memset(&vfs_conf, 0, sizeof(vfs_conf));
    vfs_conf.base_path = adapter->base_path;
    vfs_conf.fat_drive = adapter->drive_path;
    vfs_conf.max_files = H38_MAX_OPEN_FILES;
    err = esp_vfs_fat_register_cfg(&vfs_conf, &adapter->fs);
    if (err != ESP_OK) return err;
    adapter->vfs_registered = true;
    return ESP_OK;
}

esp_err_t h38_fatfs_adapter_init(h38_fatfs_adapter_t *adapter,
                                 sdmmc_card_t *card,
                                 h38_disk_guard_t *guard,
                                 uint8_t *dma_buffer,
                                 size_t dma_buffer_bytes)
{
    esp_err_t err;
    BYTE drive;
    if (!adapter || !card || !guard || !guard->initialized || !guard->bound ||
        !dma_buffer || dma_buffer_bytes != H38_GUARD_DMA_BYTES ||
        ((uintptr_t)dma_buffer & 3u) != 0u || guard->dma_buffer != dma_buffer ||
        guard->dma_buffer_bytes != dma_buffer_bytes || s_adapter != NULL) return ESP_ERR_INVALID_ARG;
    memset(adapter, 0, sizeof(*adapter));
    err = ff_diskio_get_drive(&drive);
    if (err != ESP_OK) return err;
    int path_len = snprintf(adapter->drive_path, sizeof(adapter->drive_path), "%u:", (unsigned)drive);
    if (path_len < 0 || (size_t)path_len >= sizeof(adapter->drive_path)) return ESP_FAIL;
    adapter->drive = drive;
    adapter->base_path = H38_BASE_PATH;
    adapter->card = card;
    adapter->guard = guard;
    adapter->dma_buffer = dma_buffer;
    adapter->dma_buffer_bytes = dma_buffer_bytes;
    s_adapter = adapter;
    s_guard = guard;
    s_card = card;
    ff_diskio_register(drive, &s_mapped_disk);
    adapter->disk_registered = true;
    /* VFS registration attaches POSIX FILE operations but does not mount/format. */
    err = ensure_registered(adapter);
    if (err != ESP_OK) {
        ff_diskio_unregister(drive);
        adapter->disk_registered = false;
        s_adapter = NULL;
        s_guard = NULL;
        s_card = NULL;
        return err;
    }
    return ESP_OK;
}

esp_err_t h38_fatfs_format(h38_fatfs_adapter_t *adapter, FRESULT *result)
{
    MKFS_PARM options;
    uint8_t *work;
    FRESULT fr;
    if (!adapter || !adapter->vfs_registered || adapter->mounted || !adapter->guard ||
        adapter->guard->phase != H38_GUARD_PHASE_FORMAT || !result) return ESP_ERR_INVALID_STATE;
    memset(&options, 0, sizeof(options));
    options.fmt = FM_FAT32 | FM_SFD;
    options.n_fat = 2;
    options.align = 1;
    options.au_size = 4096;
    work = heap_caps_calloc(1, 4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!work) return ESP_ERR_NO_MEM;
    fr = f_mkfs(adapter->drive_path, &options, work, 4096);
    memset(work, 0, 4096);
    free(work);
    *result = fr;
    return fr == FR_OK ? ESP_OK : (esp_err_t)fr;
}

esp_err_t h38_fatfs_mount_info(h38_fatfs_adapter_t *adapter,
                               h38_fatfs_mount_info_t *info)
{
    DWORD free_clusters = 0;
    FRESULT fr;
    if (!adapter || !adapter->fs || !info || !adapter->mounted || !adapter->guard ||
        !adapter->guard->phase_active || adapter->fs->fs_type != FS_FAT32 ||
        adapter->fs->csize != 8u || adapter->fs->n_fatent != 130813u ||
        adapter->fs->n_fatent < 2u || adapter->fs->n_fats != 2u || adapter->fs->fsize != 1025u)
        return ESP_ERR_INVALID_STATE;
    fr = f_getfree(adapter->drive_path, &free_clusters, &adapter->fs);
    adapter->last_fresult = (int32_t)fr;
    if (fr != FR_OK) return (esp_err_t)fr;
    if (free_clusters > adapter->fs->n_fatent - 2u) return ESP_ERR_INVALID_RESPONSE;
    memset(info, 0, sizeof(*info));
    info->fs_type = adapter->fs->fs_type;
    info->sector_bytes = H38_GUARD_SECTOR_BYTES;
    info->volume_sectors = (uint32_t)H38_GUARD_VOLUME_SECTORS;
    info->allocation_unit_bytes = (uint32_t)adapter->fs->csize * H38_GUARD_SECTOR_BYTES;
    info->cluster_count = adapter->fs->n_fatent - 2u;
    info->total_bytes = (uint64_t)info->cluster_count * info->allocation_unit_bytes;
    info->free_bytes = (uint64_t)free_clusters * info->allocation_unit_bytes;
    return ESP_OK;
}

static esp_err_t mount_existing(h38_fatfs_adapter_t *adapter,
                                h38_fatfs_mount_info_t *info,
                                h38_guard_phase_t expected_phase)
{
    FRESULT fr;
    esp_err_t err;
    if (!adapter || !adapter->vfs_registered || adapter->mounted || !adapter->fs ||
        !adapter->guard || !adapter->guard->phase_active ||
        adapter->guard->phase != expected_phase)
        return ESP_ERR_INVALID_STATE;
    fr = f_mount(adapter->fs, adapter->drive_path, 1);
    adapter->last_fresult = (int32_t)fr;
    if (fr != FR_OK) return (esp_err_t)fr;
    adapter->mounted = true;
    err = h38_fatfs_mount_info(adapter, info);
    if (err != ESP_OK) {
        (void)f_mount(NULL, adapter->drive_path, 0);
        adapter->mounted = false;
    }
    return err;
}

esp_err_t h38_fatfs_mount_initial(h38_fatfs_adapter_t *adapter,
                                  h38_fatfs_mount_info_t *info)
{
    uint8_t bpb[H38_GUARD_SECTOR_BYTES];
    uint32_t bps, spc, reserved, fats, roots16, total16, fatsz16, total32, fatsz32, root;
    esp_err_t err;
    if (!adapter || !info || !adapter->guard ||
        !adapter->guard->phase_active ||
        adapter->guard->phase != H38_GUARD_PHASE_FORMAT || adapter->mounted)
        return ESP_ERR_INVALID_STATE;
    err = mount_existing(adapter, info, H38_GUARD_PHASE_FORMAT);
    if (err != ESP_OK) return err;
    if (info->fs_type != FS_FAT32 || info->sector_bytes != 512u ||
        info->volume_sectors != H38_GUARD_VOLUME_SECTORS ||
        info->allocation_unit_bytes != 4096u || info->cluster_count != 130811u) goto invalid;
    if (h38_guard_read(adapter->guard, 0u, 1u, bpb, sizeof(bpb)) != H38_GUARD_OK) goto invalid;
    bps = (uint32_t)bpb[11] | ((uint32_t)bpb[12] << 8);
    spc = bpb[13];
    reserved = (uint32_t)bpb[14] | ((uint32_t)bpb[15] << 8);
    fats = bpb[16];
    roots16 = (uint32_t)bpb[17] | ((uint32_t)bpb[18] << 8);
    total16 = (uint32_t)bpb[19] | ((uint32_t)bpb[20] << 8);
    fatsz16 = (uint32_t)bpb[22] | ((uint32_t)bpb[23] << 8);
    total32 = (uint32_t)bpb[32] | ((uint32_t)bpb[33] << 8) |
              ((uint32_t)bpb[34] << 16) | ((uint32_t)bpb[35] << 24);
    fatsz32 = (uint32_t)bpb[36] | ((uint32_t)bpb[37] << 8) |
              ((uint32_t)bpb[38] << 16) | ((uint32_t)bpb[39] << 24);
    root = (uint32_t)bpb[44] | ((uint32_t)bpb[45] << 8) |
           ((uint32_t)bpb[46] << 16) | ((uint32_t)bpb[47] << 24);
    bool valid = bpb[510] == 0x55u && bpb[511] == 0xaau && bps == 512u && spc == 8u &&
                 reserved == 32u && fats == 2u && roots16 == 0u && total16 == 0u &&
                 fatsz16 == 0u && total32 == H38_GUARD_VOLUME_SECTORS &&
                 fatsz32 == 1025u && root == 2u;
    memset(bpb, 0, sizeof(bpb));
    if (!valid) goto invalid;
    return ESP_OK;
invalid:
    memset(bpb, 0, sizeof(bpb));
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t h38_fatfs_unmount_cycle(h38_fatfs_adapter_t *adapter)
{
    FRESULT fr;
    if (!adapter || !adapter->mounted || !adapter->guard || adapter->guard->failed ||
        adapter->guard->phase != H38_GUARD_PHASE_IO) return ESP_ERR_INVALID_STATE;
    fr = f_mount(NULL, adapter->drive_path, 0);
    adapter->last_fresult = (int32_t)fr;
    if (fr != FR_OK) return (esp_err_t)fr;
    adapter->mounted = false;
    return ESP_OK;
}

esp_err_t h38_fatfs_unmount_idle(h38_fatfs_adapter_t *adapter)
{
    FRESULT fr;
    if (!adapter || !adapter->mounted || !adapter->guard ||
        (adapter->guard->phase_active && !adapter->guard->failed))
        return ESP_ERR_INVALID_STATE;
    adapter->final_unmount_attempted = true;
    fr = f_mount(NULL, adapter->drive_path, 0);
    adapter->last_fresult = (int32_t)fr;
    adapter->final_unmount_error = (int32_t)fr;
    if (fr != FR_OK) return (esp_err_t)fr;
    adapter->mounted = false;
    return ESP_OK;
}

esp_err_t h38_fatfs_remount_existing(h38_fatfs_adapter_t *adapter,
                                     h38_fatfs_mount_info_t *info)
{
    return mount_existing(adapter, info, H38_GUARD_PHASE_IO);
}

esp_err_t h38_fatfs_adapter_deinit(h38_fatfs_adapter_t *adapter)
{
    esp_err_t first = ESP_OK;
    if (!adapter || !adapter->guard ||
        (adapter->guard->phase_active && !adapter->guard->failed)) return ESP_ERR_INVALID_STATE;
    if (adapter->mounted && !adapter->final_unmount_attempted) {
        FRESULT fr = f_mount(NULL, adapter->drive_path, 0);
        adapter->last_fresult = (int32_t)fr;
        if (fr != FR_OK) first = (esp_err_t)fr;
        else adapter->mounted = false;
    }
    if (adapter->vfs_registered) {
        esp_err_t err = esp_vfs_fat_unregister_path(adapter->base_path);
        if (err != ESP_OK && first == ESP_OK) first = err;
        if (err == ESP_OK) adapter->vfs_registered = false;
    }
    if (adapter->disk_registered) {
        ff_diskio_unregister(adapter->drive);
        adapter->disk_registered = false;
    }
    s_adapter = NULL;
    s_guard = NULL;
    s_card = NULL;
    return first;
}

void h38_fatfs_get_counts(const h38_fatfs_adapter_t *adapter,
                          h38_fatfs_counts_t *counts)
{
    const h38_guard_counters_t *source;
    if (!adapter || !adapter->guard || !counts) return;
    source = &adapter->guard->total;
    memset(counts, 0, sizeof(*counts));
    counts->read_bytes = source->read_bytes;
    counts->write_bytes = source->write_bytes;
    counts->read_driver_calls = source->read_driver_calls;
    counts->write_driver_calls = source->write_driver_calls;
    counts->driver_sectors = source->driver_sectors;
    counts->max_call_sectors = source->max_call_sectors;
    counts->trim_requests = source->trim_requests;
    counts->out_of_bounds_attempts = source->out_of_bounds_attempts;
}

const char *h38_fatfs_base_path(const h38_fatfs_adapter_t *adapter)
{
    return adapter ? adapter->base_path : NULL;
}
