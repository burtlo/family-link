/* H37 Stage A: bounded, read-only SDMMC metadata transport.
 * This fixture has no filesystem, NVS, Wi-Fi, or SD write API path. */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bsp/esp-box-3.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "sdmmc_cmd.h"

_Static_assert(BSP_SD_D0==9 && BSP_SD_D1==13 && BSP_SD_D2==42 && BSP_SD_D3==12 &&
               BSP_SD_CMD==14 && BSP_SD_CLK==11 && BSP_SD_POWER==43,
               "Pinned BOX-3 SDMMC pin contract changed; stop and review");
#ifndef H37_RUN_EPOCH
#error H37_RUN_EPOCH is required
#endif
#ifndef H35_RUN_EPOCH
#error H35_RUN_EPOCH is required
#endif

enum {
    CMD_MAX=256, RX_BYTES=512, TX_BYTES=8192, DMA_BYTES=4096,
    DISCOVERY_MS=45000, SESSION_MS=240000, COMMAND_MS=5000, IDLE_MS=15000,
    BYTE_BUDGET=131072, MAX_SECTORS=8, SECTOR_BYTES=512,
    POWER_GPIO=43
};
static const uint32_t EXPECTED_SECTORS=121503744u;
static const uint32_t EXPECTED_FREQ_KHZ=20000u;
static char elf_hex[65];
static bool usb_installed, host_initialized, power_configured, bound;
static bool host_deinit_attempted, power_off_attempted;
static int host_deinit_error, power_off_error;
static uint32_t read_count, charged_total, next_seq=1;
static int64_t start_us, last_activity_us, command_start_us;
static char line[CMD_MAX+1];
static uint8_t rx[RX_BYTES];
static uint8_t sector_data[DMA_BYTES] DMA_ATTR __attribute__((aligned(4)));
static char b64[5465];
static char tx_record[8192];

typedef enum { ST_NONE, ST_RESOURCES, ST_RESET, ST_POWER, ST_HOST, ST_SLOT,
    ST_CARD, ST_GEOMETRY, ST_BIND, ST_COMMAND, ST_READ, ST_TIMEOUT, ST_CLEANUP } stage_t;
static const char *stage_name(stage_t s)
{
    static const char *const names[]={"none","resources","reset","power","host","slot","card","geometry","bind","command","read","timeout","cleanup"};
    return names[s];
}
static void fail(stage_t stage, esp_err_t error);
static esp_err_t emit_record(const char *event, const char *fields)
{
    if (printf("H37,1,%s,epoch=%s,elf_sha256=%s%s%s\n",event,H37_RUN_EPOCH,elf_hex,
               fields && *fields ? "," : "",fields ? fields : "")<0) return ESP_FAIL;
    if (fflush(stdout)!=0) return ESP_FAIL;
    if (usb_installed && usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(15000))!=ESP_OK) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}
#define EMIT(ev, ...) do { char _f[1024]; int _n=snprintf(_f,sizeof(_f),__VA_ARGS__); if (_n<0 || (size_t)_n>=sizeof(_f)) fail(ST_RESOURCES,ESP_ERR_INVALID_SIZE); if (emit_record((ev),_f)!=ESP_OK) fail(ST_TIMEOUT,ESP_ERR_TIMEOUT); } while (0)

