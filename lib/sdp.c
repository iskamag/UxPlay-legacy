/**
 * Copyright (C) 2011-2012 Juho Vähä-Herttua
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2.1 or
 * later.
 */
#include "sdp.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct sdp_s {
    char *data;
    const char *connection;
    const char *rtpmap;
    const char *fmtp;
    const char *fpaeskey;
    const char *aesiv;
    const char *min_latency;
};

static void
sdp_parse_line(sdp_t *sdp, char *line)
{
    if (strlen(line) < 2 || line[1] != '=') {
        return;
    }
    if (line[0] == 'c') {
        sdp->connection = line + 2;
        return;
    }
    if (line[0] != 'a') {
        return;
    }

    char *key = line + 2;
    char *value = strchr(key, ':');
    if (!value) {
        return;
    }
    *value++ = '\0';
    if (!strcmp(key, "rtpmap") && !sdp->rtpmap) {
        sdp->rtpmap = value;
    } else if (!strcmp(key, "fmtp") && !sdp->fmtp) {
        sdp->fmtp = value;
    } else if (!strcmp(key, "fpaeskey")) {
        sdp->fpaeskey = value;
    } else if (!strcmp(key, "aesiv")) {
        sdp->aesiv = value;
    } else if (!strcmp(key, "min-latency")) {
        sdp->min_latency = value;
    }
}

sdp_t *
sdp_init(const char *data, int length)
{
    if (!data || length <= 0) {
        return NULL;
    }
    sdp_t *sdp = calloc(1, sizeof(*sdp));
    if (!sdp) {
        return NULL;
    }
    sdp->data = malloc((size_t) length + 1);
    if (!sdp->data) {
        free(sdp);
        return NULL;
    }
    memcpy(sdp->data, data, (size_t) length);
    sdp->data[length] = '\0';

    char *line = sdp->data;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) {
            *next++ = '\0';
        }
        size_t line_len = strlen(line);
        if (line_len && line[line_len - 1] == '\r') {
            line[line_len - 1] = '\0';
        }
        sdp_parse_line(sdp, line);
        line = next;
    }
    return sdp;
}

void
sdp_destroy(sdp_t *sdp)
{
    if (sdp) {
        free(sdp->data);
        free(sdp);
    }
}

#define SDP_GETTER(name, field)                  \
    const char *sdp_get_##name(sdp_t *sdp)       \
    {                                             \
        assert(sdp);                              \
        return sdp->field;                        \
    }

SDP_GETTER(connection, connection)
SDP_GETTER(rtpmap, rtpmap)
SDP_GETTER(fmtp, fmtp)
SDP_GETTER(fpaeskey, fpaeskey)
SDP_GETTER(aesiv, aesiv)
SDP_GETTER(min_latency, min_latency)
