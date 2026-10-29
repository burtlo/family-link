#pragma once

namespace product_mic
{

/* Microphone reads run on their own task and push PCM into the record session.
 * init() runs once in app_main before the display starts. The other calls are
 * non-blocking and are made from the record service on the presentation tick.
 * Returns false when the codec did not open. */
bool init();
void set_armed(bool armed);
/* Returns true once after the microphone failed to open or to read. */
bool take_failure();

} /* namespace product_mic */
