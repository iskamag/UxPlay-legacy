/*
 * Legacy AirPlay Mirroring (Apple TV software 5.x / iOS 5-6).
 *
 * This compatibility listener owns the protocol's fixed TCP port 7100.
 * It is deliberately separate from the newer SETUP-created mirror socket.
 */

#ifndef LEGACY_MIRROR_H
#define LEGACY_MIRROR_H

#include <stddef.h>
#include <stdint.h>

#include "logger.h"

typedef struct legacy_mirror_s legacy_mirror_t;
typedef int (*legacy_mirror_stream_callback_t)(void *opaque, int stream_fd,
                                                const char *body,
                                                size_t body_len);

legacy_mirror_t *legacy_mirror_init(logger_t *logger, uint16_t width,
                                    uint16_t height, uint8_t refresh_rate,
                                    uint8_t overscanned,
                                    legacy_mirror_stream_callback_t callback,
                                    void *callback_opaque);
int legacy_mirror_start(legacy_mirror_t *legacy, unsigned short *port);
void legacy_mirror_stop(legacy_mirror_t *legacy);
int legacy_mirror_decrypt_key(legacy_mirror_t *legacy,
                              const unsigned char encrypted[72],
                              unsigned char decrypted[16]);
void legacy_mirror_destroy(legacy_mirror_t *legacy);

#endif
