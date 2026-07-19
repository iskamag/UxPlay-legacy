/*
 * Copyright (C) 2026 Iska Mag
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2.1 or
 * later.
 */

#include "sdp.h"

#include <stdlib.h>
#include <string.h>

typedef enum {
    SDP_CONNECTION,
    SDP_RTPMAP,
    SDP_FMTP,
    SDP_FPAESKEY,
    SDP_AESIV,
    SDP_MIN_LATENCY,
    SDP_VALUE_COUNT
} sdp_value_t;

struct sdp_s {
    char *storage;
    const char *values[SDP_VALUE_COUNT];
};

typedef struct {
    const char *name;
    sdp_value_t value;
} sdp_attribute_t;

static const sdp_attribute_t sdp_attributes[] = {
    { "rtpmap", SDP_RTPMAP },
    { "fmtp", SDP_FMTP },
    { "fpaeskey", SDP_FPAESKEY },
    { "aesiv", SDP_AESIV },
    { "min-latency", SDP_MIN_LATENCY }
};

static char *
sdp_take_line(char **cursor)
{
    if (!cursor || !*cursor || !**cursor) {
        return NULL;
    }

    char *line = *cursor;
    char *end = line;
    while (*end && *end != '\r' && *end != '\n') {
        end++;
    }
    if (!*end) {
        *cursor = NULL;
        return line;
    }

    *end++ = '\0';
    while (*end == '\r' || *end == '\n') {
        end++;
    }
    *cursor = end;
    return line;
}

static void
sdp_read_line(sdp_t *sdp, char *line)
{
    if (!line || !line[0] || line[1] != '=') {
        return;
    }
    if (line[0] == 'c') {
        if (!sdp->values[SDP_CONNECTION]) {
            sdp->values[SDP_CONNECTION] = line + 2;
        }
        return;
    }
    if (line[0] != 'a') {
        return;
    }

    char *name = line + 2;
    char *value = strchr(name, ':');
    if (!value) {
        return;
    }
    *value++ = '\0';
    while (*value == ' ' || *value == '\t') {
        value++;
    }

    size_t count = sizeof(sdp_attributes) / sizeof(sdp_attributes[0]);
    for (size_t i = 0; i < count; i++) {
        const sdp_attribute_t *attribute = &sdp_attributes[i];
        if (!strcmp(name, attribute->name) &&
            !sdp->values[attribute->value]) {
            sdp->values[attribute->value] = value;
            return;
        }
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
    sdp->storage = malloc((size_t) length + 1);
    if (!sdp->storage) {
        free(sdp);
        return NULL;
    }
    memcpy(sdp->storage, data, (size_t) length);
    sdp->storage[length] = '\0';

    char *cursor = sdp->storage;
    char *line = NULL;
    while ((line = sdp_take_line(&cursor)) != NULL) {
        sdp_read_line(sdp, line);
    }
    return sdp;
}

void
sdp_destroy(sdp_t *sdp)
{
    if (!sdp) {
        return;
    }
    free(sdp->storage);
    free(sdp);
}

static const char *
sdp_get_value(const sdp_t *sdp, sdp_value_t value)
{
    return sdp ? sdp->values[value] : NULL;
}

const char *sdp_get_connection(sdp_t *sdp)
{
    return sdp_get_value(sdp, SDP_CONNECTION);
}

const char *sdp_get_rtpmap(sdp_t *sdp)
{
    return sdp_get_value(sdp, SDP_RTPMAP);
}

const char *sdp_get_fmtp(sdp_t *sdp)
{
    return sdp_get_value(sdp, SDP_FMTP);
}

const char *sdp_get_fpaeskey(sdp_t *sdp)
{
    return sdp_get_value(sdp, SDP_FPAESKEY);
}

const char *sdp_get_aesiv(sdp_t *sdp)
{
    return sdp_get_value(sdp, SDP_AESIV);
}

const char *sdp_get_min_latency(sdp_t *sdp)
{
    return sdp_get_value(sdp, SDP_MIN_LATENCY);
}
