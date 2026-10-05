# Plan: Qualify BOX-3 attached message storage

| Field | Value |
|---|---|
| Doc kind | Experiment plan defined before implementation |
| Status | Corrected H35 retry detected SDMMC; strict allowlist/public-mapping review and independent full restoration review passed; discovery-only, preservation/ownership/filesystem gates remain open; no attached backend qualified |
| Date | 2026-10-05 (status updated after H35 implementation commit `5170a92`) |
| Scope | Isolated BOX-3 storage fixture, host controls, strict evidence parser |
| Product partition choice | Open: single-factory versus dual OTA |
| Next gate | Read-only preserve the detected 62,209,916,928-byte medium and resolve ownership before any mount or write; default to preservation-only while ownership is unknown |

## Reason and boundaries

H32 remains unqualified. Its default-yield PCM run missed 79 of 90 deadlines; the no-yield run missed 44, including 33 service misses, despite average transaction throughput above 64,000 B/s. Opus passed that diagnostic. Neither run reached fault recovery or near-full admission. The original complete 16 MiB image was restored and independently verified. The [16 KiB write-coalescing proposal](h32-write-coalescing-correction.md) closed at its read-only source gate: one 4 KiB sector per FAT cluster prevents the proposed reduction in physical data requests. These findings motivate a separate backend experiment, not a relaxed cadence gate.

The user reports “some storage attached.” This is not proof of USB MSC, SENSOR microSD, a particular filesystem, or a BOX-3-visible device. Identify the actual medium before selecting a driver. A disk mounted on the Mac or server is not a BOX-3 backend.

Unattended demos, flashing, and server control are authorized. No person is available to insert, remove, swap, or inspect media during this run. Use deterministic nonprivate fixtures and automatic host controls; defer physical actions that cannot be performed with verified existing controls. This plan itself performs no implementation or device mutation.

Keep X02, `firmware/v1/`, product defaults, production partitions, server behavior, and the existing failed H32 evidence unchanged. Use a new island demo, private epoch, separate build/configuration, and explicit backend selection. Do not repurpose H32 captures as attached-storage results. Do not burn eFuses or change boot security. Prefer the original partition map if the isolated application fits with the existing headroom policy; if not, define and validate a demo-only fixture before flashing. A fixture never decides the production partition layout.

## Stage A — Identify and preserve (read-only first)

