/* Host mock configuration copied from the effective ESP-IDF 5.4.2 FatFs
 * settings; this header is test-only and does not configure firmware builds. */
#pragma once
#define CONFIG_FATFS_USE_FASTSEEK 0
#define CONFIG_FATFS_USE_LABEL 0
#define CONFIG_FATFS_CODEPAGE 437
#define CONFIG_FATFS_VOLUME_COUNT 2
#define CONFIG_FATFS_FS_LOCK 0
#define CONFIG_FATFS_TIMEOUT_MS 10000
#define CONFIG_FATFS_PER_FILE_CACHE 1
#define CONFIG_FATFS_DONT_TRUST_FREE_CLUSTER_CNT 0
#define CONFIG_FATFS_DONT_TRUST_LAST_ALLOC 0
#define CONFIG_FATFS_USE_DYN_BUFFERS 0
#define CONFIG_WL_SECTOR_SIZE 4096
