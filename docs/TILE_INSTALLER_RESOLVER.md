# Streaming tile installation

OpenNOW stays sandboxed. It prepares its complete seven-file tile in
`/download0/opennow/tile-stage/`, then streams those files through the socket
used to launch `opennow-tile-installer.elf` with elfldr. The payload never needs
to find or open OpenNOW's sandbox. Listing and removal also run independently
of the app PID, title metadata APIs, or kernel file-table visibility.

Release `00.003.000` attempted to resolve the app's root from process metadata.
Console installation returned `invalidGame` with `EPERM`, despite a working
transport. Version `00.003.001` removes that dependency rather than trying a
different AppInfo layout or guessing a sandbox directory.

## Wire format

Protocol version 2 keeps the fixed-endian `ONTI` header, request IDs, bounded
result/list frames, and five-second per-I/O timeout. An install request carries
only its operation and request ID. A remove request also carries a title ID.
No request carries a PID, stage name, arbitrary source path, or credentials.

Each install request is followed by one file frame and its raw bytes for every
required file, then an end frame. A file frame includes a relative path and
64-bit byte length. Both endpoints stream in 64 KiB chunks instead of holding
the tile in memory. Only these exact paths are accepted:

| Relative path | Maximum bytes |
| --- | ---: |
| `eboot.bin` | 2097152 |
| `sce_module/libc.prx` | 2097152 |
| `sce_sys/param.json` | 16383 |
| `sce_sys/icon0.png` | 2097152 |
| `sce_sys/pic0.dds` | 8294548 |
| `sce_sys/pic1.dds` | 8294548 |
| `forwarder.json` | 16383 |

Files must be nonempty. Unknown names, traversal, duplicate or missing files,
oversized lengths, wrong request IDs, invalid padding, truncated frames, and
short file bodies fail closed. The client reports upload bytes through the
existing `Phase::installing` progress callback.

## Filesystem boundary

The payload creates an exclusive hidden `.opennow-upload-*` folder under
`/data/homebrew`. It creates only the required subdirectories and writes files
with exclusive, no-follow opens. After synchronizing the files and directories,
it calls the existing shared `store::install` implementation. That validates
metadata, pins both program hashes, allocates a collision-free title ID, and
publishes a synchronized hidden sibling by atomic rename. Existing valid tiles
are not overwritten. Inventory ignores hidden uploads, including incomplete
metadata left by another transfer.

The payload removes its upload folder after success, rejection, or disconnect.
A power loss or forced process termination can leave a hidden upload folder;
it is not a visible title and can be removed while no installer is running.
The app removes its own sandbox stage after the request. Cancellation during
upload closes the connection; once the end frame is sent, the installer must
finish and its result takes precedence over a late cancel request.

Client stage-read failures, socket failures, protocol/metadata rejection, and
payload upload-storage failures have separate result codes and messages.
Payload filesystem failures drain only the remaining bounded, valid stream
before returning their error so they are not disguised as a broken pipe.

The independent payload uses the ordinary public SDK CRT, which adjusts its
own process capabilities during startup. No app PID is supplied to that code.
OpenNOW uses its separate application CRT and never changes its root or
credentials for tile operations. The socket belongs to the already privileged
elfldr environment; this protocol is not a general-purpose authenticated service.

## Verification

`tests/installer_protocol_test.cpp` exercises framing, lengths, whitelisted
paths, partial I/O, result validation, progress, and cancellation.
`tests/installer_stream_test.cpp` pipes a real staged tile through `wire::Io`
into the actual payload dispatcher and shared store under temporary roots.
It covers installation, idempotence, listing/removal, rejected transfers,
storage errors, and cleanup. `bash tools/test-forwarder.sh` runs these tests
with ASan and UBSan; `bash tools/test-port.sh` includes them in the full suite.
Native GPU compilation, import auditing, and ELF extent validation remain
separate checks. Console installation and ShadowMountPlus registration still
require hardware validation.
