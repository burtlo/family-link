#include "fl_opus_size_probe.h"

#include <string.h>

#include "fl_opus.h"

void fl_opus_size_probe_force_link(void)
{
    fl_opus_codec_t codec;
    int16_t pcm[FL_OPUS_FRAME_SAMPLES];
    uint8_t pkt[FL_OPUS_MAX_PACKET_BYTES];

    memset(pcm, 0, sizeof(pcm));
    memset(pkt, 0, sizeof(pkt));

    if (fl_opus_init(&codec, FL_OPUS_PROFILE_VOIP_16K) != 0) {
        return;
    }

    int plen = fl_opus_encode_frame(&codec, pcm, pkt, sizeof(pkt));
    if (plen > 0) {
        (void)fl_opus_decode_frame(&codec, pkt, plen, pcm);
    }
    (void)fl_opus_decode_frame(&codec, NULL, 0, pcm);

    if (fl_opus_set_profile(&codec, FL_OPUS_PROFILE_APP_24K) == 0) {
        (void)fl_opus_encode_frame(&codec, pcm, pkt, sizeof(pkt));
    }

    fl_opus_deinit(&codec);
}
