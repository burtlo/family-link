# H38 correction: stack workspace and changed-layout provenance

## Observed failure

The CRLF-corrected retry passed BIND, wrote and read back the exact approved MBR sector, and dispatched FORMAT. It then reported a main-task stack overflow and rebooted with ESP_RST_PANIC. No FORMAT_START arrived. Do not infer that formatting completed or that the callback write count was zero after the crash. The card is unqualified; the current MBR differs from H37. The existing controller continues to its authoritative deadline and mandatory existing-image restoration.

Independent linked analysis finds app_main frame 5,392 bytes plus init_card 4,336 plus emit_cid 4,192: 13,920 bytes before library/driver frames, exceeding the configured 12,288-byte stack. app_main plus do_layout is 10,816 bytes before nested calls. The panic location does not identify which earlier call damaged the stack.

## Correction and verification

1. Move the EMIT macro's 4,096-byte formatting workspace to one static buffer. All EMIT callers run synchronously on the single application task; retain exact formatting bounds, error handling, records, and transport limits. No stack-size increase or filesystem-profile change is proposed.
2. Independently review single-task ownership and non-reentrancy, then inspect the fresh linked stack frames and remaining filesystem/digest call chains before hardware. Rerun required host contract/controller checks; preserve all failed epochs.
3. Add an explicit prior-H38-layout input to preparation. Preserve H35 identity and H37 root provenance, but obtain the next expected old MBR from the previous actual successful LAYOUT_RESULT readback. Validate the exact epoch/ELF-bound successful prefix, private sector hash and canonical MBR, actual ordered dispatch ledger, immutable reviewed artifacts/intent, and independently approved layout-reference proof. Bind that proof immutably in new run metadata and revalidate it before use. Require verified restoration of the prior device run before preparing a retry.
4. The new intent and build use a new epoch. BIND still freshly reads the actual card MBR and rejects a mismatch before any write. This is a full new bounded layout/format/I/O attempt, with no automatic resume, repair, or qualification credit from the crashed run.
5. Exercise changed-sector/proof/artifact/identity/dispatch rejection and exact prior-layout acceptance. Independently review source and linked artifacts. Run only after prior recovery is verified; restore the existing full image, read back the complete image/PT/NVS/apps, and independently verify healthy original startup after the retry.

The operator's current-content preservation waiver remains active. All card/BOX identities, sectors, hashes, raw captures and paths remain private. Product/X02 and later cadence/fault/outbox work remain outside this correction.
