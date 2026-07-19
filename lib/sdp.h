/*
 * Copyright (C) 2026 Iska Mag
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2.1 or
 * later.
 */

#ifndef UXPLAY_LEGACY_SDP_H
#define UXPLAY_LEGACY_SDP_H

typedef struct sdp_s sdp_t;

sdp_t *sdp_init(const char *data, int length);
void sdp_destroy(sdp_t *sdp);

const char *sdp_get_connection(sdp_t *sdp);
const char *sdp_get_rtpmap(sdp_t *sdp);
const char *sdp_get_fmtp(sdp_t *sdp);
const char *sdp_get_fpaeskey(sdp_t *sdp);
const char *sdp_get_aesiv(sdp_t *sdp);
const char *sdp_get_min_latency(sdp_t *sdp);

#endif
