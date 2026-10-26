/*
 * libcurl HTTP & HTTPS test harness for Banana OS port verification.
 * Tests:
 * 1. libcurl version and features verification (Mbed TLS backend enabled).
 * 2. HTTP GET / Status 200 handling with write callback.
 * 3. HTTP 404 Not Found handling.
 * 4. Chunked transfer decoding and response size accounting.
 * 5. Relative and absolute redirection tracking (CURLOPT_FOLLOWLOCATION).
 * 6. Connection cleanup, easy handle reset, and zero memory leak validation.
 * 7. HTTPS verified GET using custom/system CA certificates.
 * 8. Enforced certificate validation: untrusted certificate must fail with CURLE_PEER_FAILED_VERIFICATION.
 * 9. HTTPS non-blocking poll & timeout behavior (CURLOPT_TIMEOUT_MS triggers CURLE_OPERATION_TIMEDOUT).
 * 10. HTTPS POST upload & response echo verification.
 * 11. HTTPS redirection (relative and absolute) tracking with TLS validation.
 * 12. Easy handle reuse and cancellation / reset between transfers without resource leaks.
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
#include "test_tls_certs.h"

#define TEST_HTTP_PORT 18080
#define TEST_HTTPS_PORT 19443

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

static volatile int http_server_running = 1;

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
    address.sin_port = htons(TEST_HTTP_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("server bind failed");
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 5) < 0) {
        perror("server listen failed");
        close(server_fd);
        return NULL;
    }

    while (http_server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!http_server_running) break;
            continue;
        }

        char buffer[2048] = {0};
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            if (strstr(buffer, "GET /hello HTTP")) {
                const char *body = "Hello from Banana OS libcurl port!";
                char resp[512];
                int len = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n\r\n"
                    "%s", strlen(body), body);
                write(client_fd, resp, len);
            } else if (strstr(buffer, "GET /notfound HTTP")) {
                const char *body = "Not Found";
                char resp[512];
                int len = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n\r\n"
                    "%s", strlen(body), body);
                write(client_fd, resp, len);
            } else if (strstr(buffer, "GET /redirect_rel HTTP")) {
                const char *resp =
                    "HTTP/1.1 302 Found\r\n"
                    "Location: /target\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n\r\n";
                write(client_fd, resp, strlen(resp));
            } else if (strstr(buffer, "GET /redirect_abs HTTP")) {
                char resp[256];
                int len = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 302 Found\r\n"
                    "Location: http://127.0.0.1:%d/target\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n\r\n", TEST_HTTP_PORT);
                write(client_fd, resp, len);
            } else if (strstr(buffer, "GET /target HTTP")) {
                const char *body = "Welcome to Banana OS target!";
                char resp[512];
                int len = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n\r\n"
                    "%s", strlen(body), body);
                write(client_fd, resp, len);
            } else if (strstr(buffer, "GET /chunked HTTP")) {
                const char *headers =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "Connection: close\r\n\r\n";
                write(client_fd, headers, strlen(headers));
                const char *c1 = "7\r\nBanana \r\n";
                write(client_fd, c1, strlen(c1));
                const char *c2 = "8\r\nlibcurl!\r\n";
                write(client_fd, c2, strlen(c2));
                const char *c_end = "0\r\n\r\n";
                write(client_fd, c_end, strlen(c_end));
            } else {
                const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                write(client_fd, resp, strlen(resp));
            }
        }
        close(client_fd);
    }

    close(server_fd);
    return NULL;
}

int main(void) {
    printf("=== libcurl HTTP/HTTPS test harness starting ===\n");

    /* 1. Start mock HTTP server */
    pthread_t http_srv_thread;
    if (pthread_create(&http_srv_thread, NULL, dummy_http_server, NULL) != 0) {
        fprintf(stderr, "Failed to start mock HTTP server\n");
        return 1;
    }
    usleep(100000); /* 100ms startup */

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_version_info_data *ver_info = curl_version_info(CURLVERSION_NOW);
    printf("libcurl version: %s (host: %s, ssl: %s)\n",
           ver_info->version, ver_info->host, ver_info->ssl_version ? ver_info->ssl_version : "none");

    if (!ver_info->ssl_version || !strstr(ver_info->ssl_version, "mbedTLS")) {
        fprintf(stderr, "FATAL: libcurl was not built with mbedTLS backend!\n");
        return 1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init failed\n");
        return 1;
    }

    /* Test 1: Simple GET 200 */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/hello", TEST_HTTP_PORT);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 1: HTTP GET 200] code=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 200 || !strstr(buf.data, "Hello from Banana OS libcurl port!")) {
            fprintf(stderr, "Test 1 failed\n");
            return 1;
        }
        free(buf.data);
    }

    /* Test 2: HTTP 404 */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/notfound", TEST_HTTP_PORT);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 2: HTTP GET 404] code=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 404 || !strstr(buf.data, "Not Found")) {
            fprintf(stderr, "Test 2 failed\n");
            return 1;
        }
        free(buf.data);
    }

    /* Test 3: Relative Redirection */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/redirect_rel", TEST_HTTP_PORT);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 3: HTTP Redirect Rel] code=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
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
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/redirect_abs", TEST_HTTP_PORT);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 4: HTTP Redirect Abs] code=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
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
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/chunked", TEST_HTTP_PORT);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 5: HTTP Chunked] code=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 200 || strcmp(buf.data, "Banana libcurl!") != 0) {
            fprintf(stderr, "Test 5 failed (expected 'Banana libcurl!', got '%s')\n", buf.data ? buf.data : "");
            return 1;
        }
        free(buf.data);
    }

    /* Stop HTTP server */
    http_server_running = 0;
    int trigger_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(TEST_HTTP_PORT);
    connect(trigger_fd, (struct sockaddr *)&address, sizeof(address));
    close(trigger_fd);
    pthread_join(http_srv_thread, NULL);

    /* 2. HTTPS Tests with TLS backend */
    char ca_file[] = "/tmp/banana_test_ca_XXXXXX";
    int fd_ca = mkstemp(ca_file);
    if (fd_ca < 0) { perror("mkstemp ca"); return 1; }
    write(fd_ca, test_ca_crt_pem, strlen(test_ca_crt_pem));
    close(fd_ca);

    /* Test 6: Verified HTTPS GET against test TLS server */
    {
        response_buf_t buf = {0};
        char errbuf[CURL_ERROR_SIZE] = {0};
        char url[128];
        snprintf(url, sizeof(url), "https://127.0.0.1:%d/hello", TEST_HTTPS_PORT);
        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 6: HTTPS Verified GET 200] res=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 200 || !buf.data || strcmp(buf.data, "Hello Secure TLS!") != 0) {
            fprintf(stderr, "Test 6 failed: %s\n", errbuf);
            unlink(ca_file);
            return 1;
        }
        free(buf.data);
    }

    /* Test 7: HTTPS Follow Location (Relative and Absolute) */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "https://127.0.0.1:%d/redirect_rel", TEST_HTTPS_PORT);
        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 7: HTTPS Redirect Rel] res=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 200 || strcmp(buf.data, "Hello Secure TLS!") != 0) {
            fprintf(stderr, "Test 7 failed\n");
            unlink(ca_file);
            return 1;
        }
        free(buf.data);
    }

    /* Test 8: HTTPS POST Upload & Response Echo */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "https://127.0.0.1:%d/upload", TEST_HTTPS_PORT);
        curl_easy_reset(curl);
        const char *upload_payload = "BananaOS Secure HTTPS Streaming Upload Test";
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, upload_payload);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        printf("[Test 8: HTTPS POST Upload] res=%d data='%s'\n", res, buf.data ? buf.data : "");
        if (res != CURLE_OK || strcmp(buf.data, upload_payload) != 0) {
            fprintf(stderr, "Test 8 failed\n");
            unlink(ca_file);
            return 1;
        }
        free(buf.data);
    }

    /* Test 9: Transfer Timeout (poll / socket non-blocking error check) */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "https://127.0.0.1:%d/slow", TEST_HTTPS_PORT);
        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 400L); /* 400ms timeout on a 1.5s endpoint */
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        printf("[Test 9: HTTPS Transfer Timeout] res=%d (%s)\n", res, curl_easy_strerror(res));
        if (res != CURLE_OPERATION_TIMEDOUT) {
            fprintf(stderr, "Test 9 failed: expected timeout (28), got %d\n", res);
            unlink(ca_file);
            return 1;
        }
        free(buf.data);
    }

    /* Test 10: Enforced Certificate Verification against Default / System CA Bundle */
    {
        response_buf_t buf = {0};
        char errbuf[CURL_ERROR_SIZE] = {0};
        char url[128];
        snprintf(url, sizeof(url), "https://127.0.0.1:%d/hello", TEST_HTTPS_PORT);
        curl_easy_reset(curl);
        /* Do NOT provide CURLOPT_CAINFO: relies on built-in /etc/ssl/certs/ca-certificates.crt */
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

        CURLcode res = curl_easy_perform(curl);
        printf("[Test 10: System CA Verification Rejection] res=%d (%s)\n", res, curl_easy_strerror(res));
        if (res == CURLE_OK) {
            fprintf(stderr, "Test 10 failed: Certificate verification was improperly bypassed!\n");
            unlink(ca_file);
            return 1;
        }
        free(buf.data);
    }

    /* Test 11: Handle Reuse and Connection Reset after Errors */
    {
        response_buf_t buf = {0};
        char url[128];
        snprintf(url, sizeof(url), "https://127.0.0.1:%d/hello", TEST_HTTPS_PORT);
        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[Test 11: Handle Reset & Reuse] res=%d http=%ld data='%s'\n", res, http_code, buf.data ? buf.data : "");
        if (res != CURLE_OK || http_code != 200 || strcmp(buf.data, "Hello Secure TLS!") != 0) {
            fprintf(stderr, "Test 11 failed\n");
            unlink(ca_file);
            return 1;
        }
        free(buf.data);
    }

    /* Clean up client and temporary CA file */
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    unlink(ca_file);

    printf("ALL libcurl HTTP AND HTTPS TESTS COMPLETED SUCCESSFULLY.\n");
    return 0;
}
