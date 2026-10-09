# H38 correction: fixed marker buffers

## Observed failure

The stack-corrected epoch passed BIND and exact MBR readback, explicitly formatted the bounded volume as FAT32 (1,055,744 format bytes), validated its BPB, and mounted successfully. IO then failed with ESP_ERR_INVALID_SIZE before its first matrix record. This remains unqualified; the controller must finish authoritative abort and full existing-image restoration.

## Proposed correction

The marker contains the fixed epoch, runtime ELF, source revision, intent, H35 reference, CID binding, and approved geometry/profile metadata. Its 512-byte write, verification, and inventory buffers are smaller than that fixed content. Preserve the exact marker schema/content and the frozen 4,096-byte maximum; use one named 1,024-byte buffer capacity at every marker call site, only after an independent exact length check proves it sufficient for the fixed validated inputs. Preserve snprintf truncation rejection, file transaction ordering, marker verification, byte counters, and retained-file limits. Do not increase global record, generated-file, SD phase, or storage bounds.

Before hardware, compile and run the actual marker_contents C function off device with full-width synthetic identifiers, prove the old 512-byte buffer rejects it, prove the new capacity succeeds, and exercise exact terminator/truncation boundaries. Check that every marker write/verify/inventory buffer uses the same capacity. Independently review the new linked stack paths, source/profile bounds and exact artifacts. Use another immutable epoch bound to the previous actual reviewed MBR readback; preparation waits for verified prior recovery and independent layout-reference approval. BIND still rereads the live MBR before writes. The prior formatted volume receives no qualification credit, and this is a full new layout/format/I/O attempt.

Keep all private sectors, hashes, identifiers, captures and images private. Product/X02 and later cadence/fault/outbox work remain outside this correction.
