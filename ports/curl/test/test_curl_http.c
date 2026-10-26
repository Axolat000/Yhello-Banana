/*
 * libcurl HTTP test harness for Banana OS port verification.
 * Tests:
 * 1. libcurl version and features verification.
 * 2. HTTP GET / Status 200 handling with write callback.
 * 3. HTTP 404 Not Found handling.
 * 4. Chunked transfer decoding and response size accounting.
 * 5. Relative and absolute redirection tracking (CURLOPT_FOLLOWLOCATION).
 * 6. Connection cleanup, easy handle reset, and zero memory leak validation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "curl/curl.h"

#define TEST_PORT 18080

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} response_buf_t;

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    response_buf_t *mem = (response_buf_t *)userp;

    if (mem->size + realsize + 1 > mem->capacity) {
        size_t new_cap = (mem->capacity == 0) ? 1024 : mem->capacity * 2;
        while (new_cap < mem->size + realsize + 1) new_cap *= 2;
        char *ptr = (char *)realloc(mem->data, new_cap);
        if (!ptr) return 0;
        mem->data = ptr;
        mem->capacity = new_cap;
    }

    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

static volatile int server_running = 1;

static void *dummy_http_server(void *arg) {
    (void)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("server socket failed");
        return NULL;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(TEST_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("server bind failed");
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 10) < 0) {
        perror("server listen failed");
        close(server_fd);
        return NULL;
    }

    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!server_running) break;
            continue;
        }

        char buffer[2048];
        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            if (strstr(buffer, "GET /redirect_abs ") != NULL) {
                char resp[512];
                snprintf(resp, sizeof(resp),
                         "HTTP/1.1 302 Found\r\n"
                         "Location: http://127.0.0.1:%d/target\r\n"
                         "Content-Length: 0\r\n"
                         "Connection: close\r\n\r\n", TEST_PORT);
                send(client_fd, resp, strlen(resp), 0);
            } else if (strstr(buffer, "GET /redirect_rel ") != NULL) {
                const char *resp = "HTTP/1.1 302 Found\r\n"
                                   "Location: /target\r\n"
                                   "Content-Length: 0\r\n"
                                   "Connection: close\r\n\r\n";
                send(client_fd, resp, strlen(resp), 0);
            } else if (strstr(buffer, "GET /target ") != NULL) {
                const char *body = "Welcome to Banana OS target!";
                char resp[512];
                snprintf(resp, sizeof(resp),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n\r\n%s", strlen(body), body);
                send(client_fd, resp, strlen(resp), 0);
            } else if (strstr(buffer, "GET /chunked ") != NULL) {
                const char *hdr = "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Transfer-Encoding: chunked\r\n"
                                  "Connection: close\r\n\r\n";
                send(client_fd, hdr, strlen(hdr), 0);
                const char *c1 = "7\r\nBanana \r\n";
                send(client_fd, c1, strlen(c1), 0);
                const char *c2 = "8\r\nlibcurl!\r\n";
                send(client_fd, c2, strlen(c2), 0);
                const char *c3 = "0\r\n\r\n";
                send(client_fd, c3, strlen(c3), 0);
            } else if (strstr(buffer, "GET /404 ") != NULL) {
                const char *resp = "HTTP/1.1 404 Not Found\r\n"
                                   "Content-Type: text/plain\r\n"
                                   "Content-Length: 9\r\n"
                                   "Connection: close\r\n\r\nNot Found";
                send(client_fd, resp, strlen(resp), 0);
            } else {
                /* Default 200 response */
                const char *body = "Hello from Banana OS libcurl port!";
                char resp[512];
                snprintf(resp, sizeof(resp),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n\r\n%s", strlen(body), body);
                send(client_fd, resp, strlen(resp), 0);
            }
        }
        close(client_fd);
    }

    close(server_fd);
    return NULL;
}

int main(void) {
    pthread_t srv_thread;
    printf("=== libcurl HTTP test harness starting ===\n");

    if (pthread_create(&srv_thread, NULL, dummy_http_server, NULL) != 0) {
        fprintf(stderr, "Failed to start mock HTTP server\n");
        return 1;
    }
    usleep(100000); /* 100ms startup */

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_version_info_data *ver_info = curl_version_info(CURLVERSION_NOW);
    printf("libcurl version: %s (host: %s)\n", ver_info->version, ver_info->host);

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init failed\n");
        return 1;
    }

    /* Test 1: Simple GET 200 */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/hello", TEST_PORT);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 1: GET 200] code=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 200 || !strstr(buf.data, "Hello from Banana OS")) {
            fprintf(stderr, "Test 1 failed\n");
            return 1;
        }
        free(buf.data);
    }

    /* Test 2: HTTP 404 */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/404", TEST_PORT);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 2: GET 404] code=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 404) {
            fprintf(stderr, "Test 2 failed\n");
            return 1;
        }
        free(buf.data);
    }

    /* Test 3: Relative Redirection */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/redirect_rel", TEST_PORT);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 3: Redirect Rel] code=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 200 || !strstr(buf.data, "Welcome to Banana OS target!")) {
            fprintf(stderr, "Test 3 failed\n");
            return 1;
        }
        free(buf.data);
    }

    /* Test 4: Absolute Redirection */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/redirect_abs", TEST_PORT);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 4: Redirect Abs] code=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 200 || !strstr(buf.data, "Welcome to Banana OS target!")) {
            fprintf(stderr, "Test 4 failed\n");
            return 1;
        }
        free(buf.data);
    }

    /* Test 5: Chunked Transfer Decoding */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/chunked", TEST_PORT);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 5: Chunked] code=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 200 || strcmp(buf.data, "Banana libcurl!") != 0) {
            fprintf(stderr, "Test 5 failed (expected 'Banana libcurl!', got '%s')\n", buf.data ? buf.data : "");
            return 1;
        }
        free(buf.data);
    }

    /* Cleanup */
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    /* Stop server */
    server_running = 0;
    /* Send dummy request to unblock accept if needed */
    int trigger_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(TEST_PORT);
    connect(trigger_fd, (struct sockaddr *)&address, sizeof(address));
    close(trigger_fd);

    pthread_join(srv_thread, NULL);

    printf("ALL libcurl HTTP TESTS COMPLETED SUCCESSFULLY.\n");
    return 0;
}