static void make_elf_hex(void)
{
    const esp_app_desc_t *app=esp_app_get_description();
    for (int i=0;i<32;i++) snprintf(elf_hex+2*i,3,"%02x",app->app_elf_sha256[i]);
}
static bool hex_lower(const char *s, size_t n)
{
    if (strlen(s)!=n) return false;
    for (size_t i=0;i<n;i++) if (!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return false;
    return true;
}
static bool decimal(const char *s, uint32_t *out)
{
    if (!s || !*s) return false;
    uint32_t n=0;
    for (;*s;s++) { if (*s<'0'||*s>'9') return false; unsigned d=(unsigned)(*s-'0'); if (n>(UINT32_MAX-d)/10u) return false; n=n*10u+d; }
    *out=n; return true;
}
static bool split_fields(char *s, char **out, size_t cap, size_t *count)
{
    size_t n=0; char *start=s;
    for (char *p=s;;p++) {
        if (*p==',' || *p=='\0') {
            if (p==start || n==cap) return false;
            out[n++]=start;
            if (*p=='\0') break;
            *p='\0'; start=p+1;
        }
    }
    *count=n; return true;
}
static esp_err_t read_line(char *dst, size_t cap, uint32_t command_ms, uint32_t idle_ms, int64_t *last_rx)
{
    size_t used=0; command_start_us=0;
    while (1) {
        int64_t now=esp_timer_get_time();
        if ((now-start_us)/1000>=SESSION_MS) return ESP_ERR_TIMEOUT;
        if ((now-*last_rx)/1000>=idle_ms || (used && (now-command_start_us)/1000>=command_ms)) return ESP_ERR_TIMEOUT;
        int n=usb_serial_jtag_read_bytes(rx,sizeof(rx),0);
        if (n<0) return ESP_FAIL;
        if (!n) { vTaskDelay(1); continue; }
        if (!used) command_start_us=now;
        *last_rx=now;
        for (int i=0;i<n;i++) {
            unsigned char c=rx[i];
            if (c=='\n') { if (i+1<n || used+1>CMD_MAX || used+1>=cap) return ESP_ERR_INVALID_SIZE; dst[used++]='\n'; dst[used]='\0'; return ESP_OK; }
            if (c<0x20 || c>0x7e || used+2>CMD_MAX || used+1>=cap) return ESP_ERR_INVALID_SIZE;
            dst[used++]=(char)c;
        }
    }
}
static int32_t cid_digest(const sdmmc_card_t *card, unsigned char digest[32])
{
    char canonical[160];
    int n=snprintf(canonical,sizeof(canonical),"%s:%08x:%08x:%.*s:%08x:%08x:%08x",
        H35_RUN_EPOCH,card->cid.mfg_id,card->cid.oem_id,(int)sizeof(card->cid.name),card->cid.name,
        card->cid.revision,card->cid.serial,card->cid.date);
    int32_t rc=ESP_FAIL;
    if (n>0 && n<(int)sizeof(canonical) && mbedtls_sha256((const unsigned char *)canonical,(size_t)n,digest,0)==0) rc=ESP_OK;
    memset(canonical,0,sizeof(canonical)); return rc;
}
static void emit_cid(const sdmmc_card_t *card)
{
    char name_hex[33]; const size_t name_len=sizeof(card->cid.name);
    _Static_assert(sizeof(((sdmmc_card_t *)0)->cid.name)<=16,"CID name must fit the H37 private record");
    for (size_t i=0;i<name_len;i++) snprintf(name_hex+2*i,3,"%02x",(unsigned char)card->cid.name[i]);
    name_hex[2*name_len]='\0';
    EMIT("CID_PRIVATE","mfg_id=%" PRIu32 ",oem_id=%" PRIu32 ",revision=%" PRIu32 ",serial=%" PRIu32 ",date=%" PRIu32 ",name_size=%u,name_hex=%s",
        (uint32_t)card->cid.mfg_id,(uint32_t)card->cid.oem_id,(uint32_t)card->cid.revision,(uint32_t)card->cid.serial,(uint32_t)card->cid.date,(unsigned)name_len,name_hex);
    memset(name_hex,0,sizeof(name_hex));
}
static void finish(stage_t *stage, esp_err_t *error)
{
    esp_err_t prior=*error;
    if (host_initialized) {
        host_deinit_attempted=true;
        host_deinit_error=sdmmc_host_deinit();
        host_initialized=false;
        if (host_deinit_error!=ESP_OK) { *stage=ST_CLEANUP; *error=(esp_err_t)host_deinit_error; }
    }
    if (power_configured) {
        power_off_attempted=true;
        power_off_error=gpio_set_level(POWER_GPIO,1);
        if (power_off_error!=ESP_OK) { *stage=ST_CLEANUP; *error=(esp_err_t)power_off_error; }
    }
    if (*stage==ST_NONE && *error==ESP_OK) *error=prior;
    char fields[128];
    snprintf(fields,sizeof(fields),"host_deinit_attempted=%u,deinit_error=%d,power_off_attempted=%u,power_off_error=%d",
        (unsigned)host_deinit_attempted,host_deinit_error,(unsigned)power_off_attempted,power_off_error);
    if (emit_record("CLEANUP",fields)!=ESP_OK) { *stage=ST_TIMEOUT; *error=ESP_ERR_TIMEOUT; }
}
static void terminal(stage_t stage, int error)
{
    const bool ok=(stage==ST_NONE && error==ESP_OK && bound && host_deinit_attempted && host_deinit_error==ESP_OK && power_off_attempted && power_off_error==ESP_OK);
    char fields[256];
    snprintf(fields,sizeof(fields),"result=%s,failure_stage=%s,error=%d,read_count=%" PRIu32 ",charged_total=%" PRIu32 ",bound=%u,scope=classification_only,media_writes=0",
        ok?"read_complete":"failed",stage_name(ok?ST_NONE:(stage==ST_NONE?ST_CLEANUP:stage)),ok?0:(error?error:ESP_FAIL),read_count,charged_total,(unsigned)bound);
    (void)emit_record("COMPLETE",fields);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
static void fail(stage_t stage, esp_err_t error)
{
    finish(&stage,&error);
    terminal(stage,error);
}
static esp_err_t init_usb(void)
{
    usb_serial_jtag_driver_config_t cfg={.rx_buffer_size=RX_BYTES,.tx_buffer_size=TX_BYTES};
    esp_err_t err=usb_serial_jtag_driver_install(&cfg);
    if (err==ESP_OK) { usb_installed=true; usb_serial_jtag_vfs_use_driver(); }
    return err;
}
static esp_err_t handle_bind(char **f, sdmmc_card_t *card, int64_t *last_rx)
{
    if (bound || strcmp(f[0],"H37C") || strcmp(f[1],"1") || strcmp(f[2],"BIND") ||
        strcmp(f[3],H37_RUN_EPOCH) || strcmp(f[4],elf_hex) ||
        !hex_lower(f[3],32) || !hex_lower(f[4],64) || !hex_lower(f[5],32) || !hex_lower(f[6],64)) return ESP_ERR_INVALID_ARG;
    if (strcmp(f[5],H35_RUN_EPOCH)) {
        EMIT("IDENTITY_MATCH","reference_epoch=%s,match=0,error=%d",f[5],ESP_ERR_INVALID_ARG);
        return ESP_ERR_INVALID_ARG;
    }
    unsigned char digest[32]; char digest_hex[65];
    esp_err_t err=(esp_err_t)cid_digest(card,digest);
    if (err==ESP_OK) {
        for (int i=0;i<32;i++) snprintf(digest_hex+2*i,3,"%02x",digest[i]);
        digest_hex[64]='\0';
        bound=(memcmp(digest_hex,f[6],64)==0);
        memset(digest,0,sizeof(digest)); memset(digest_hex,0,sizeof(digest_hex));
        EMIT("IDENTITY_MATCH","reference_epoch=%s,match=%u,error=%d",H35_RUN_EPOCH,(unsigned)bound,bound?ESP_OK:ESP_ERR_INVALID_CRC);
        if (!bound) return ESP_ERR_INVALID_CRC;
        *last_rx=esp_timer_get_time();
    }
    return err;
}
static bool command(char *dst, char **fields, size_t *n, uint32_t command_ms, int64_t *last_rx, esp_err_t *read_error)
{
    esp_err_t err=read_line(dst,CMD_MAX+1,command_ms,IDLE_MS,last_rx);
    if (read_error) *read_error=err;
    if (err!=ESP_OK) return false;
    size_t len=strlen(dst);
    if (!len || dst[len-1]!='\n') return false;
    dst[len-1]='\0';
    return split_fields(dst,fields,9,n);
}
void app_main(void)
{
    make_elf_hex(); start_us=esp_timer_get_time(); last_activity_us=start_us;
    esp_err_t err=init_usb();
    EMIT("BOOT","reset_reason=%d",(int)esp_reset_reason());
    EMIT("TRANSPORT","profile=sdmmc_metadata_v1,backend=sdmmc,mode=read_only,console=usb_serial_jtag,usb_host=disabled,slot=0,width_requested=4,max_freq_khz=20000,command_timeout_ms=1000,byte_budget=131072,max_request_sectors=8,command_line_max=256,record_max=8192,session_ms=240000,command_ms=5000,idle_ms=15000,discovery_ms=45000,usb_rx_bytes=512,usb_tx_bytes=8192");
    if (err!=ESP_OK) { stage_t s=ST_RESOURCES; fail(s,err); }
    if (esp_reset_reason()==ESP_RST_INT_WDT || esp_reset_reason()==ESP_RST_TASK_WDT || esp_reset_reason()==ESP_RST_WDT) { stage_t s=ST_RESET; fail(s,ESP_FAIL); }
    if ((esp_timer_get_time()-start_us)/1000>=DISCOVERY_MS) { stage_t s=ST_TIMEOUT; fail(s,ESP_ERR_TIMEOUT); }

    gpio_config_t power={.pin_bit_mask=1ULL<<POWER_GPIO,.mode=GPIO_MODE_OUTPUT};
    err=gpio_config(&power);
    if (err==ESP_OK) { power_configured=true; err=gpio_set_level(POWER_GPIO,0); }
    EMIT("POWER","gpio=43,active_level=0,error=%d",err);
    if (err!=ESP_OK) { stage_t s=ST_POWER; fail(s,err); }
    vTaskDelay(pdMS_TO_TICKS(100));
    if ((esp_timer_get_time()-start_us)/1000>=DISCOVERY_MS) { stage_t s=ST_TIMEOUT; fail(s,ESP_ERR_TIMEOUT); }
    sdmmc_host_t host=SDMMC_HOST_DEFAULT(); host.slot=SDMMC_HOST_SLOT_0;
    host.max_freq_khz=(int)EXPECTED_FREQ_KHZ; host.command_timeout_ms=1000;
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk=BSP_SD_CLK; slot.cmd=BSP_SD_CMD; slot.d0=BSP_SD_D0;
    slot.d1=BSP_SD_D1; slot.d2=BSP_SD_D2; slot.d3=BSP_SD_D3;
    slot.width=4; slot.cd=SDMMC_SLOT_NO_CD; slot.wp=SDMMC_SLOT_NO_WP; slot.flags=0;
    err=sdmmc_host_init(); host_initialized=(err==ESP_OK);
    EMIT("HOST","error=%d",err);
    if (err!=ESP_OK) { stage_t s=ST_HOST; fail(s,err); }
    if ((esp_timer_get_time()-start_us)/1000>=DISCOVERY_MS) { stage_t s=ST_TIMEOUT; fail(s,ESP_ERR_TIMEOUT); }
    err=sdmmc_host_init_slot(host.slot,&slot);
    EMIT("SLOT","error=%d",err);
    if (err!=ESP_OK) { stage_t s=ST_SLOT; fail(s,err); }
    if ((esp_timer_get_time()-start_us)/1000>=DISCOVERY_MS) { stage_t s=ST_TIMEOUT; fail(s,ESP_ERR_TIMEOUT); }
    sdmmc_card_t card={0}; err=sdmmc_card_init(&host,&card);
    EMIT("CARD","error=%d",err);
    if (err!=ESP_OK) { stage_t s=ST_CARD; fail(s,err); }
    unsigned width=(unsigned)host.get_bus_width(host.slot);
    uint64_t capacity=(uint64_t)card.csd.capacity*(uint64_t)card.csd.sector_size;
    EMIT("GEOMETRY","sectors=%" PRIu32 ",sector_bytes=%d,capacity_bytes=%" PRIu64 ",bus_width=%u,real_freq_khz=%d,ddr=%u",
        (uint32_t)card.csd.capacity,card.csd.sector_size,capacity,width,card.real_freq_khz,(unsigned)card.is_ddr);
    if ((esp_timer_get_time()-start_us)/1000>DISCOVERY_MS) { stage_t s=ST_TIMEOUT; fail(s,ESP_ERR_TIMEOUT); }
    if (!card.is_mem || card.is_mmc || card.is_sdio || card.csd.capacity!=EXPECTED_SECTORS ||
        card.csd.sector_size!=SECTOR_BYTES || width!=4 || card.real_freq_khz!=(int)EXPECTED_FREQ_KHZ) { stage_t s=ST_GEOMETRY; fail(s,ESP_ERR_INVALID_SIZE); }
    emit_cid(&card);
    if ((esp_timer_get_time()-start_us)/1000>=DISCOVERY_MS) { stage_t s=ST_TIMEOUT; fail(s,ESP_ERR_TIMEOUT); }
    last_activity_us=esp_timer_get_time();
    EMIT("READY","accepts=BIND");
    if ((esp_timer_get_time()-start_us)/1000>=DISCOVERY_MS) { stage_t s=ST_TIMEOUT; fail(s,ESP_ERR_TIMEOUT); }
    char *f[9]; size_t nf=0;
    err=ESP_OK;
    if (!command(line,f,&nf,COMMAND_MS,&last_activity_us,&err) || nf!=7) { stage_t s=(err==ESP_ERR_TIMEOUT)?ST_TIMEOUT:ST_COMMAND; fail(s,err==ESP_OK?ESP_ERR_INVALID_ARG:err); }
    err=handle_bind(f,&card,&last_activity_us);
    if (err!=ESP_OK) { stage_t s=ST_BIND; fail(s,err); }
    while (1) {
        if ((esp_timer_get_time()-start_us)/1000>=SESSION_MS) { stage_t s=ST_TIMEOUT; fail(s,ESP_ERR_TIMEOUT); }
        err=ESP_OK;
        if (!command(line,f,&nf,COMMAND_MS,&last_activity_us,&err)) { stage_t s=(err==ESP_ERR_TIMEOUT)?ST_TIMEOUT:ST_COMMAND; fail(s,err); }
        if ((nf!=6 && nf!=8) || strcmp(f[0],"H37C") || strcmp(f[1],"1") || strcmp(f[3],H37_RUN_EPOCH) || strcmp(f[4],elf_hex)) { stage_t s=ST_COMMAND; fail(s,ESP_ERR_INVALID_ARG); }
        if (!strcmp(f[2],"READ")) {
            uint32_t seq,lba,count;
            if (nf!=8 || !decimal(f[5],&seq) || !decimal(f[6],&lba) || !decimal(f[7],&count)) { stage_t s=ST_COMMAND; fail(s,ESP_ERR_INVALID_ARG); }
            read_count++;
            if (count>0 && count<=MAX_SECTORS) charged_total+=count*SECTOR_BYTES;
            else if (count>MAX_SECTORS) charged_total=BYTE_BUDGET+1;
            if (seq!=next_seq || count<1 || count>MAX_SECTORS || lba>card.csd.capacity || count>card.csd.capacity-lba) { stage_t s=ST_COMMAND; fail(s,ESP_ERR_INVALID_ARG); }
            uint32_t bytes=count*SECTOR_BYTES;
            if (bytes>DMA_BYTES || charged_total>BYTE_BUDGET) { stage_t s=ST_COMMAND; fail(s,ESP_ERR_INVALID_SIZE); }
            if ((esp_timer_get_time()-start_us)/1000>=SESSION_MS) { stage_t s=ST_TIMEOUT; fail(s,ESP_ERR_TIMEOUT); }
            next_seq++;
            int64_t begin=esp_timer_get_time();
            err=sdmmc_read_sectors(&card,sector_data,lba,count);
            int64_t elapsed=esp_timer_get_time()-begin;
            if (err==ESP_OK && (elapsed/1000>COMMAND_MS || (esp_timer_get_time()-command_start_us)/1000>COMMAND_MS || (esp_timer_get_time()-start_us)/1000>SESSION_MS)) err=ESP_ERR_TIMEOUT;
            if (err!=ESP_OK) {
                char fields[192]; snprintf(fields,sizeof(fields),"seq=%" PRIu32 ",lba=%" PRIu32 ",count=%" PRIu32 ",charged_total=%" PRIu32 ",error=%d,elapsed_us=%" PRIi64 ",len=0,sha256=none,b64=none",
                    seq,lba,count,charged_total,err,elapsed);
                (void)emit_record("READ_RESULT",fields); stage_t s=(err==ESP_ERR_TIMEOUT)?ST_TIMEOUT:ST_READ; fail(s,err);
            }
            unsigned char hash[32]; char hash_hex[65]; size_t encoded=0;
            if (mbedtls_sha256(sector_data,bytes,hash,0)!=0 ||
                mbedtls_base64_encode((unsigned char *)b64,sizeof(b64),&encoded,sector_data,bytes)!=0 || encoded>5464) { stage_t s=ST_RESOURCES; fail(s,ESP_FAIL); }
            for (int i=0;i<32;i++) snprintf(hash_hex+2*i,3,"%02x",hash[i]); hash_hex[64]='\0';
            char prefix[256]; int plen=snprintf(prefix,sizeof(prefix),"seq=%" PRIu32 ",lba=%" PRIu32 ",count=%" PRIu32 ",charged_total=%" PRIu32 ",error=0,elapsed_us=%" PRIi64 ",len=%u,sha256=%s,b64=",
                seq,lba,count,charged_total,elapsed,(unsigned)bytes,hash_hex);
            int head=snprintf(tx_record,sizeof(tx_record),"H37,1,READ_RESULT,epoch=%s,elf_sha256=%s,%s",H37_RUN_EPOCH,elf_hex,prefix);
            if (plen<0 || head<0 || (size_t)head+encoded+2>sizeof(tx_record)) { stage_t s=ST_RESOURCES; fail(s,ESP_ERR_INVALID_SIZE); }
            memcpy(tx_record+head,b64,encoded); tx_record[head+encoded]='\n'; tx_record[head+encoded+1]='\0';
            size_t tx_len=(size_t)head+encoded+1;
            if (usb_serial_jtag_write_bytes(tx_record,tx_len,0)!=(int)tx_len ||
                usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(15000))!=ESP_OK) { stage_t s=ST_TIMEOUT; fail(s,ESP_ERR_TIMEOUT); }
            memset(hash,0,sizeof(hash)); memset(hash_hex,0,sizeof(hash_hex)); memset(b64,0,encoded);
            continue;
        }
        if (!strcmp(f[2],"FINISH")) {
            uint32_t seq;
            if (nf!=6 || !decimal(f[5],&seq) || seq!=next_seq) { stage_t s=ST_COMMAND; fail(s,ESP_ERR_INVALID_ARG); }
            stage_t s=ST_NONE; err=ESP_OK; finish(&s,&err); terminal(s,err);
        }
        { stage_t s=ST_COMMAND; fail(s,ESP_ERR_INVALID_ARG); }
    }
}
