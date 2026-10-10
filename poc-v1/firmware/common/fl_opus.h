#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_OPUS_SAMPLE_RATE_HZ   16000
#define FL_OPUS_FRAME_SAMPLES      320
#define FL_OPUS_FRAME_PCM_BYTES    640
#define FL_OPUS_MAX_PACKET_BYTES   400

typedef enum {
    FL_OPUS_PROFILE_VOIP_16K = 0, /* OPUS_APPLICATION_VOIP @ 16 kbps */
    FL_OPUS_PROFILE_APP_24K  = 1, /* OPUS_APPLICATION_AUDIO @ 24 kbps */
} fl_opus_profile_t;

typedef struct {
    void *enc;
    void *dec;
    fl_opus_profile_t profile;
    int bitrate_bps;
} fl_opus_codec_t;

/*
 * Missing / corrupt frame policy (decode path):
 *
 * - Missing (len == 0 or packet == NULL): call Opus packet-loss concealment
 *   (decode with no packet). Audible: short smooth fill; no state reset.
 *
 * - Corrupt (opus_decode error): log once per frame, run PLC for that 20 ms
 *   slot (same as missing). Do not advance encoder state on the wire side.
 *
 * - Never abort the demo on a single bad frame; callers get FL_OPUS_FRAME_SAMPLES
 *   of PCM (PLC or silence fallback if PLC fails).
 */

int fl_opus_init(fl_opus_codec_t *codec, fl_opus_profile_t profile);
void fl_opus_deinit(fl_opus_codec_t *codec);

int fl_opus_set_profile(fl_opus_codec_t *codec, fl_opus_profile_t profile);

/** Returns encoded length in bytes, or negative Opus error code. */
int fl_opus_encode_frame(fl_opus_codec_t *codec, const int16_t *pcm_320,
                         uint8_t *out, size_t out_cap);

/**
 * Decode one frame to pcm_320. For missing frames pass packet=NULL and len=0.
 * For corrupt injection pass a bad buffer; PLC is used on decode failure.
 */
int fl_opus_decode_frame(fl_opus_codec_t *codec, const uint8_t *packet, int len,
                         int16_t *pcm_320);

#ifdef __cplusplus
}
#endif
