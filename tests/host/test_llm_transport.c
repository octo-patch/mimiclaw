/* Offline transport test: compile the real LLM implementation with ESP/NVS mocks.
 * See run.sh. No provider credentials or network access are used. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../main/llm/llm_proxy.c"

static cJSON *storage;
static esp_http_client_config_t http_config;
static char captured_url[256], captured_headers[2048], captured_body[16384];
static bool use_proxy, proxy_read_done;
static const char *reply =
    "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{"
    "\"role\":\"assistant\",\"content\":\"<think>keep tags</think>checking\","
    "\"reasoning_content\":\"keep reasoning exactly\",\"tool_calls\":[{"
    "\"id\":\"call_1\",\"type\":\"function\",\"function\":{"
    "\"name\":\"lookup\",\"arguments\":\"{\\\"q\\\":\\\"test\\\"}\"}}]}}]}";

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{ (void)name; (void)mode; *handle = 1; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *len)
{
    (void)handle;
    cJSON *v = cJSON_GetObjectItem(storage, key);
    if (!v) return ESP_FAIL;
    assert(strlen(v->valuestring) < *len);
    strcpy(out, v->valuestring);
    *len = strlen(out) + 1;
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    (void)handle;
    cJSON_DeleteItemFromObject(storage, key);
    cJSON_AddStringToObject(storage, key, value);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; return ESP_OK; }
void nvs_close(nvs_handle_t handle) { (void)handle; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    http_config = *config;
    snprintf(captured_url, sizeof(captured_url), "%s", config->url);
    captured_headers[0] = '\0';
    return &http_config;
}
int esp_http_client_set_method(esp_http_client_handle_t client, int method)
{ (void)client; assert(method == HTTP_METHOD_POST); return ESP_OK; }
int esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value)
{
    (void)client;
    size_t n = strlen(captured_headers);
    snprintf(captured_headers + n, sizeof(captured_headers) - n, "%s: %s\r\n", key, value);
    return ESP_OK;
}
int esp_http_client_set_post_field(esp_http_client_handle_t client, const char *body, int len)
{
    (void)client;
    assert(len == (int)strlen(body) && len < (int)sizeof(captured_body));
    strcpy(captured_body, body);
    return ESP_OK;
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    (void)client;
    esp_http_client_event_t event = {
        .event_id = HTTP_EVENT_ON_DATA, .user_data = http_config.user_data,
        .data = (void *)reply, .data_len = (int)strlen(reply),
    };
    return http_config.event_handler(&event);
}
int esp_http_client_get_status_code(esp_http_client_handle_t client)
{ (void)client; return 200; }
int esp_http_client_cleanup(esp_http_client_handle_t client)
{ (void)client; return ESP_OK; }

bool http_proxy_is_enabled(void) { return use_proxy; }
proxy_conn_t *proxy_conn_open(const char *host, int port, int timeout_ms)
{
    assert(port == 443 && timeout_ms > 0);
    snprintf(captured_url, sizeof(captured_url), "https://%s/v1/chat/completions", host);
    captured_headers[0] = '\0';
    proxy_read_done = false;
    return (proxy_conn_t *)&http_config;
}
int proxy_conn_write(proxy_conn_t *conn, const char *data, int len)
{
    (void)conn;
    char *target = captured_headers[0] ? captured_body : captured_headers;
    assert(len < (int)(target == captured_body ? sizeof(captured_body) : sizeof(captured_headers)));
    memcpy(target, data, len);
    target[len] = '\0';
    return len;
}
int proxy_conn_read(proxy_conn_t *conn, char *buf, int len, int timeout_ms)
{
    (void)conn; (void)timeout_ms;
    if (proxy_read_done) return 0;
    proxy_read_done = true;
    int n = snprintf(buf, len, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", reply);
    assert(n < len);
    return n;
}
void proxy_conn_close(proxy_conn_t *conn) { (void)conn; }

static void reset(void)
{
    cJSON_Delete(storage);
    storage = cJSON_CreateObject();
    strcpy(s_provider, MIMI_LLM_PROVIDER_DEFAULT);
    strcpy(s_model, MIMI_LLM_DEFAULT_MODEL);
    strcpy(s_minimax_region, MIMI_MINIMAX_REGION_DEFAULT);
    s_api_key[0] = '\0';
    s_model_configured = false;
}

static cJSON *send_request(cJSON *messages, const char *model, const char *host)
{
    llm_response_t response;
    const char *tools = "[{\"name\":\"lookup\",\"input_schema\":{\"type\":\"object\"}}]";
    char *before = cJSON_PrintUnformatted(messages);
    assert(llm_chat_tools("system prompt", messages, tools, &response) == ESP_OK);
    assert(strstr(captured_url, host));
    assert(strstr(captured_headers, "Authorization: Bearer offline-fixture"));
    if (use_proxy) assert(strstr(captured_headers, "POST /v1/chat/completions HTTP/1.1"));
    cJSON *body = cJSON_Parse(captured_body);
    assert(body);
    assert(strcmp(cJSON_GetObjectItem(body, "model")->valuestring, model) == 0);
    assert(cJSON_GetObjectItem(body, "max_completion_tokens")->valueint == MIMI_LLM_MAX_TOKENS);
    assert(!cJSON_GetObjectItem(body, "thinking"));
    assert(!cJSON_GetObjectItem(body, "reasoning_effort"));
    assert(!cJSON_GetObjectItem(body, "reasoning_split"));
    assert(!cJSON_GetObjectItem(body, "service_tier"));
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(body, "tools")) == 1);
    assert(response.tool_use && response.call_count == 1);
    assert(strcmp(response.calls[0].id, "call_1") == 0);
    assert(strcmp(response.calls[0].input, "{\"q\":\"test\"}") == 0);
    if (provider_is_minimax()) {
        assert(response.assistant_message);
        cJSON *original = cJSON_Parse(reply);
        cJSON *message = cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(original, "choices"), 0), "message");
        assert(cJSON_Compare(message, response.assistant_message, 1));
        cJSON_Delete(original);
    } else {
        assert(!response.assistant_message);
    }
    char *after = cJSON_PrintUnformatted(messages);
    assert(strcmp(before, after) == 0);
    free(before); free(after);
    llm_response_free(&response);
    assert(!response.assistant_message);
    return body;
}

int main(void)
{
    const char *models[] = {NULL, "MiniMax-M3", "MiniMax-M2.7", "custom-model"};
    const char *regions[] = {"global_en", "cn_zh", "unknown"};
    cJSON *messages = cJSON_Parse("[{\"role\":\"user\",\"content\":\"hello\"}]");
    for (int proxy = 0; proxy < 2; proxy++) {
        use_proxy = proxy;
        for (size_t r = 0; r < 3; r++) {
            for (size_t m = 0; m < 4; m++) {
                reset();
                nvs_set_str(1, MIMI_NVS_KEY_PROVIDER, "minimax");
                nvs_set_str(1, MIMI_NVS_KEY_API_KEY, "offline-fixture");
                nvs_set_str(1, MIMI_NVS_KEY_MINIMAX_REGION, regions[r]);
                if (models[m]) nvs_set_str(1, MIMI_NVS_KEY_MODEL, models[m]);
                assert(llm_proxy_init() == ESP_OK);
                const char *expected = models[m] ? models[m] :
                    (MIMI_SECRET_MODEL[0] ? MIMI_SECRET_MODEL : "MiniMax-M3.1-Flash");
                cJSON_Delete(send_request(messages, expected, r == 1 ? "api.minimax.cn" : "api.minimax.io"));
                assert(llm_set_provider("openai") == ESP_OK);
                cJSON_Delete(send_request(messages, models[m] || MIMI_SECRET_MODEL[0] ? expected : MIMI_LLM_DEFAULT_MODEL, "api.openai.com"));
                assert(llm_set_provider("minimax") == ESP_OK);
                cJSON_Delete(send_request(messages, expected, r == 1 ? "api.minimax.cn" : "api.minimax.io"));
                assert(llm_set_model("explicit-cli-model") == ESP_OK);
                llm_set_provider("openai");
                llm_set_provider("minimax");
                cJSON_Delete(send_request(messages, "explicit-cli-model", r == 1 ? "api.minimax.cn" : "api.minimax.io"));
            }
        }
    }
    reset();
    llm_proxy_init();
    assert(strcmp(s_model, MIMI_SECRET_MODEL[0] ? MIMI_SECRET_MODEL :
        (strcmp(MIMI_SECRET_MODEL_PROVIDER, "minimax") == 0 ? "MiniMax-M3.1-Flash" : MIMI_LLM_DEFAULT_MODEL)) == 0);
    llm_set_api_key("offline-fixture");
    llm_set_provider("minimax");
    llm_set_minimax_region("cn_zh");
    use_proxy = false;
    llm_response_t response;
    assert(llm_chat_tools("system", messages, NULL, &response) == ESP_OK);
    cJSON_AddItemToArray(messages, response.assistant_message);
    response.assistant_message = NULL;
    llm_response_free(&response);
    cJSON_AddItemToArray(messages, cJSON_Parse("{\"role\":\"user\",\"content\":[{\"type\":\"tool_result\",\"tool_use_id\":\"call_1\",\"content\":\"found\"}]}"));
    cJSON *body = send_request(messages, MIMI_SECRET_MODEL[0] ? MIMI_SECRET_MODEL : "MiniMax-M3.1-Flash", "api.minimax.cn");
    cJSON *sent = cJSON_GetObjectItem(body, "messages");
    assert(cJSON_GetArraySize(sent) == 4);
    assert(cJSON_Compare(cJSON_GetArrayItem(messages, 1), cJSON_GetArrayItem(sent, 2), 1));
    assert(strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(sent, 3), "tool_call_id")->valuestring, "call_1") == 0);
    cJSON_Delete(body);
    cJSON_Delete(messages);

    /* Existing block-shaped history and non-MiniMax media must stay intact. */
    messages = cJSON_Parse("[{\"role\":\"assistant\",\"content\":[{\"type\":\"tool_use\",\"id\":\"old_call\",\"name\":\"lookup\",\"input\":{}}]}]");
    body = send_request(messages, MIMI_SECRET_MODEL[0] ? MIMI_SECRET_MODEL : "MiniMax-M3.1-Flash", "api.minimax.cn");
    cJSON *assistant = cJSON_GetArrayItem(cJSON_GetObjectItem(body, "messages"), 1);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(assistant, "tool_calls")) == 1);
    cJSON_Delete(body);
    cJSON_Delete(messages);
    messages = cJSON_Parse("[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"describe\"},{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/png\",\"data\":\"fixture\"}}]}]");
    llm_set_provider("anthropic");
    reply = "{\"stop_reason\":\"end_turn\",\"content\":[{\"type\":\"text\",\"text\":\"answer\"}]}";
    assert(llm_chat_tools("system", messages, NULL, &response) == ESP_OK);
    assert(strcmp(captured_url, MIMI_LLM_API_URL) == 0);
    assert(strstr(captured_headers, "x-api-key: offline-fixture"));
    assert(strstr(captured_headers, "anthropic-version:"));
    body = cJSON_Parse(captured_body);
    assert(cJSON_Compare(messages, cJSON_GetObjectItem(body, "messages"), 1));
    assert(cJSON_GetObjectItem(body, "max_tokens"));
    assert(!response.assistant_message && strcmp(response.text, "answer") == 0);
    llm_response_free(&response);
    cJSON_Delete(body);
    cJSON_Delete(messages);
    cJSON_Delete(storage);
    puts("PASS: direct/proxy requests, regions, defaults, build/NVS/CLI overrides, legacy/custom models, tool/reasoning history");
    return 0;
}
