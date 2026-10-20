#pragma once
#include <assert.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_HTTP_CONNECT 3
#define ESP_ERR_HTTP_WRITE_DATA 4
#define ESP_ERROR_CHECK(call) assert((call) == ESP_OK)
static inline const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock"; }
