#include "fl_opus.h"

#include <string.h>

#include "esp_log.h"
#include "opus.h"

static const char *TAG = "fl_opus";

static int profile_bitrate(fl_opus_profile_t profile)
{
    switch (profile) {
    case FL_OPUS_PROFILE_VOIP_16K:
        return 16000;
    case FL_OPUS_PROFILE_APP_24K:
        return 24000;
    default:
        return 16000;
    }
}

static int profile_application(fl_opus_profile_t profile)
{
    switch (profile) {
    case FL_OPUS_PROFILE_VOIP_16K:
        return OPUS_APPLICATION_VOIP;
    case FL_OPUS_PROFILE_APP_24K:
        return OPUS_APPLICATION_AUDIO;
    default:
        return OPUS_APPLICATION_VOIP;
    }
}

static void destroy_handles(fl_opus_codec_t *codec)
{
    if (codec == NULL) {
        return;
    }
    if (codec->enc != NULL) {
        opus_encoder_destroy((OpusEncoder *)codec->enc);
        codec->enc = NULL;
    }
    if (codec->dec != NULL) {
        opus_decoder_destroy((OpusDecoder *)codec->dec);
        codec->dec = NULL;
    }
}

int fl_opus_set_profile(fl_opus_codec_t *codec, fl_opus_profile_t profile)
{
    if (codec == NULL) {
        return OPUS_BAD_ARG;
    }
    if (codec->profile == profile && codec->enc != NULL) {
        return OPUS_OK;
    }
    destroy_handles(codec);
    return fl_opus_init(codec, profile);
}

int fl_opus_init(fl_opus_codec_t *codec, fl_opus_profile_t profile)
{
    if (codec == NULL) {
        return OPUS_BAD_ARG;
    }
    memset(codec, 0, sizeof(*codec));
    codec->profile = profile;
    codec->bitrate_bps = profile_bitrate(profile);

    int err = OPUS_OK;
    OpusEncoder *enc =
        opus_encoder_create(FL_OPUS_SAMPLE_RATE_HZ, 1, profile_application(profile), &err);
    if (err != OPUS_OK || enc == NULL) {
        ESP_LOGE(TAG, "opus_encoder_create %d", err);
        return err != OPUS_OK ? err : OPUS_INTERNAL_ERROR;
    }
    (void)opus_encoder_ctl(enc, OPUS_SET_BITRATE(codec->bitrate_bps));
    (void)opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(2));
    (void)opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    (void)opus_encoder_ctl(enc, OPUS_SET_DTX(0));

    OpusDecoder *dec = opus_decoder_create(FL_OPUS_SAMPLE_RATE_HZ, 1, &err);
    if (err != OPUS_OK || dec == NULL) {
        opus_encoder_destroy(enc);
        ESP_LOGE(TAG, "opus_decoder_create %d", err);
        return err != OPUS_OK ? err : OPUS_INTERNAL_ERROR;
    }

    codec->enc = enc;
    codec->dec = dec;
    ESP_LOGI(TAG, "init profile=%d bitrate=%d", (int)profile, codec->bitrate_bps);
    return OPUS_OK;
}

void fl_opus_deinit(fl_opus_codec_t *codec)
{
    destroy_handles(codec);
    if (codec != NULL) {
        memset(codec, 0, sizeof(*codec));
    }
}

int fl_opus_encode_frame(fl_opus_codec_t *codec, const int16_t *pcm_320, uint8_t *out,
                         size_t out_cap)
{
    if (codec == NULL || codec->enc == NULL || pcm_320 == NULL || out == NULL) {
        return OPUS_BAD_ARG;
    }
    return opus_encode((OpusEncoder *)codec->enc, pcm_320, FL_OPUS_FRAME_SAMPLES, out,
                       (opus_int32)out_cap);
}

int fl_opus_decode_frame(fl_opus_codec_t *codec, const uint8_t *packet, int len,
                         int16_t *pcm_320)
{
    if (codec == NULL || codec->dec == NULL || pcm_320 == NULL) {
        return OPUS_BAD_ARG;
    }

    int n;
    if (packet == NULL || len <= 0) {
        n = opus_decode((OpusDecoder *)codec->dec, NULL, 0, pcm_320, FL_OPUS_FRAME_SAMPLES, 0);
        if (n < 0) {
            memset(pcm_320, 0, FL_OPUS_FRAME_PCM_BYTES);
            return FL_OPUS_FRAME_SAMPLES;
        }
        return n;
    }

    n = opus_decode((OpusDecoder *)codec->dec, packet, len, pcm_320, FL_OPUS_FRAME_SAMPLES, 0);
    if (n < 0) {
        ESP_LOGW(TAG, "opus_decode corrupt (%d); PLC", n);
        n = opus_decode((OpusDecoder *)codec->dec, NULL, 0, pcm_320, FL_OPUS_FRAME_SAMPLES, 0);
        if (n < 0) {
            memset(pcm_320, 0, FL_OPUS_FRAME_PCM_BYTES);
            return FL_OPUS_FRAME_SAMPLES;
        }
    }
    return n;
}
