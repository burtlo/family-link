#include "lab02_platform/wifi.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

const char wifi_event_base[] = "WIFI_EVENT";
const char ip_event_base[] = "IP_EVENT";

struct esp_netif_stub {
    int unused;
};

struct event_group_stub {
    EventBits_t bits;
};

static const char *s_scenario;
static struct esp_netif_stub s_netif;
static struct event_group_stub s_event_group;
static int s_netif_create_calls;
static int s_wifi_init_calls;
static int s_handler_register_calls;
static int s_handler_unregister_calls;
static int s_set_mode_calls;
static int s_set_config_calls;
static int s_wifi_start_calls;
static int s_wifi_connect_calls;
static int s_wait_calls;

esp_err_t esp_netif_init(void)
{
    return ESP_OK;
}

esp_err_t esp_event_loop_create_default(void)
{
    return ESP_OK;
}

esp_netif_t *esp_netif_create_default_wifi_sta(void)
{
    assert(s_netif_create_calls == 0);
    s_netif_create_calls++;
    return &s_netif;
}

esp_err_t esp_wifi_init(const wifi_init_config_t *config)
{
    (void)config;
    s_wifi_init_calls++;
    if (strcmp(s_scenario, "init-fail") == 0 && s_wifi_init_calls == 1) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register(esp_event_base_t event_base, int32_t event_id,
                                              esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              esp_event_handler_instance_t *instance)
{
    (void)event_base;
    (void)event_id;
    (void)event_handler;
    (void)event_handler_arg;
    assert(instance != NULL);
    *instance = (void *)(uintptr_t)(s_handler_register_calls + 1);
    s_handler_register_calls++;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t event_base, int32_t event_id,
                                                esp_event_handler_instance_t instance)
{
    (void)event_base;
    (void)event_id;
    assert(instance != NULL);
    s_handler_unregister_calls++;
    return ESP_OK;
}

esp_err_t esp_wifi_set_mode(wifi_mode_t mode)
{
    (void)mode;
    s_set_mode_calls++;
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(wifi_interface_t interface, const wifi_config_t *config)
{
    (void)interface;
    (void)config;
    s_set_config_calls++;
    return ESP_OK;
}

esp_err_t esp_wifi_start(void)
{
    s_wifi_start_calls++;
    if (strcmp(s_scenario, "start-fail") == 0 && s_wifi_start_calls == 1) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t esp_wifi_connect(void)
{
    s_wifi_connect_calls++;
    return ESP_OK;
}

EventGroupHandle_t xEventGroupCreate(void)
{
    return &s_event_group;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits_to_wait_for,
                                int clear_on_exit, int wait_for_all, TickType_t ticks_to_wait)
{
    (void)group;
    (void)bits_to_wait_for;
    (void)clear_on_exit;
    (void)wait_for_all;
    if (ticks_to_wait == 0) {
        return 0;
    }
    return s_wait_calls++ == 0 ? 0 : BIT0;
}

void xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits_to_clear)
{
    group->bits &= ~bits_to_clear;
}

void xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits_to_set)
{
    group->bits |= bits_to_set;
}

static void assert_setup_once(void)
{
    assert(s_netif_create_calls == 1);
    assert(s_wifi_init_calls == 1);
    assert(s_handler_register_calls == 2);
    assert(s_handler_unregister_calls == 0);
    assert(s_set_mode_calls == 1);
    assert(s_set_config_calls == 1);
    assert(s_wifi_start_calls == 1);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    s_scenario = argv[1];

    if (strcmp(s_scenario, "lifecycle") == 0) {
        assert(!lab02_board_wifi_connect());
        assert(lab02_board_wifi_connect());
        assert_setup_once();
        assert(s_wifi_connect_calls == 1);
        puts("WIFI_LIFECYCLE_RESULT PASS");
        return 0;
    }

    if (strcmp(s_scenario, "init-fail") == 0) {
        assert(!lab02_board_wifi_connect());
        assert(!lab02_board_wifi_connect());
        assert(s_netif_create_calls == 1);
        assert(s_wifi_init_calls == 2);
        assert(s_handler_register_calls == 2);
        assert(s_wifi_start_calls == 1);
        puts("WIFI_INIT_ERROR_RESULT PASS");
        return 0;
    }

    if (strcmp(s_scenario, "start-fail") == 0) {
        assert(!lab02_board_wifi_connect());
        assert(!lab02_board_wifi_connect());
        assert(s_netif_create_calls == 1);
        assert(s_wifi_init_calls == 1);
        assert(s_handler_register_calls == 2);
        assert(s_handler_unregister_calls == 0);
        assert(s_set_mode_calls == 2);
        assert(s_set_config_calls == 2);
        assert(s_wifi_start_calls == 2);
        puts("WIFI_START_ERROR_RESULT PASS");
        return 0;
    }

    fprintf(stderr, "unknown scenario: %s\n", s_scenario);
    return EXIT_FAILURE;
}
