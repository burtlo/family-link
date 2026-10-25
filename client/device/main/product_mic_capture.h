#ifndef PRODUCT_MIC_CAPTURE_H
#define PRODUCT_MIC_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Microphone reads run on their own task and push PCM into the record session.
 * product_mic_init runs once in app_main before the display starts. The other
 * two calls are non-blocking and are made from the presentation tick. */
void product_mic_init(void);
void product_mic_set_armed(int armed);
/* Returns 1 once after the microphone failed to open or to read. */
int product_mic_take_failure(void);

#ifdef __cplusplus
}
#endif

#endif
