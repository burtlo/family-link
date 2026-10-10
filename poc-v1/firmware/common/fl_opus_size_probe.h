#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Linker anchor: retains Opus encode/decode paths for X02 size probes. */
void fl_opus_size_probe_force_link(void);

#ifdef __cplusplus
}
#endif
