# Game home-screen forwarding

An OpenNOW forwarder is a separate PS5 tile for one GeForce NOW catalog
variant. It starts OpenNOW `PPSA99082` with that variant's ID and store.
OpenNOW restores or refreshes the saved NVIDIA login, resolves the variant,
and requests its stream with saved stream settings. It skips catalog browsing.

## Requirements

- OpenNOW must be installed and registered as `PPSA99082`.
- ShadowMountPlus must scan `/data/homebrew` and support directory titles on
  the console's firmware. It owns registration through its TitleDir bridge or
  `sceAppInstUtilAppInstallTitleDir`. OpenNOW does not edit the shell database.
- A standard launcher must listen on `127.0.0.1:10199`, or a compatible
  elfldr must listen on `127.0.0.1:9021`. Each forwarder carries the standard
  launcher and sends it to elfldr when the launcher is absent.
- Tile creation, listing, and removal require elfldr on `127.0.0.1:9021`.
  OpenNOW streams the bundled `opennow-tile-installer.elf` to it on demand.
  The independent payload runs in elfldr's normal unsandboxed environment;
  OpenNOW itself never changes credentials, process roots, or jail state.
  Missing installer support disables tile operations, not application startup.
- A tile does not grant a game license, change an NVIDIA subscription, or
  make an unsupported game playable. Creating artwork requires Internet access.

After creation, request a homebrew rescan in ShadowMountPlus. After removing
a folder, refresh or remove its registration there. Art is copied during
registration, so changing art requires registration again.

## Launch arguments

```text
--gfn-app-id <positive numeric catalog variant ID> [--store <store>]
```

The ID is `Game::id`, not the parent application's UUID, name, or store
product ID. Lookup uses upstream OpenNOW's `variantIds` filter. The current
GFN API accepts positive signed 32-bit variant IDs. A specified store must
match the resolved variant. Without a store, OpenNOW uses its catalog store.
Arguments are separate strings, not shell syntax.

The standard launcher passes the JSON argument array directly to
`sceSystemServiceLaunchApp`. OpenNOW accepts this array without an executable
name and also accepts a conventional leading `argv[0]`. Duplicate flags,
unknown flags, missing values, nonnumeric IDs, control characters in stores,
and oversized values are rejected. The parser bounds IDs to 95 bytes and
stores to 47 bytes before lookup validates the numeric API range.

For a synthetic catalog variant, the configuration is:

```json
{
  "format": "ps5-forwarder/1",
  "target": "PPSA99082",
  "creator": "OpenNOW PS5",
  "args": ["--gfn-app-id", "12345", "--store", "XBOX"]
}
```

`12345` is a test value, not an asserted ID for any released game.

With no saved account, sign-in remains on screen until authorization succeeds.
Unknown, unavailable, unentitled, and mismatched-store variants report an error
without allocating a session. A conflicting remote session is not stopped or
adopted. Circle returns from forwarded launch to the normal launcher without
signing out. During a stream, normal session controls apply.

## Writer API and UI integration

`src/forwarder/forwarder.hpp` exposes synchronous worker operations without
exceptions or RTTI:

- `create(const Game&, const Options&, const Job&)` creates a tile.
- `list(Visit, void*, const Paths&)` visits recognized OpenNOW tiles.
- `remove(const char* titleId, const Paths&)` removes one recognized tile.
- `Result` carries a status, title ID, count, and native error code.
- `message(result.code)` returns displayable status text.

The game-detail UI can wire **Add to Home Screen** as follows:

1. Copy the selected `Game` while holding the catalog-view mutex. Do not pass
   a pointer into a mutable catalog page.
2. Disable the action when `forwarder::available()` is false or a stream is
   active. Keep at most one operation queued.
3. Run `forwarder::create(game, {}, job)` on a worker. A 1 MiB stack exceeds
   the bounded encoder and file-operation frames. Do not run downloads,
   encoding, or filesystem work on the render or active streaming worker.
4. Publish progress through the UI's mutex or event queue. Callbacks execute
   on the calling worker and must not reenter writer operations.
   `Job::cancel` points to an atomic flag that must outlive the operation.
5. Show the result message and title ID. Treat `alreadyExists` as success.
   Offer the ShadowMountPlus rescan instruction.
6. Use `list` for management, and pass its selected `titleId` to `remove`.
   Refresh the list after completion.

`Options` accepts an injected artwork fetcher. Alternate `Paths`, including
`stageRoot` and `collisionRoots`, apply only to host tests. Native operations
always use the bundled template, sandbox staging directory, and fixed installer
inventory roots. Custom scan roots are not accepted over the protocol.

The existing UI worker keeps its `Job`/`Progress` interface. `Phase::installing`
reports completed and total upload bytes. The sheet displays those counts in
KiB. No component under `src/ui/` changes here.

## Installer boundary

The app prepares a complete folder at
`/download0/opennow/tile-stage/job-<pid>-<sequence>/`, including both program
files, both JSON files, the icon, and both backgrounds. It synchronizes the
folder before submitting an install request and removes it after the reply.
Artwork downloading and encoding stay in the sandbox.

The versioned, little-endian protocol uses the same socket as the ELF stream.
Requests contain an operation, a request ID, and a title ID only for removal.
Installation then streams the seven whitelisted staged files in 64 KiB chunks,
with bounded 64-bit file lengths and an end frame. Requests never contain a
PID, stage name, arbitrary path, or credentials. Fixed frame sizes, bounded
entry counts, socket timeouts, and matching request IDs make malformed or
incomplete exchanges fail closed.
The packaged ELF must end exactly at its section table so protocol bytes
cannot be consumed as part of the executable.

