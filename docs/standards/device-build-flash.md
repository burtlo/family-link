# Device bootstrap build and flash

The first v2 image is a fresh ESP32-S3 bootstrap: USB diagnostics and a sleeping
idle task. It has no display, network, media, user session or endpoint credentials.
Kit nicknames select physical positions; anyone may later log into any endpoint.

## Daily commands

```text
make build
make devices
make flash.mazi
make flash.arlo
make flash.lynn
make flash.audrey
make flash.all
```

`make build` requires the configured ESP-IDF version but no USB kit or roster.
It builds `client/device`, records size data, and publishes an immutable package
under `artifacts/device/bootstrap/<build-id>/`. A successful package atomically
updates `current.json`; a failed build leaves the previous package selected.
The merged `firmware.bin` contains bootloader, partition table and application;
the package also retains component images, ELF, effective sdkconfig, partition
layout, IDF flash metadata, size report and SHA-256 hashes.

`make flash` requires exactly one connected registered kit. Named targets resolve
deployment alias -> endpoint -> physical kit -> USB serial -> current port.
There is no remembered-port or first-device fallback. `make flash.all` preflights
all kits in the active roster before any write, then processes them sequentially.
Failures return nonzero with a result per kit. Connected-device discovery is
passive and does not reset devices.

Flashing uses the existing package and verifies its hashes and configured
target/toolchain. It never builds, stamps identities, provisions credentials or
automatically binds kits. It writes the component images at IDF-generated offsets
rather than writing merged-image padding across data gaps. The partition table
is replaced: migrating existing data or layout is outside this bootstrap contract.
There is no automatic full-chip erase.

Each write is followed by bounded serial capture looking for that package's
`V2_BOOTSTRAP_READY build=<build-id>` marker. The firmware repeats the marker every
two seconds to allow attachment after reset. Logs are retained under
`logs/device/<run-id>/`. A confirmed write without a matching marker is reported
as boot unconfirmed, not a successful runtime verification.

## Explicit USB binding

Use a real local deployment roster (copy the example if needed):

```text
make devices
make device.bind KIT=arlo PORT=COM7
```

Binding updates only the selected kit's USB serial in the local YAML, validates
before replacing the file, and refuses serials already assigned to another kit.
On macOS use the current `/dev/cu.*` port instead. Confirm the physical kit before
binding; USB serial identification does not establish a person's identity.

Settings use the existing host INI/environment resolution: `firmware.source`,
`firmware.build`, `firmware.artifacts`, `firmware.logs`, `firmware.boot_seconds`
and `idf.*`. Scripts activate ESP-IDF internally. `make install` remains the
explicit setup step. Build/flash never install dependencies automatically.

`make test` covers selection and package failure boundaries. Build success is
L0; matching serial boot evidence supports the bootstrap path on that target.
Display, timing, media and application behavior remain unverified until added.
Flash commands are excluded from automatic implementation/repair flows.

Future presentation, single-writer session and worker components must follow
[client coding standards](client-application-coding-standards.md). Endpoint
credential provisioning remains a separate later implementation decision.
