# h18 playback clips

Message 1 is the generated melody (built in the host, not a file here).

`voice-NN.wav` files are copies of inbox recordings from `data/03_messages/`.
`voice-01` through `voice-04` were empty and are omitted; the catalog is the
melody plus `voice-05` … `voice-22`. The host imports any missing or newer
WAVs from that tree when it builds the catalog (it will not restore 01–04).
Boot on the box walks the catalog and wraps.

WAVs are gitignored — family audio stays on the desk machine.
