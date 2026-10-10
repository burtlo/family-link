/* Stage A discovery only. No filesystem, NVS, block writes or USB OTG host.
 * GPIOs/active-low power verified in pinned esp-box-3 3.2.0 header/source.
 * Card init configures the SD protocol; it does not read user data sectors.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "sdmmc_cmd.h"
#include "bsp/esp-box-3.h"

_Static_assert(BSP_SD_D0==9 && BSP_SD_D1==13 && BSP_SD_D2==42 && BSP_SD_D3==12 &&
               BSP_SD_CMD==14 && BSP_SD_CLK==11 && BSP_SD_POWER==43,
               "Pinned SENSOR SDMMC GPIO contract changed; stop and review");

#ifndef H35_RUN_EPOCH
#error H35_RUN_EPOCH must be provided by the isolated build
#endif
#define RECORD(event, fmt, ...) printf("H35,1," event ",epoch=" H35_RUN_EPOCH "," fmt "\n", ##__VA_ARGS__)
enum { POWER_GPIO=43, DISCOVERY_LIMIT_MS=30000 };
static TaskHandle_t discovery_task;
static TaskHandle_t deadline_task;

static void halt(void)
{
    fflush(stdout);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

static void deadline(void *unused)
{
    (void)unused;
    vTaskDelay(pdMS_TO_TICKS(DISCOVERY_LIMIT_MS));
    /* Suspend discovery before claiming a terminal record. No reset loop.
     * Do not deinitialize a driver whose transaction might be in flight. */
    /* Acquire the stream before suspension so a suspended printf cannot
     * leave the terminal writer waiting on its stdio lock. */
    flockfile(stdout);
    vTaskSuspend(discovery_task);
    RECORD("STOP", "reason=deadline,error=%d", ESP_ERR_TIMEOUT);
    RECORD("COMPLETE", "result=timeout,card_present=unproven,scope=discovery_only");
    funlockfile(stdout);
    halt();
}

void app_main(void)
{
    char elf_hash[65];
    const esp_app_desc_t *app=esp_app_get_description();
    for (int i=0;i<32;i++) snprintf(elf_hash+2*i,3,"%02x",app->app_elf_sha256[i]);
    const esp_reset_reason_t reset_reason=esp_reset_reason();
    RECORD("BOOT", "reset_reason=%d,elf_sha256=%s", (int)reset_reason,elf_hash);
    RECORD("TRANSPORT", "backend=sdmmc,mode=read_only,console=usb_serial_jtag,usb_host=disabled,slot=0,width_requested=4,max_freq_khz=20000,command_timeout_ms=1000,deadline_ms=30000");
    fflush(stdout);
    /* A preceding watchdog reset is failure evidence. Halt before configuring
     * power or the SDMMC host so a watchdog reboot cannot repeat discovery. */
    if (reset_reason==ESP_RST_INT_WDT || reset_reason==ESP_RST_TASK_WDT || reset_reason==ESP_RST_WDT) {
        RECORD("STOP", "reason=watchdog_reset,error=%d", ESP_FAIL);
        RECORD("COMPLETE", "result=error,card_present=unproven,scope=discovery_only");
        halt();
    }
    discovery_task=xTaskGetCurrentTaskHandle();
    if (xTaskCreate(deadline, "h35_deadline", 3072, NULL, 8, &deadline_task)!=pdPASS) {
        RECORD("STOP", "reason=allocation,error=%d", ESP_ERR_NO_MEM);
        RECORD("COMPLETE", "result=error,card_present=unproven,scope=discovery_only");
        halt();
    }
    gpio_config_t power={.pin_bit_mask=1ULL<<POWER_GPIO,.mode=GPIO_MODE_OUTPUT};
    esp_err_t err=gpio_config(&power);
    if (err==ESP_OK) err=gpio_set_level(POWER_GPIO,0);
    RECORD("POWER", "gpio=43,active_level=0,error=%d", err);
    bool initialized=false;
    if (err==ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        err=sdmmc_host_init();
        initialized=(err==ESP_OK);
        RECORD("HOST", "error=%d",err);
    }
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();
    host.slot=SDMMC_HOST_SLOT_0;
    host.max_freq_khz=SDMMC_FREQ_DEFAULT;
    host.command_timeout_ms=1000;
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk=BSP_SD_CLK; slot.cmd=BSP_SD_CMD; slot.d0=BSP_SD_D0;
    slot.d1=BSP_SD_D1; slot.d2=BSP_SD_D2; slot.d3=BSP_SD_D3;
    slot.width=4;
    slot.cd=SDMMC_SLOT_NO_CD; slot.wp=SDMMC_SLOT_NO_WP;
    /* Match BSP: external pullups, no speculative internal-pullup override. */
    slot.flags=0;
    if (err==ESP_OK) {
        err=sdmmc_host_init_slot(host.slot,&slot);
        RECORD("SLOT", "error=%d",err);
    }
    sdmmc_card_t card={0};
    if (err==ESP_OK) {
        err=sdmmc_card_init(&host,&card); /* Exactly one attempt. */
        RECORD("CARD", "error=%d",err);
    }
    /* No card-detect pin exists; timeout cannot establish physical absence. */
    const char *result="init_failed";
    if (err==ESP_OK && card.is_mem && !card.is_mmc && !card.is_sdio && card.csd.sector_size>0 && card.csd.capacity>0) {
        RECORD("GEOMETRY", "sectors=%" PRIu32 ",sector_bytes=%d,capacity_bytes=%" PRIu64 ",bus_width=%u,real_freq_khz=%d,card_max_freq_khz=%" PRIu32 ",ddr=%u",
            (uint32_t)card.csd.capacity,card.csd.sector_size,(uint64_t)card.csd.capacity*card.csd.sector_size,
            (unsigned)host.get_bus_width(host.slot),card.real_freq_khz,card.max_freq_khz,(unsigned)card.is_ddr);
        /* Epoch-salted canonical decoded CID hash; never expose raw identity.
         * Hash individual fields to avoid struct padding/ABI dependence. */
        char canonical[160];
        int n=snprintf(canonical,sizeof(canonical),"%s:%08x:%08x:%.*s:%08x:%08x:%08x",
            H35_RUN_EPOCH,card.cid.mfg_id,card.cid.oem_id,(int)sizeof(card.cid.name),card.cid.name,
            card.cid.revision,card.cid.serial,card.cid.date);
        unsigned char hash[32]; char hex[65];
        if (n<=0 || n>=(int)sizeof(canonical) || mbedtls_sha256((const unsigned char *)canonical,n,hash,0)!=0) {
            err=ESP_FAIL; result="error";
        } else {
            for (int i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",hash[i]);
            RECORD("IDENTITY", "scheme=epoch_sha256_decoded_cid_v1,sha256=%s",hex);
            result="detected";
        }
        memset(canonical,0,sizeof(canonical));
    } else if (err==ESP_OK) {
        err=ESP_ERR_NOT_SUPPORTED; result="unsupported";
    }
    esp_err_t deinit=initialized ? sdmmc_host_deinit() : ESP_OK;
    esp_err_t power_off=gpio_set_level(POWER_GPIO,1);
    vTaskDelete(deadline_task);
    RECORD("STOP", "reason=finished,error=%d,deinit_error=%d,power_off_error=%d",err,deinit,power_off);
    if (deinit!=ESP_OK || power_off!=ESP_OK) result="error";
    RECORD("COMPLETE", "result=%s,card_present=%s,scope=discovery_only",result,
           strcmp(result,"detected")==0 ? "proven" : "unproven");
    halt();
}
