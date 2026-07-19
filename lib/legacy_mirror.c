/*
 * Copyright (C) 2026 Iska Mag
 *
 * Legacy AirPlay Mirroring discovery/stream listener.
 *
 * iOS 5 and iOS 6 query GET /stream.xml on a fixed TCP port (normally 7100)
 * before starting the FairPlay/RAOP session.  Newer UxPlay only creates that
 * socket after a binary-plist SETUP, which leaves old clients with an
 * immediate TCP reset.  This listener restores the old control endpoint.
 *
 * POST /stream is decoded as a binary plist and its connection is handed to
 * UxPlay's H.264 stream parser.  Some 130.x clients also perform FairPlay on
 * this connection, so the listener retains FairPlay state across requests.
 */

#include "legacy_mirror.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

#include "compat.h"
#include "fairplay.h"
#include "netutils.h"
#include "sockets.h"
#include "threads.h"
#include "utils.h"

#include <plist/plist.h>

#define LEGACY_HEADER_LIMIT (64 * 1024)
#define LEGACY_BODY_LIMIT   (1024 * 1024)

struct legacy_mirror_s {
    logger_t *logger;
    uint16_t width;
    uint16_t height;
    uint8_t refresh_rate;
    uint8_t overscanned;
    legacy_mirror_stream_callback_t callback;
    void *callback_opaque;
    fairplay_t *fairplay;

    int server_fd;
    int client_fd;
    int running;
    int joined;
    thread_handle_t thread;
    mutex_handle_t mutex;
};

static int
legacy_send_all(int fd, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        int ret = send(fd, data + sent, length - sent, 0);
        if (ret <= 0) {
            return -1;
        }
        sent += (size_t) ret;
    }
    return 0;
}

static long
legacy_content_length(const char *headers)
{
    const char *line = headers;
    while (line && *line) {
        const char *end = strstr(line, "\r\n");
        size_t length = end ? (size_t) (end - line) : strlen(line);
        if (length >= 15 && !strncasecmp(line, "Content-Length:", 15)) {
            char *parse_end = NULL;
            long value = strtol(line + 15, &parse_end, 10);
            return (parse_end == line + 15 || value < 0) ? -1 : value;
        }
        line = end ? end + 2 : NULL;
    }
    return 0;
}

static void
legacy_log_plist(legacy_mirror_t *legacy, const char *body, size_t body_len)
{
    plist_t root = NULL;
    plist_from_bin(body, (uint32_t) body_len, &root);
    if (!root) {
        logger_log(legacy->logger, LOGGER_WARNING,
                   "iOS 6 POST /stream body was not a binary plist (%zu bytes)",
                   body_len);
        return;
    }

    char *xml = NULL;
    uint32_t xml_len = 0;
    plist_to_xml(root, &xml, &xml_len);
    if (xml) {
        char *stripped = utils_strip_data_from_plist_xml(xml);
        logger_log(legacy->logger, LOGGER_INFO,
                   "iOS 6 legacy mirror parameters:\n%s",
                   stripped ? stripped : xml);
        free(stripped);
        plist_mem_free(xml);
    }
    plist_free(root);
}

static int
legacy_read_request(int fd, char **request,
                    size_t *body_offset, size_t *body_len)
{
    size_t capacity = 4096;
    size_t used = 0;
    char *buffer = malloc(capacity + 1);
    if (!buffer) {
        return -1;
    }

    char *header_end = NULL;
    while (!header_end) {
        if (used == capacity) {
            if (capacity >= LEGACY_HEADER_LIMIT) {
                free(buffer);
                return -1;
            }
            capacity *= 2;
            char *grown = realloc(buffer, capacity + 1);
            if (!grown) {
                free(buffer);
                return -1;
            }
            buffer = grown;
        }
        /* Do not consume bytes from the H.264 stream following POST /stream. */
        int ret = recv(fd, buffer + used, 1, 0);
        if (ret <= 0) {
            free(buffer);
            return -1;
        }
        used += (size_t) ret;
        buffer[used] = '\0';
        header_end = strstr(buffer, "\r\n\r\n");
    }

    *body_offset = (size_t) (header_end + 4 - buffer);
    long content_length = legacy_content_length(buffer);
    if (content_length < 0 || content_length > LEGACY_BODY_LIMIT) {
        free(buffer);
        return -1;
    }
    *body_len = (size_t) content_length;

    size_t required = *body_offset + *body_len;
    if (required > capacity) {
        char *grown = realloc(buffer, required + 1);
        if (!grown) {
            free(buffer);
            return -1;
        }
        buffer = grown;
        capacity = required;
    }
    while (used < required) {
        int ret = recv(fd, buffer + used, required - used, 0);
        if (ret <= 0) {
            free(buffer);
            return -1;
        }
        used += (size_t) ret;
    }
    buffer[used] = '\0';

    *request = buffer;
    return 0;
}

