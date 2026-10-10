#ifndef FAMILY_LINK_H38_FATFS_ADAPTER_H
#define FAMILY_LINK_H38_FATFS_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ff.h"
#include "h38_disk_guard.h"
#include "sdmmc_cmd.h"

typedef struct {
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t read_driver_calls;
    uint64_t write_driver_calls;
    uint64_t driver_sectors;
    uint32_t max_call_sectors;
    uint64_t trim_requests;
    uint64_t out_of_bounds_attempts;
} h38_fatfs_counts_t;

typedef struct {
    FATFS *fs;
    BYTE drive;
    char drive_path[8];
    const char *base_path;
    sdmmc_card_t *card;
    h38_disk_guard_t *guard;
    uint8_t *dma_buffer;
    size_t dma_buffer_bytes;
    bool disk_registered;
    bool vfs_registered;
    bool mounted;
    bool final_unmount_attempted;
    int32_t final_unmount_error;
    int32_t last_fresult;
} h38_fatfs_adapter_t;

typedef struct {
    uint32_t fs_type;
    uint32_t sector_bytes;
    uint32_t volume_sectors;
    uint32_t allocation_unit_bytes;
    uint32_t cluster_count;
    uint64_t total_bytes;
    uint64_t free_bytes;
} h38_fatfs_mount_info_t;

/* Custom mapped ff_diskio registration; never registers the stock SDMMC driver. */
esp_err_t h38_fatfs_adapter_init(h38_fatfs_adapter_t *adapter,
                                 sdmmc_card_t *card,
                                 h38_disk_guard_t *guard,
                                 uint8_t *dma_buffer,
                                 size_t dma_buffer_bytes);
/* Explicit fixed-profile FAT32 SFD format; caller controls FORMAT guard phase. */
esp_err_t h38_fatfs_format(h38_fatfs_adapter_t *adapter, FRESULT *result);
/* Initial mount only; does not format on failure. */
esp_err_t h38_fatfs_mount_initial(h38_fatfs_adapter_t *adapter,
                                  h38_fatfs_mount_info_t *info);
/* Clean software remount helpers for the reviewed IO module. */
esp_err_t h38_fatfs_unmount_cycle(h38_fatfs_adapter_t *adapter);
esp_err_t h38_fatfs_unmount_idle(h38_fatfs_adapter_t *adapter);
esp_err_t h38_fatfs_remount_existing(h38_fatfs_adapter_t *adapter,
                                     h38_fatfs_mount_info_t *info);
/* Final teardown after no FatFs call is in flight. */
esp_err_t h38_fatfs_adapter_deinit(h38_fatfs_adapter_t *adapter);
esp_err_t h38_fatfs_mount_info(h38_fatfs_adapter_t *adapter,
                               h38_fatfs_mount_info_t *info);
void h38_fatfs_get_counts(const h38_fatfs_adapter_t *adapter,
                          h38_fatfs_counts_t *counts);
const char *h38_fatfs_base_path(const h38_fatfs_adapter_t *adapter);

#endif
