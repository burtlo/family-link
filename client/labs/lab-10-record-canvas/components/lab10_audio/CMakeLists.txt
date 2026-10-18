# L10-05 is host-model-only. The component is intentionally not required by
# the firmware app until an audio-device contract is verified.
idf_component_register(
    SRCS "lab10_audio.c"
    INCLUDE_DIRS "include"
)