static void
legacy_handle_get(legacy_mirror_t *legacy, int fd)
{
    /* Match the spec example: 1280x720, 60Hz, overscanned.
     * Some iOS 5/6 clients may reject non-overscanned or high-res modes. */
    uint16_t width = 1280;
    uint16_t height = 720;
    uint8_t overscanned = 1;

    char xml[768];
    int xml_len = snprintf(
        xml, sizeof(xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\r\n"
        "<plist version=\"1.0\"><dict>"
        "<key>height</key><integer>%u</integer>"
        "<key>overscanned</key><%s/>"
        "<key>refreshRate</key><real>%.16f</real>"
        "<key>version</key><string>130.14</string>"
        "<key>width</key><integer>%u</integer>"
        "</dict></plist>\r\n",
        height, overscanned ? "true" : "false",
        1.0 / (double) legacy->refresh_rate, width);

    char response[1024];
    int response_len = snprintf(
        response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/x-apple-plist+xml\r\n"
        "Content-Length: %d\r\n"
        "Server: AirTunes/130.14\r\n\r\n%s",
        xml_len, xml);
    legacy_send_all(fd, response, (size_t) response_len);
    logger_log(legacy->logger, LOGGER_INFO,
               "Answered iOS 6 GET /stream.xml with %ux%u@%u",
               legacy->width, legacy->height, legacy->refresh_rate);
}

static int
legacy_handle_post(legacy_mirror_t *legacy, int fd, const char *body,
                   size_t body_len)
{
    legacy_log_plist(legacy, body, body_len);

    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Server: AirTunes/130.14\r\n\r\n";
    if (legacy_send_all(fd, response, sizeof(response) - 1) < 0) {
        return -1;
    }

    logger_log(legacy->logger, LOGGER_INFO,
               "Accepted iOS 6 POST /stream; handing off legacy H.264 stream");
    if (!legacy->callback) {
        logger_log(legacy->logger, LOGGER_ERR,
                   "No legacy H.264 stream callback is installed");
        return -1;
    }
    return legacy->callback(legacy->callback_opaque, fd, body, body_len);
}

static void
legacy_handle_fairplay(legacy_mirror_t *legacy, int fd, const char *body,
                       size_t body_len)
{
    unsigned char reply[142];
    int reply_len = 0;
    int result = -1;
    if (body_len == 16) {
        result = fairplay_setup(legacy->fairplay,
                                (const unsigned char *) body, reply);
        reply_len = 142;
    } else if (body_len == 164) {
        result = fairplay_handshake(legacy->fairplay,
                                    (const unsigned char *) body, reply);
        reply_len = 32;
    }
    if (result) {
        static const char bad_request[] =
            "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        legacy_send_all(fd, bad_request, sizeof(bad_request) - 1);
        return;
    }

    char headers[256];
    int headers_len = snprintf(
        headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %d\r\n"
        "Server: AirTunes/130.14\r\n\r\n",
        reply_len);
    if (legacy_send_all(fd, headers, (size_t) headers_len) < 0) {
        return;
    }
    legacy_send_all(fd, (const char *) reply, (size_t) reply_len);
    logger_log(legacy->logger, LOGGER_INFO,
               "Handled iOS 6 FairPlay stage on TCP 7100 (%zu bytes)",
               body_len);
}

static THREAD_RETVAL
legacy_mirror_thread(void *opaque)
{
    legacy_mirror_t *legacy = opaque;

    while (1) {
        MUTEX_LOCK(legacy->mutex);
        int running = legacy->running;
        int server_fd = legacy->server_fd;
        MUTEX_UNLOCK(legacy->mutex);
        if (!running) {
            break;
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        struct timeval timeout = {1, 0};
        int ready = select(server_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready == 0) {
            continue;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        MUTEX_LOCK(legacy->mutex);
        legacy->client_fd = client_fd;
        MUTEX_UNLOCK(legacy->mutex);

        bool stream_handed_off = false;
        while (!stream_handed_off) {
            MUTEX_LOCK(legacy->mutex);
            running = legacy->running;
            MUTEX_UNLOCK(legacy->mutex);
            if (!running) {
                break;
            }

            char *request = NULL;
            size_t body_offset = 0;
            size_t body_len = 0;
            if (legacy_read_request(client_fd, &request, &body_offset,
                                    &body_len)) {
                break;
            }
            char method[16] = {0};
            char path[256] = {0};
            sscanf(request, "%15s %255s", method, path);
            logger_log(legacy->logger, LOGGER_INFO,
                       "iOS 6 mirror request: %s %s", method, path);
            if (!strcmp(method, "GET") && !strcmp(path, "/stream.xml")) {
                legacy_handle_get(legacy, client_fd);
            } else if (!strcmp(method, "POST") &&
                       !strcmp(path, "/fp-setup")) {
                legacy_handle_fairplay(legacy, client_fd,
                                       request + body_offset, body_len);
            } else if (!strcmp(method, "POST") && !strcmp(path, "/stream")) {
                stream_handed_off =
                    !legacy_handle_post(legacy, client_fd,
                                        request + body_offset, body_len);
            } else {
                static const char not_found[] =
                    "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                legacy_send_all(client_fd, not_found, sizeof(not_found) - 1);
            }
            free(request);
        }

        MUTEX_LOCK(legacy->mutex);
        if (legacy->client_fd == client_fd) {
            legacy->client_fd = -1;
        }
        MUTEX_UNLOCK(legacy->mutex);
        if (!stream_handed_off) {
            shutdown(client_fd, SHUT_RDWR);
            CLOSESOCKET(client_fd);
        }
    }

    MUTEX_LOCK(legacy->mutex);
    legacy->running = 0;
    MUTEX_UNLOCK(legacy->mutex);
    return NULL;
}

legacy_mirror_t *
legacy_mirror_init(logger_t *logger, uint16_t width, uint16_t height,
                   uint8_t refresh_rate, uint8_t overscanned,
                   legacy_mirror_stream_callback_t callback,
                   void *callback_opaque)
{
    legacy_mirror_t *legacy = calloc(1, sizeof(*legacy));
    if (!legacy) {
        return NULL;
    }
    legacy->logger = logger;
    legacy->width = width;
    legacy->height = height;
    legacy->refresh_rate = refresh_rate ? refresh_rate : 60;
    legacy->overscanned = overscanned;
    legacy->callback = callback;
    legacy->callback_opaque = callback_opaque;
    legacy->fairplay = fairplay_init(logger);
    if (!legacy->fairplay) {
        free(legacy);
        return NULL;
    }
    legacy->server_fd = -1;
    legacy->client_fd = -1;
    legacy->joined = 1;
    MUTEX_CREATE(legacy->mutex);
    return legacy;
}

int
legacy_mirror_start(legacy_mirror_t *legacy, unsigned short *port)
{
    if (!legacy || !port) {
        return -1;
    }
    MUTEX_LOCK(legacy->mutex);
    if (legacy->running || !legacy->joined) {
        MUTEX_UNLOCK(legacy->mutex);
        return 0;
    }

    int fd = netutils_init_socket(port, 0, 0);
    if (fd < 0 || listen(fd, 2) < 0) {
        if (fd >= 0) {
            CLOSESOCKET(fd);
        }
        MUTEX_UNLOCK(legacy->mutex);
        return -1;
    }
    legacy->server_fd = fd;
    legacy->running = 1;
    legacy->joined = 0;
    THREAD_CREATE(legacy->thread, legacy_mirror_thread, legacy);
    MUTEX_UNLOCK(legacy->mutex);

    logger_log(legacy->logger, LOGGER_INFO,
               "Legacy iOS 5/6 mirror control listening on TCP %u", *port);
    return 0;
}

void
legacy_mirror_stop(legacy_mirror_t *legacy)
{
    if (!legacy) {
        return;
    }
    MUTEX_LOCK(legacy->mutex);
    int should_join = !legacy->joined;
    legacy->running = 0;
    int client_fd = legacy->client_fd;
    MUTEX_UNLOCK(legacy->mutex);

    if (client_fd >= 0) {
        shutdown(client_fd, SHUT_RDWR);
    }
    if (should_join) {
        THREAD_JOIN(legacy->thread);
        MUTEX_LOCK(legacy->mutex);
        legacy->joined = 1;
        MUTEX_UNLOCK(legacy->mutex);
    }
    if (legacy->server_fd >= 0) {
        CLOSESOCKET(legacy->server_fd);
        legacy->server_fd = -1;
    }
}

void
legacy_mirror_destroy(legacy_mirror_t *legacy)
{
    if (!legacy) {
        return;
    }
    legacy_mirror_stop(legacy);
    fairplay_destroy(legacy->fairplay);
    MUTEX_DESTROY(legacy->mutex);
    free(legacy);
}

int
legacy_mirror_decrypt_key(legacy_mirror_t *legacy,
                          const unsigned char encrypted[72],
                          unsigned char decrypted[16])
{
    if (!legacy) {
        return -1;
    }
    return fairplay_decrypt(legacy->fairplay, encrypted, decrypted);
}
