# Durable long-message work: pause checkpoint

**Updated:** 2026-10-05

**Status:** Paused at the user's request, before the H35 device flash.

**Repository:** `main`, latest experiment commit `5170a92` (`Add isolated H35 SDMMC discovery fixture and safety controller`).

This checkpoint records the exact state to resume from. The ordered experiment list remains in [long-message-experiments.md](long-message-experiments.md), and the attached-storage safety and acceptance plan remains [attached-storage-qualification.md](attached-storage-qualification.md). This file takes precedence over older “immediate work” instructions in [durable-message-continuation.md](durable-message-continuation.md) where they describe H32 implementation as still pending.

## What is complete

- **H34 server message-store proof:** implementation and qualification evidence are committed. It passed 21 functional checks and 50 process-crash boundaries with a deterministic 180-second PCM fixture. It proves server-side file storage, chunk acknowledgements, recoverable completion, inbox reconstruction, bounded range reads, authorization, cull/restore, admission, and corruption isolation. It does not prove switched-power-loss behavior or device outbox behavior. See [H34 evidence](../evidence/h34-message-store/README.md).
- **H32 on-chip storage qualification:** not qualified. The default-yield PCM run missed 79 of 90 deadlines; the no-yield comparison missed 44 of 90 scheduled deadlines, including 33 service misses. The no-yield full-image restore/readback passed. See [failure summary](../evidence/onchip-storage-qualification/no-yield-20261005-failure/summary.md).
- **H32 write-coalescing candidate:** closed at the read-only feasibility gate. The 4 KiB FAT cluster causes the proposed 16 KiB writes to remain four 4 KiB driver writes, so this candidate was not flashed. See [preflight evidence](../evidence/onchip-storage-qualification/coalescing-preflight.md).
- **Attached-storage safety plan:** committed as `bee7427`. It sets the discovery, media-ownership, preservation, mutation-boundary, cadence, fault, near-full, restore, and acceptance gates. It makes clear that discovery alone is not storage qualification.
- **H35 SDMMC read-only discovery implementation:** committed as `5170a92`. It uses the BOX-3 BSP SDMMC pins and active-low power control, retains the native USB Serial/JTAG console, initializes the card once, and emits versioned discovery records. It does not mount a filesystem, read user data sectors, write media, initialize USB host, or change eFuses. The H35 reference partition CSV matches the preserved device table; the controller also regenerates its build table from the private original backup and refuses a mismatch. The controller’s flash path verifies the original device image, flashes only the application, captures serial output, and performs a complete-image restore and readback even on capture failure.
- **H35 host-side checks:** the parser accepted synthetic success and expected failure fixtures and rejected 14 malformed/unsafe fixture variants, including watchdog-reset boots. Python compilation and `git diff --check` passed. These are software checks, not hardware evidence.

## Exact H35 stopping point

A fresh immutable H35 run was prepared after commit `5170a92`; its firmware build completed successfully under ESP-IDF 5.4.2. The private manifest binds the build to that commit and the selected original device backup. The app is 278,816 bytes in a 1,536,000-byte factory slot (about 18.2% used; the controller requires at least 15% headroom). The build command invokes the controller's build validation internally. The original device partition map was independently compared to the H35 reference CSV and the controller-generated private table.

No H35 image has been flashed. No H35 serial discovery result exists. No filesystem or removable-media content has been inspected. At the time of this pause, the device's last verified state is the restored original image from the preceding H32 work; the H35 procedure still performs a fresh connected full-image check before flashing.

The fresh H35 build epoch and raw logs, flash images, serial capture, and device identity are in operator-private storage outside the repository. Keep them there; do not copy private paths, raw captures, CID values, serial numbers, MAC addresses, NVS contents, or full-image hashes into public evidence.

## Resume sequence

1. Re-read `docs/AGENTS.md` and this checkpoint. Confirm the worktree/branch and inspect the private H35 run rather than preparing over it. Never reuse an epoch if source or configuration changes.
2. Run the explicit H35 `validate-build` command against the fresh run and original backup. Have an independent reviewer validate that same final epoch's build manifest, app bounds, source/configuration binding, and no-media-write properties. If any input changed, prepare a new epoch and rebuild.
3. If both gates pass, run the already authorized bounded `flash-capture` operation on the uniquely identified serial device. It must establish a fresh full-image match before mutation, capture before application launch, retain the raw result privately, and execute the controller's full-image restore/readback path. Treat any failure to restore as a blocking incident, not a discovery verdict.
4. Parse the capture into a new sanitized evidence file only after checking the private log and restore proof. Independently review the raw-to-public mapping, ensure the evidence contains no private identity, and commit the Stage A result and plan status. Do not describe `init_failed` or an unproven timeout as proof that no card is physically present.
5. If a card is detected, stop before mounting or writing. Discovery provides geometry and a salted identity digest but does not back up the card, establish filesystem contents, establish ownership, or authorize formatting. Define and execute the plan's media-preservation and ownership gate first. No person is available to insert/remove media, so any gate requiring a physical action remains unproved unless a verified remote mechanism exists.
6. Only after preservation establishes an authorized mode, write a plan for the next attached-storage stage. Stage B is mount/I/O proof with formatting disabled. Stage C is the identical Opus/PCM cadence diagnostic. Stage D requires dedicated/disposable media and covers ordered interruption recovery, near-full refusal/reclamation, and actual removal/reinsertion through a verified controllable setup. Commit each completed stage before advancing. A partial or shared-media result does not qualify the backend.
7. Do not start the durable device outbox on H32: H32 failed its PCM cadence gate. If attached media fully qualifies, make and commit the explicit dependency decision described at the end of `attached-storage-qualification.md`. Then update the outbox plan and implement the durable outbox demo against the measured backend. Verify capture persistence across server unavailability, retry, reset, and restart, with exactly one completed server message. Use generated or existing nonprivate audio because no speaker is present.
8. Continue remaining long-message work in the committed roadmap order: device playback/streaming, server codec/container integration, whole-message sketch timeline persistence, then X02 integration. Keep product defaults and X02 unchanged until the relevant isolated gates and integration plan are complete.

## Safety and evidence reminders

- User authorization covers unattended server control, test implementation, flashing, generated fixtures, subagents, and commits. A person is not present to speak or manipulate hardware.
- Retain original backups and restore proof privately. Keep commits limited to source, plans, sanitized evidence, and reproducibility instructions.
- The hardware-recovery limits for USB host and serial coexistence remain in the attached-storage plan. H35 uses SDMMC and native USB serial; do not switch to USB host discovery without proving a separate working debug/recovery path.
- Never run a destructive filesystem or media operation on an unidentified/shared medium. A successful SDMMC card initialization is not proof of a writable filesystem or dedicated test ownership.
- The recurring continuation automation `estimate-message-memory-costs` has been set to **PAUSED**. Resume only when the user asks to continue.
