#pragma once
#include "esp_err.h"
typedef struct {
    int event_id;
    void *user_data;
    void *data;
    int data_len;
} esp_http_client_event_t;
typedef struct {
    const char *url;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
    int timeout_ms, buffer_size, buffer_size_tx;
    int (*crt_bundle_attach)(void *);
} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
#define HTTP_EVENT_ON_DATA 1
#define HTTP_METHOD_POST 1
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
int esp_http_client_set_method(esp_http_client_handle_t client, int method);
int esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);
int esp_http_client_set_post_field(esp_http_client_handle_t client, const char *body, int len);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int esp_http_client_cleanup(esp_http_client_handle_t client);