1. Read [hardware facts](../HARDWARE.md), [media constraints](../STORAGE.md), and [USB sharing constraints](../UVC-CAMERA.md). Inventory serial ports and select exactly one BOX-3 by recorded identity. Recheck detected chip, flash size, running firmware, partition map, and the preceding full-image restore proof. Ambiguity is a stop.
2. Inspect available local inventory and existing accessory information without treating old kit descriptions as current physical proof. Record the hypothesis and confidence. DOCK USB-A supports the candidate USB-host MSC path and occupies the camera port; DOCK USB-C is power only. SENSOR provides the candidate SDMMC microSD path and replaces the dock. Its documented SDMMC GPIOs are 9/11/12/13/14/42; confirm against the pinned BSP and hardware revision before configuring pins. A verified unattended programming/debug and recovery transport must remain available while exercising the chosen path. The repository expects a BOX-3B on a dock; that is a discovery hypothesis only. Local pinned ESP-IDF v5.4.2 includes `examples/peripherals/usb/host/msc`, which can inform a read-only MSC discovery fixture; there is no existing repository MSC demo. Re-enumerate the serial port rather than relying on a historical port name.
3. If existing firmware cannot enumerate the accessory, prepare only a read-only isolated discovery image after the device backup gate. **Before any USB-host image is flashed**, prove from the exact board schematic and pinned source that the host can run while the current native USB Serial/JTAG programming/debug channel remains usable, or identify an already connected independent UART/network control and a verified unattended recovery path. The ESP32-S3 USB-OTG and USB-Serial-JTAG controllers share the internal PHY; [Espressif's USB Host guide](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html#external-phy-configuration) requires an external PHY for simultaneous host use and USB Serial/JTAG. Do not burn an eFuse or flash a host image that could sever the only control channel. If coexistence and recovery cannot be proved, stop the USB path before flashing and consider the SDMMC path only if its actual accessory and pins can be identified safely. USB discovery records host enumeration, class/subclass/protocol, LUN, capacity/block size, and available device descriptors; SD discovery records host initialization, card identity/capacity, and negotiated bus settings. A successful host enumeration alone does not prove a mounted writable backend. Do not blindly probe unsupported pin combinations. If neither safe path detects a device, stop with `no_device` evidence and restore.
4. Preserve fresh full 16 MiB and separate original NVS backups privately outside the repository, verify byte counts/SHA-256 and relevant readback, and review the exact complete-image restore command against the selected device and port before any flash. Preserve existing namespaces; if a sentinel is needed, use separate experiment sentinel/progress namespaces and retain the pre-sentinel original image. Do not reuse another epoch's authority.
5. Once a BOX-3-visible medium is identified, read geometry, partition table, filesystem signature, mount capability, capacity/free space, and directory metadata without changing it. Disable formatting, repair, `fsck` writes, and automatic initialization. Distinguish read-only block access from a driver/filesystem whose mount can write metadata; obtain preservation first if truly read-only mounting cannot be guaranteed. Unsupported filesystem is a stop, not permission to format.
6. Store a complete block image of the identified medium privately when feasible, verified by byte count/hash and an independent reread. Record source identity/geometry privately and backup coverage explicitly. Do not stream or print user filenames or contents into committed evidence. If full backup cannot be obtained, do not imply that a file copy protects filesystem metadata or fault injection.

**Stage A pass:** one device-visible medium and transport are bound to an epoch; actual filesystem and geometry are established; device restore is possible; media preservation coverage and allowed experiment mode are explicit. Discovery does not qualify storage.

## Media authority and exact write boundaries

Default to preservation. There is no authorization to erase an arbitrary attached disk. Existing user data, unknown contents, mount failure, or an empty directory listing do not establish a dedicated blank test medium. “Blank” requires complete-medium read evidence plus explicit dedicated-test provenance; an existing empty filesystem requires complete namespace inspection including hidden entries, partition inspection, and dedicated-test provenance. Partition gaps, unrecognized sectors, or another partition remain unknown data until resolved.

Select and record exactly one mode:

| Mode | Allowed work | Qualification limit |
|---|---|---|
| Read-only / unknown ownership | Discovery and preservation only | No write diagnosis |
| Preserved shared medium | Bounded new reserved directory, clean mount/I/O/cadence only | Fault/removal and physical near-full blocked |
| Verified empty dedicated or disposable medium, fully preserved | Full isolated campaign | Eligible for full qualification |

A verified backup is not permission to format or fill shared media. Formatting is allowed only on the verified empty dedicated test medium, at explicitly reviewed partition/LBA boundaries with filesystem choice and geometry recorded. Never select a whole disk when authority covers a partition. Default all mount APIs to `format_if_mount_failed=false`; subsequent unexpected mount failure stops writes and preserves the medium.

For shared-media diagnostics, create one collision-resistant mount-relative directory, for example `/family-storage-qual/<epoch>/`, only after preservation and ownership are established. Refuse an existing name. Bind a marker to epoch, backend identity, source/build hashes, and mode. Canonicalize every path and reject traversal or operations outside the marker-bound directory. Define an absolute byte/file ceiling before running: at least the 12,096,000-byte retained PCM workload plus 360,000-byte Opus workload, manifests, probes, temporary siblings, and metadata margin, but bounded by measured free space and a conservative shared-medium reserve. Calculate the exact ceiling from the implemented fixture before writing. Stop if it cannot fit. Never enlarge the ceiling unattended. No writes outside that directory except unavoidable filesystem metadata; document that filesystem-wide effect. File confinement cannot make reset or removal safe for unrelated data.

Publish a write manifest before mutation: on-chip flash offset/length for every flashed artifact, NVS namespace/region, removable partition/LBA authority for any format, test root, byte/file ceilings, mount/format flags, and deletion scope. Review independently. Clean up only files positively owned by this epoch; never recursive-delete an unverified root. Full medium restoration, if needed, uses the verified original block image and exact original boundaries, followed by independent readback. Do not restore a stale block image over concurrent outside changes; exclusive access and revalidation are required. Otherwise stop and retain private backups.

## Stage B — Build and prove mount/I/O

Before implementation, independently review and commit this plan. Implement a dedicated demo/configuration/build directory and host controller with commands for inventory, backup, read-only discovery, prepare-run, validate-build, flash, capture, parse, cleanup, and restore. Record pinned ESP-IDF/BSP/MSC dependencies and effective settings. Choose only the detected transport. Confirm USB host/programming coexistence, host power control, transfer timeout/error handling, or SDMMC pin/bus ownership from local pinned sources; no assumptions about camera coexistence or SD speed class.

Build gate: application size/headroom, decoded partition table, flash-offset manifest, source/config/binary hashes, and device identity must match. Unrelated product files must have no diff. Build a strict parser alongside versioned records before hardware execution; synthetic parser cases are software validation, not hardware evidence. Commit reviewed implementation before the hardware phase.

Mount without automatic format, record cold/warm mount time, capacity/free bytes, filesystem/cluster/sector configuration, driver settings, and error codes. Write and durably close a deterministic probe, rename, full SHA-256 readback, then perform exactly five initial reboot/remount probe cycles only on the dedicated medium. Keep later retained-probe checks distinct from these five identities. Shared mode uses five clean unmount/remount cycles and is explicitly not reboot recovery proof.

Run 20 iterations each of deterministic 64 KiB and 192 KiB files: open, write, `fflush` plus `fsync`, close, same-directory rename, reopen/full read/hash, manifest replacement, enumeration, delete, and measured reclamation. Record replacement rename behavior and whether file/directory durability mechanisms are supported by this filesystem/driver. An API success does not prove the medium persisted its internal cache. Preserve retained probes and files across the allowed remounts. Unexpected integrity, authority, timeout, watchdog, or mount failure stops the run.

**Stage B pass:** identified medium mounts and completes integrity/I/O checks in the authorized mode. Shared mode may pass bounded diagnostics while reboot/fault durability remains unproved.

## Stage C — Identical recording cadence diagnostic

Use H32's deterministic workload, seeds, transaction ordering, and retained-file/reclamation behavior as the comparison contract. Run 90 two-second Opus-sized chunks at 2,000 B/s (4,000 bytes/chunk), then 90 two-second PCM chunks at 67,200 B/s (134,400 bytes/chunk). These are generated size/pattern fixtures, not an encoder or live microphone proof. Each transaction includes payload write, flush/fsync, close, rename, full readback/SHA-256 verification, and durable manifest replacement. Do not pre-generate a whole recording, batch manifests, omit checks, reduce rate, or extend the two-second period.

Record scheduled/actual start and finish, lateness, skipped periods, service and scheduled deadline misses, cumulative schedule, backlog high-water, actual wall interval, and summed service time. Record individual open/write/flush/close/rename/read-verify/manifest/total timings and write-plus-flush distributions (min/median/nearest-rank p95/max). Report payload and ordered aggregate checksums, free space, heap/PSRAM minima, allocation capabilities/size, stack watermarks, and allocation failure handling.

Keep an independent heartbeat with interval, timer resolution, priority/core, sample count and maximum gap. Record effective task/interrupt watchdog settings and events; do not inherit H32's disabled task watchdog without an explicit fixture justification. Capture budgets fail closed: first byte 45 seconds, discovery/mount milestone 240 seconds, output stall 90 seconds, three panic reports, total wall cap 90 minutes; refine a milestone only with documented source/measurement evidence before the run. Host monitoring must remain responsive and report meaningful progress at least each minute.

**Stage C pass:** both complete streams have zero scheduled/service misses, no growing backlog, intact checksums, and durable PCM transaction throughput above 64,000 B/s. Averages or 90 generated records cannot substitute for deadlines. Heartbeat and watchdog results describe this island, not microphone/network/UI concurrency. If either fails, stop before faults/floor, preserve exact terminal failure and partial serial bytes, and restore. A cadence pass alone is not backend qualification.

## Stage D — Full interruption and capacity campaign

This stage requires dedicated/disposable media authority. Shared/unknown media cannot enter it even if cadence passed. Preserve all prior committed probes/chunks and validate them after each recovery without formatting.

Run the complete ordered nine-point by ten-repeat matrix (90 fault/recovery pairs), retaining H32's exact boundaries:

| Point | Interruption boundary |
|---|---|
| 0 | After 128 buffered payload bytes, before flush |
| 1 | After 32,768 buffered bytes, before flush |
| 2 | After 65,535 buffered bytes, before flush |
| 3 | After 65,536 bytes and fsync, before close |
| 4 | After close, before rename |
| 5 | After rename, before manifest |
| 6 | After durable manifest, before delete |
| 7 | After final-file delete, before manifest delete |
| 8 | After both deletes |

Persist cursor separately from the tested file, bind it to epoch and medium, and advance only after matching recovery. Record stdio buffering and precise injection semantics: buffered bytes are not claimed as physical sectors written. Validate prior files, identify/quarantine partial/orphan files, require manifests to agree with valid final files, and retain namespace inventories/checksums. Label software restart, EN reset, host disconnect, and switched power separately. The baseline 90 software-reset pairs do not establish actual power-loss durability; run switched-power repetitions only with an identified remotely controllable safe setup. Otherwise mark power loss unproven.

Derive a conservative free-space floor from the largest chunk plus temporary sibling/replacement manifest, measured allocation and directory growth, driver/filesystem overhead, and recovery margin. On dedicated media only, fill with epoch-owned validated files in bounded steps, enforce a maximum time/bytes budget based on actual capacity before starting, and demonstrate refusal before the floor, prior-file integrity, repeated remount, deletion, and expected reclamation. A huge medium may require a separately reviewed dedicated test partition; do not resize existing partitions or fill an arbitrary large disk merely to reach the floor. Quota-limited directory or simulated ENOSPC tests may demonstrate admission logic but cannot prove physical near-full filesystem behavior. Without safe physical near-full evidence, full qualification remains blocked.

Exercise media unavailable at boot, clean unmount, read/write/flush failures, and removal during write/flush/rename/recovery, followed by reinsertion and identity recheck. Actual removal is performed only if a verified remotely controlled mechanism exists and the medium is dedicated; driver-injected failures remain clearly labeled simulations. On unavailable media, stop capture safely, retain committed chunks, expose an intelligible storage-unavailable state, never mark data sent, and never fall back silently to RAM/on-chip storage. Reject a replacement medium or marker mismatch. No user present means inaccessible physical removal remains an explicit unproved gate; do not claim it happened.

**Stage D pass:** all 90 ordered recoveries and dedicated near-full/refusal/reclamation checks pass, and actual media removal, reinsertion, and identity recheck succeed through a verified remotely controllable setup. Without that mechanism, Stage D is partial or blocked even if simulated removal paths pass. Power-loss limits remain explicit; a simulator cannot satisfy an actual-removal gate.

## Evidence, restore, review, and handoff

Store sanitized evidence under `docs/evidence/attached-storage-qualification/<epoch>/`: inventory and authority verdict, build/write-boundary manifest, mount/I/O/cadence CSVs, ordered fault results, floor/removal results, parser verdict, summary, and restore proof. Preserve raw capture/metadata, flash/NVS/media images, serials, paths, credentials, filenames and household data privately. Commit only redacted identities and aggregates; bind private/raw and sanitized captures by byte count/SHA-256. Record Git revision, pinned tools, source/config/binary/partition hashes, backend profile, run epoch, observed medium geometry/configuration, reset class, measurement definitions, and measured versus derived values.

The versioned strict parser requires exact epoch/backend/mode/build binding, phase order, unique initial remount identities, complete later retained-file checks, both I/O matrices, all 180 ordered chunks and aggregates, timing/backlog gates, and all 90 matching fault/recovery pairs for full mode. Reject duplicates, missing/truncated records, wrong identity, unexpected format, checksum disagreement, authority breaches, or a premature completion record. Discovery, shared diagnostic, cadence, full campaign, and restoration each receive separate verdicts; never map a partial run to full acceptance. Preserve failure/not-reached records reproducibly.

After every flashed epoch, success or failure, restore the complete original BOX-3 image and independently compare all 16 MiB against its preserved original SHA-256; decode/check original partitions, original pre-sentinel NVS, application descriptors, and private device identity, then capture restored boot. For shared diagnostics verify pre-existing files privately and remove only epoch-owned files after evidence capture; compare baseline inventory/content integrity. For dedicated-media campaigns restore the original medium block image when required by the declared exit state and verify complete readback/geometry. Failure to restore is a blocking outcome, never a qualified backend result.

Request an independent review of safety boundaries and parser before flashing, and of raw-to-sanitized calculations, complete gate coverage, and restore proof after execution. Commit reviewed implementation, then reviewed run evidence and resulting plan status before advancing phases. Review findings are resolved with concrete evidence; neither review nor a commit substitutes for hardware gates.

Expose a backend-neutral contract: identity/availability, explicit mount/unmount, total/free bytes, bounded admission/reserve, open/read/write/flush/close, atomic replace semantics, enumerate, checksum, delete/reclaim, recovery, and structured errors. Keep USB/SD/FAT details outside queue semantics. Do not add queue/upload behavior in this qualification island.

The current [durable-outbox plan](durable-outbox-demo.md) explicitly requires qualified on-chip storage first. This document does not silently replace that dependency. If attached full qualification succeeds, make and commit an explicit dependency decision before queue implementation: name this measured backend/medium profile as the first backend, amend that plan's target/background/Phase 1 acceptance and optional-removable wording, link completed evidence/floor/recovery/removal limits, and retain H32 as failed. Partial discovery/cadence or a blocked physical floor/removal campaign cannot advance the existing outbox prerequisite. Product single-factory versus dual OTA and later real audio/network/UI integration remain open.

## References

- [On-chip qualification and original safety contract](onchip-storage-qualification.md)
- [No-yield failure and original-image restore](../evidence/onchip-storage-qualification/no-yield-20261005-failure/summary.md)
- [Coalescing preflight no-go](../evidence/onchip-storage-qualification/coalescing-preflight.md)
- [Durable outbox dependency](durable-outbox-demo.md)
- [Protocol](../MESSAGE-PROTOCOL.md) and [architecture](../LONG-MESSAGE-ARCHITECTURE.md)
