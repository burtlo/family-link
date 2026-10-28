#include "lab12_fixture_client.h"
#include "sdkconfig.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

#ifndef CONFIG_LAB12_SERVER_URL
#define CONFIG_LAB12_SERVER_URL "http://127.0.0.1:8771"
#endif
#ifndef CONFIG_LAB12_WIFI_SSID
#define CONFIG_LAB12_WIFI_SSID ""
#endif
#ifndef CONFIG_LAB12_WIFI_PASSWORD
#define CONFIG_LAB12_WIFI_PASSWORD ""
#endif
#define WIFI_READY BIT0
#define WIFI_FAILED BIT1
#define RESPONSE_CAPACITY 2048
static EventGroupHandle_t wifi_events;
static esp_netif_t *station;
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(wifi_events, WIFI_READY);
}
bool lab12_fixture_wifi_connect(void)
{
    if (CONFIG_LAB12_WIFI_SSID[0] == '\0')
        return false;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
        return false;
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return false;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return false;
    station = esp_netif_create_default_wifi_sta();
    if (!station)
        return false;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK)
        return false;
    wifi_events = xEventGroupCreate();
    if (!wifi_events)
        return false;
    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL) != ESP_OK || esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL) != ESP_OK)
        return false;
    wifi_config_t cfg = {0};
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", CONFIG_LAB12_WIFI_SSID);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", CONFIG_LAB12_WIFI_PASSWORD);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK || esp_wifi_start() != ESP_OK || esp_wifi_connect() != ESP_OK)
        return false;
    EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_READY | WIFI_FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    return (bits & WIFI_READY) != 0;
}
typedef struct {
    char data[RESPONSE_CAPACITY];
    size_t used;
} response_t;
static esp_err_t on_http(esp_http_client_event_t *event)
{
    response_t *r = (response_t *)event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && r && event->data_len > 0) {
        if (r->used + (size_t)event->data_len >= sizeof(r->data))
            return ESP_FAIL;
        memcpy(r->data + r->used, event->data, (size_t)event->data_len);
        r->used += (size_t)event->data_len;
        r->data[r->used] = '\0';
    }
    return ESP_OK;
}
static int request(const char *method, const char *path, const char *body, response_t *response)
{
    char url[256];
    if (snprintf(url, sizeof(url), "%s%s", CONFIG_LAB12_SERVER_URL, path) >= (int)sizeof(url))
        return -1;
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 3000, .event_handler = on_http, .user_data = response};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return -1;
    esp_http_client_set_method(client, strcmp(method, "PATCH") == 0 ? HTTP_METHOD_PATCH : HTTP_METHOD_GET);
    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, (int)strlen(body));
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);
    return status;
}
static bool read_profile(const char *json, lab12_profile_t *p)
{
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;
    cJSON *u = cJSON_GetObjectItemCaseSensitive(root, "user_id"), *r = cJSON_GetObjectItemCaseSensitive(root, "revision"), *a = cJSON_GetObjectItemCaseSensitive(root, "avatar_id"), *c = cJSON_GetObjectItemCaseSensitive(root, "profile_accent");
    bool ok = cJSON_IsString(u) && cJSON_IsNumber(r) && cJSON_IsString(a) && cJSON_IsString(c);
    if (ok) {
        memset(p, 0, sizeof(*p));
        snprintf(p->user_id, sizeof(p->user_id), "%s", u->valuestring);
        p->revision = (uint32_t)r->valuedouble;
        snprintf(p->avatar_id, sizeof(p->avatar_id), "%s", a->valuestring);
        snprintf(p->profile_accent, sizeof(p->profile_accent), "%s", c->valuestring);
    }
    cJSON_Delete(root);
    return ok;
}
bool lab12_fixture_get_profile(const char *endpoint, lab12_profile_t *p)
{
    char path[64];
    response_t response = {0};
    snprintf(path, sizeof(path), "/profile/%s", endpoint);
    int status = request("GET", path, NULL, &response);
    return status == 200 && read_profile(response.data, p);
}
int lab12_fixture_patch(const char *endpoint, const lab12_profile_update_t *u, lab12_profile_t *canonical, uint32_t *conflict_revision, bool reject, bool drop_response)
{
    cJSON *root = cJSON_CreateObject(), *changes = cJSON_CreateObject();
    char *body = NULL;
    response_t response = {0};
    char path[64], op[48];
    snprintf(op, sizeof(op), "device-%s-%lu", u->user_id, (unsigned long)u->operation_id);
    cJSON_AddStringToObject(root, "operation_id", op);
    cJSON_AddNumberToObject(root, "expected_revision", u->expected_revision);
    if (reject)
        cJSON_AddBoolToObject(root, "fixture_reject", true);
    if (drop_response)
        cJSON_AddBoolToObject(root, "fixture_drop_response", true);
    if (u->change_avatar)
        cJSON_AddStringToObject(changes, "avatar_id", u->avatar_id);
    if (u->change_accent)
        cJSON_AddStringToObject(changes, "profile_accent", u->profile_accent);
    cJSON_AddItemToObject(root, "changes", changes);
    body = cJSON_PrintUnformatted(root);
    snprintf(path, sizeof(path), "/profile/%s", endpoint);
    int status = body ? request("PATCH", path, body, &response) : -1;
    if (status == 200 && canonical)
        read_profile(response.data, canonical);
    if (status == 409 && conflict_revision) {
        cJSON *res = cJSON_Parse(response.data);
        cJSON *r = res ? cJSON_GetObjectItemCaseSensitive(res, "revision") : NULL;
        if (cJSON_IsNumber(r))
            *conflict_revision = (uint32_t)r->valuedouble;
        if (res)
            cJSON_Delete(res);
    }
    if (body)
        cJSON_free(body);
    cJSON_Delete(root);
    return status;
}
bool lab12_fixture_get_probes(char *reject, unsigned nr, char *offline, unsigned no, char *layout, unsigned nl, char *message, unsigned nm)
{
    response_t response = {0};
    if (request("GET", "/fixtures", NULL, &response) != 200)
        return false;
    cJSON *root = cJSON_Parse(response.data);
    cJSON *r = root ? cJSON_GetObjectItemCaseSensitive(root, "reject_preview") : NULL, *o = root ? cJSON_GetObjectItemCaseSensitive(root, "offline_probe") : NULL, *l = root ? cJSON_GetObjectItemCaseSensitive(root, "layout") : NULL, *m = root ? cJSON_GetObjectItemCaseSensitive(root, "message_color") : NULL;
    cJSON *rv = r ? cJSON_GetObjectItemCaseSensitive(r, "reject_preview") : NULL, *ov = o ? cJSON_GetObjectItemCaseSensitive(o, "work") : NULL, *lv = l ? cJSON_GetObjectItemCaseSensitive(l, "layout") : NULL, *mv = m ? cJSON_GetObjectItemCaseSensitive(m, "observations") : NULL;
    bool ok = cJSON_IsString(rv) && cJSON_IsString(ov) && cJSON_IsString(lv) && cJSON_IsArray(mv) && cJSON_IsString(cJSON_GetArrayItem(mv, 0)) && cJSON_IsString(cJSON_GetArrayItem(mv, 1));
    if (ok) {
        snprintf(reject, nr, "%s", rv->valuestring);
        snprintf(offline, no, "%s", ov->valuestring);
        snprintf(layout, nl, "%s", lv->valuestring);
        snprintf(message, nm, "%s,%s", cJSON_GetArrayItem(mv, 0)->valuestring, cJSON_GetArrayItem(mv, 1)->valuestring);
    }
    if (root)
        cJSON_Delete(root);
    return ok;
}
bool lab12_fixture_get_stats(uint32_t *side_effects)
{
    response_t response = {0};
    if (request("GET", "/stats", NULL, &response) != 200)
        return false;
    cJSON *root = cJSON_Parse(response.data);
    cJSON *n = root ? cJSON_GetObjectItemCaseSensitive(root, "side_effect_count") : NULL;
    bool ok = cJSON_IsNumber(n);
    if (ok && side_effects)
        *side_effects = (uint32_t)n->valuedouble;
    if (root)
        cJSON_Delete(root);
    return ok;
}