The installer writes a fresh hidden upload folder under `/data/homebrew`,
synchronizes it, and passes it to the shared atomic installer. It removes the
upload folder after success, failure, or disconnect. It never opens the app's
sandbox or depends on process-title or root metadata. List and remove also
need no PID check. The [streaming design](TILE_INSTALLER_RESOLVER.md) records
the whitelist, caps, cleanup, and failure reporting.

`available()` lazily probes the bundled payload and elfldr from a detached
worker, caches the answer, and returns without blocking the render thread.
It performs no startup privilege operation and does not prove that a later
installation will succeed.

## Files, IDs, and atomicity

The writer follows [PS5 Forwarder Format version 1](../vendor/ps5-forwarder-format/SPEC.md).
Each tile contains the shared `eboot.bin` and `sce_module/libc.prx`, standard
`forwarder.json`, and `sce_sys/param.json`. The title name is the game name,
the category is game, and the content version is `01.000.000`.

Allocation starts at a stable hash of the variant ID and store within
`PPSA99200` through `PPSA99899`. It skips registry reservations, occupied
folders, embedded title IDs, installed app-metadata roots, and the default
internal and external homebrew scan roots. OpenNOW's own ID is never used.
An exclusive empty-directory reservation prevents cooperating writers from
allocating the same path. Existing apps and tiles are not overwritten.
Repeating creation for the same ID and store returns the existing tile,
including one made by Forwarder Manager with the same argument contract.

No offline allocator can reserve IDs against disconnected storage, future
installations, or an unlisted scan root. Attach relevant storage and do not
run another tile manager concurrently. PS5 has no private
title-ID allocation range that guarantees immunity from future installations.

The icon is a 512 x 512 PNG. Both backgrounds are 3840 x 2160 DX10 BC7 DDS
files, using hero artwork or the cover when no hero URL exists. Fetching,
JPEG validation, decoding, and cropping reuse existing artwork code. BC7
encoding resamples a 960 x 540 decoded image and streams blocks to disk,
without allocating a full 4K image or DDS buffer. PNG encoding streams
stored-deflate scanlines. Downloads remain capped at 1.5 MiB and source JPEGs
at 2048 x 2048. Publisher artwork is downloaded, not distributed with the app.

The payload validates the staged tree, rejects unknown files and symlinks,
and verifies both pinned program hashes. It copies the permitted files into
a hidden sibling folder under `/data/homebrew`, regenerates `param.json`
with the allocated title ID, synchronizes the files and directories, and
renames the hidden folder over its exclusive empty reservation. Metadata,
allocation, validation, copying, listing, and removal share one implementation
in `src/forwarder/tile_store.cpp`, compiled by the payload and host tests.

The mounter never sees incomplete metadata. Cancellation during upload closes
the socket and cleans both transfer stages. After the end frame, installation
is not cancellable; its final result takes precedence over a late cancel request.
A power loss may leave a hidden upload, hidden stage, or empty reservation, which can be
removed while OpenNOW is closed. Existing valid tiles are not rewritten on
retry. Removal renames a recognized
tile out of the visible namespace before deleting it. An ownership record
allows interrupted deletion to finish on retry. Symlinks are not traversed.

## Verification and console testing

`bash tools/test-forwarder.sh` tests metadata against pinned `psfwd`, title-ID
collisions and exhaustion, standard-reader interoperability, cancellation,
failed publication, interrupted removal, symlink safety, argument parsing,
streamed-file validation, real dispatcher-to-store uploads, protocol framing
and replies, launch resolution, and encoded images. `bash tools/test-port.sh` includes
these checks under ASan and UBSan. `tools/verify-forwarder-template.py` checks
both vendored program hashes. The writer checks them before creation too.

After committing the source and running the native GPU build, import audit,
and `tools/package-manifest.py`, run
`python3 tools/package-tile-test.py --output ~/.capy/work/build-stream-tiles/`.
It rejects a dirty or mismatched source commit, verifies every packaged hash
and required asset, checks the installer ELF extent, and writes a deterministic
test ZIP with its source manifest and a separate `SHA256SUMS` file.

Console tests are still required for streamed installation through elfldr,
unchanged sandbox font/certificate/storage access, GPU qualification, native
directory enumeration and writes, ShadowMountPlus registration and
removal, artwork display, launcher bootstrap, argv delivery, saved-login
refresh, and direct game streaming. Host and build checks do not prove those.

## Research sources

The implementation was checked against actual source in:

- `ps5-forwarder-format`: `SPEC.md`, `src/psfwd.c`,
  `forwarder/src/main.c`, and `launcher/launcher.c`.
- `ps5-forwarder-manager`: `src/fwd/store.cpp` and `src/fwd/image.cpp`.
- ShadowMountPlus: `include/sm_paths.h`, `src/sm_gameinfo.c`,
  `src/sm_scan.c`, and `src/sm_install.c`.
- `ps5-payload-dev/websrv`: `src/ps5/sys.c` and `src/ps5/hbldr.c`.
- etaHEN: `Source Code/util/source/common_utils.c`, `launchApp`.
- OpenNOW desktop: `native/opennow-core/src/gfn/catalog.rs`,
  `gfn/catalog_actions.rs`, `cloudmatch.rs`, and `artwork_cache.rs`.

Redistributed source and binary pins are in `upstream-lock.json` and
[third-party notices](../THIRD_PARTY_NOTICES.md).
