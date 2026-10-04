# Port status — 2026-10-04

## Architecture decision

OpenNOW desktop at the revision in `upstream-lock.json` is a Qt Quick shell, a Rust account-service process and an in-process Rust NVST streamer. Its platform implementations cover desktop operating systems. The PS5 native build uses a FreeBSD-derived ABI, a clean-room libc companion and a constrained C++ runtime with exceptions/RTTI disabled. A Linux binary or Flatpak is not a PS5 executable, and selecting the Linux Rust backend would not supply PS5 audio, video or input.

The OpenCloudGaming organization also maintains OpenNOW-Switch. Its C++ `app/src/gfn/authentication.cpp` provides a smaller starting point for the account flow, but depends on exception-based services, Jansson, platform storage and a separate libpeer streaming stack. This prototype ports that device-login behavior, replacing exception/dynamic-string handling with a bounded state machine and cJSON. It does not claim the whole upstream client is running.

Native platform code is based on the pinned PS5 native application boilerplate. The socket adapter and libcurl compatibility implementation derive from ProsperoLight. Their provenance is recorded separately. No Moonlight streaming code is wired into this prototype.

## Evidence obtained

1. Host tests exercise form escaping, verification-host checks, polling intervals, pending/slow-down, expiry, denial, malformed JSON, server backoff, profile rejection and successful account verification using synthetic responses.
2. The real device-authorize endpoint returned HTTP 200 with `verification_uri` on `https://static-login.nvidia.com/service/gfn/pin`, a 900-second expiry and a five-second polling interval. The temporary challenge was discarded without account authorization. No codes or response bodies are retained in logs or fixtures.
3. Docker builds and native packaging run on `vps`. Static libraries need all application symbols hidden; the initial converter rejected visible compatibility symbols. The final linker policy hides them.
4. PacBrew libcurl/OpenSSL reference libc APIs not all usable in a native title. Native DNS/socket adapters and compatibility functions are linked into the executable. Certificate-directory functions fail explicitly because the prototype exclusively uses its bundled PEM file. Imports into `libScePosixForWebKit` are checked by `tools/audit-imports.py`.
5. The CPU renderer writes tiled VideoOut buffers. Host preview explicitly untile-converts them, rather than treating them as linear RGB.

## Current acceptance gate: 00.002.000

The user confirmed successful NVIDIA account verification in 00.001.005. Version 00.002.000 adds store-specific catalog rows, CloudMatch allocation/polling/cancellation, NVIDIA WebSocket/NVST signaling and libpeer transport. The PS5 backend integrates software H.264 decoding, Opus/AudioOut stereo and DualSense reports. The first target is 1280×720 at 30 fps, displayed centered on the native canvas. Hardware decoding and account persistence/refresh remain future work.

The streaming dependencies are built from the pinned OpenNOW-Switch source archive. Native compatibility covers socket descriptors, entropy, timers and FreeBSD address structures. Host authorization, catalog/session lifecycle, socket, address parsing, SDP, WebSocket queue and handshake tests pass. The VPS native build and import audit pass. The renderer preview uses synthetic catalog data.

00.002.000 was installed after the user closed OpenNOW. All six files were read back and hash-verified in raw SELF mode; the prior title tree is retained for rollback. No diagnostic payload was used. A bounded FTP watcher was started before requesting relaunch; it attempts to preserve system-generated crash dumps and is not an attached debugger.

The requested first test is Minecraft Dungeons 2 on the XBOX store. Availability must be established from the live catalog. Catalog responses, cloud allocation, decoded video, sound and actual controller delivery are still unverified on PS5. Do not describe the build as playable until those observations are recorded.

## First console crash and correction (00.001.001)

The initial build crashed before login on firmware 13.00. A narrowly scoped FTP watcher recovered a 219156-byte `.prosperodmp` matching its manifest; decompression yielded a 2844120-byte ELF core. The target thread was `opennow-auth` (PID 148), SIGSEGV with RIP/fault address zero. The captured stack contained return address `0x4030ec`; the preceding indirect call at `0x4030e6` resolves to `sceRandomGetRandomNumber`. The module was absent from the captured mappings.

00.001.001 removes that import and uses the kernel `kern.rng_pseudo` mechanism from ProsperoLight's entropy backend, with checked return values, bounded chunks and cleared output on failure. A host regression exercises successful multi-chunk reads, short responses and failures; the native build audit rejects reintroduction of the unloaded import. The corrected package was installed with a retained rollback tree and full raw SELF readback verification. The user confirmed that the initial screen now appears. Cross then failed during network socket creation.


## Native network acceptance (00.001.002–003)

The diagnostic build 00.001.002 captured libcurl error 7, `failed to open socket: Protocol not supported`, with native socket errno 43. This happens before an HTTP response or account authentication. The fixed-message diagnostic file contains no headers, bodies or login tokens; its initial 0600 mode prevented FTP reads, corrected narrowly to 0644.

00.001.003 logs socket arguments and retries TCP/UDP creation with protocol zero only when the equivalent explicit protocol is rejected with EPROTONOSUPPORT. Unsupported protocols and other errors are preserved. Host stubs exercise success, fallback, refusal and preservation of the final native error. This is a targeted compatibility attempt; PS5 connection and QR acceptance remain pending.


00.001.003 console testing still failed. The added arguments established `domain=2 type=536870913 protocol=6`: IPv4 TCP with FreeBSD SOCK_NONBLOCK (0x20000000) embedded in the type. 00.001.004 strips SOCK_NONBLOCK/SOCK_CLOEXEC before sceNetSocket and applies SCE_NET_SO_NBIO afterward; failure closes the new socket while preserving the option error. No exec occurs in this native title. Regression tests cover flag translation and cleanup. Hardware acceptance remains pending.


00.001.004 console diagnostics confirmed socket creation succeeds and TLS is reached. It then failed with curl error 60: the hostname was classified as IPv6. The inherited `__inet_pton` rejected AF_INET6 with -1; libcurl's TLS peer classification treats a nonzero parse result as a literal. 00.001.005 replaces that incomplete conversion with the ISC parser from curl 8.18.0, supporting both address families. Tests check DNS names, malformed literals, IPv4, IPv6, embedded IPv4, output preservation and unsupported families. Peer and hostname certificate checks remain enabled. The user subsequently confirmed that login succeeds and the screen reports ACCOUNT CHECK PASSED.

## Streaming sign-in crash and correction (00.002.001)

The first 00.002.000 console test crashed when Cross requested sign-in, before catalog or streaming acceptance. The FTP watcher recovered a 254134-byte compressed core. PID 164 / `opennow-auth` faulted at RIP 0x4044b4, in `Http::request`, on a call to `secureErase`. The fault address 0x7eedd07b8 equals RSP minus eight: the call's return-address push crosses unmapped stack memory. Its callers resolve to `Login::begin` and the worker. The new worker-local Login/Cloud objects and larger authorization header exhausted the default native thread stack.

00.002.001 moves the single worker's large state objects into process-lifetime storage and creates the thread with a checked 2 MiB stack attribute. Attribute initialization/configuration/creation failures remain visible and do not fall back to the small default. The compiled worker frame is 0x778 bytes, excluding saved registers. Host regressions, renderer preview and native build/import audit pass. Console retest remains required.

## Cloud allocation refusal (00.002.002)

After the stack correction, the user reached the catalog and launching Dungeons II on XBOX returned HTTP 500 / CloudMatch statusCode 4. This confirms progress past sign-in, but does not identify the cause of allocation failure or validate streaming. Version 00.002.002 adds the upstream network-test session request, forwards its ID when available, rejects non-numeric launch IDs, and displays bounded statusDescription/unifiedErrorCode/session error code fields without recording tokens or full responses. Host lifecycle/error regressions and the VPS build/import audit pass. The refusal's cause remains unconfirmed pending the console retest.

## Controlled request parity test (00.002.003)

The user confirmed Dungeons II / XBOX launches with the same account in the official GFN client. The port's refusal reports INTERNAL_ERROR_STATUS 8A8C0000, unified 0, session error 1; these fields do not establish a unique cause. Both pinned OpenNOW reference clients include userAge, which the PS5 request omitted. For the next controlled test, use the user's supplied age from `/data/opennow/launch-age.txt`, separate from source and packages, and send it as a JSON number. Invalid/missing configuration blocks launch rather than inventing an age. Tests assert forwarding a synthetic age and rejecting missing configuration. This is a compatibility hypothesis awaiting console validation, not a confirmed fix.

## Allocation progress and signaling address parsing (00.002.004)

After 00.002.003 the user observed five minutes of "Preparing cloud session - queue position 0". The port previously treated all non-ready states as queued and invented zero when no queue field existed. It also ignored RTSP/RTSPS connection resource paths handled by pinned OpenNOW-Switch. This is a confirmed parser gap, not proof of the actual unseen response's state. 00.002.004 translates these resource hosts into WSS `/nvst/` signaling endpoints, accepts usage 14/16, preserves previously received endpoints, and distinguishes allocation, provisioning and ready-without-endpoint states. Queue/setup fields come from seatSetupInfo where present. Unsupported advertisement/unknown states produce explicit errors. Regression fixtures cover RTSP/RTSPS ready responses, missing addresses and provisioning; host tests and native build/import audit pass. The app was closed by the user before replacement.

## Media acceptance pending (00.002.005)

Console testing progressed through cleanup (seat step 5), configuration (step 3) and WebRTC completed. The user heard potentially broken audio but saw no video, then closed OpenNOW. 00.002.005 handles RTP RED primary payload extraction before Opus decoding using the pinned Switch helper and tests; adds a 40 ms audio prebuffer; and displays video RTP/access-unit/drop, decoded-frame/error and audio packet/error counters while no frame is displayed. Periodic keyframe requests also cover the case where no complete access unit reaches the media callback. A video timeout retains the counters for diagnosis. This does not yet establish a video root cause or working playback.

## RTP/RTCP native byte-order defect (00.002.006)

The 0.2.5 console counters showed AUDIO 2000 / ERR 2000 with all video counters zero. Inspecting preprocessor macros with the actual VPS PS5 compiler established that sys/endian.h defines `_BYTE_ORDER`, `_BIG_ENDIAN` and `_LITTLE_ENDIAN`, but not the double-underscore names used by libpeer. Comparing the two undefined macros selected the big-endian bit-field declaration on x86-64. This corrupted RTP payload classification and RTCP fields. 00.002.006 uses checked compiler byte-order constants and reads received RTP payload types directly from the wire/payload parser. A byte-level regression, with legacy macros undefined, verifies RTP receive/send fields and an RTCP feedback header. Runtime playback still requires confirmation.

## Audio confirmed; video routing diagnostics (00.002.007)

After the endian correction, the user confirmed clear audio and AUDIO 2000 / ERR 0, but all video counters remained zero until timeout. This validates the audio path, not video receipt. 00.002.007 adds bounded private media diagnostics: transport RTP counts, SRTP rejection counts, unmatched/routed packet counts, payload-type histogram, decoder counters and whitelisted SDP media/codec/direction lines. No ICE credentials, session IDs, tokens or media payloads are logged. The purpose is to distinguish absent video from routing/decryption failure before changing negotiation or decoding.

## Video transport confirmed (00.002.008 diagnostic)

The private 00.002.007 journal showed negotiated H.264 PT96, thousands of successfully decrypted/routed video packets, no unmatched packets, and zero H.264 parser packets. Audio PT63 played correctly. Failure is between routing and the H.264 assembly callback, not proven to be the decoder. The host upstream H.264 assembly/reordering tests pass. 00.002.008 exposes assembler initialization and distinct rejection codes for missing buffers, oversize packets and RTP header bounds/padding failures, without recording payload bytes. Console root cause remains pending these diagnostics.

## Missing assembler initialization (00.002.009)

The 0.2.8 capture reports ASSEMBLER ready=0 / lastResult=-100 throughout successful PT96 routing. This establishes a missing decoder callback, not malformed RTP headers. It does not alone prove which allocation failed. 00.002.009 uses a fixed, single-owner native BSS arena for the bounded NAL/access-unit/reorder buffers, logs initialization success/codec, and refuses streaming if the callback is absent. An isolated native-arena host test verifies exclusive ownership, cleanup and reacquisition under ASan/UBSan. The VPS build/import audit passed; runtime validation remains outstanding.

## Assembly recovered; FFmpeg rejects input (00.002.010 diagnostics)

00.002.009 reported native buffers ready and decoder callback present. Routed H.264 packets are now processed by the assembler. No displayed frames were decoded; avcodec_send_packet reports -1094995529 (AVERROR_INVALIDDATA). This is an observed decoder-stage failure, not proof that the network stream is corrupt. 00.002.010 adds bounded FFmpeg error-level messages and the size/NAL-type mask of the first three access units, without retaining media payload bytes, to identify the concrete rejection. Native build/import validation is separate from hardware acceptance.

## FFmpeg allocation failure and application heap (00.002.011)

00.002.010 records the first complete access unit including SPS/PPS/IDR (NAL mask 928), followed by FFmpeg "Could not allocate memory", "h264_slice_header_init() failed", and no frame. 00.002.011 integrates the pinned ProsperoLight process-lifetime 128 MiB mspace allocator and malloc/calloc/realloc/free/posix_memalign/usable-size wrappers, with foreign-pointer routing and libc fallback. Native process heap parameters follow its explicit 256 MiB size. This is application-local; no console/global settings or other services are changed. ASan/UBSan host tests with mocked mspace operations exercise allocation, zeroing, alignment, realloc data preservation/fallback and ownership-aware free. These validate wrapper behavior, not the native allocator implementation. The VPS build/import audit passes. Hardware video acceptance remains pending.

## Selectable stream quality (00.002.012)

The user reports native gameplay working. The new build adds profile selection with L1 before launch, coordinated CloudMatch/SDP/NVST requests up to experimental 1080p60 at 75 Mb/s, full-screen 1080p SDR conversion, explicit Opus stereo negotiation and bounded audio redundancy/PLC recovery. Software decoding and CPU tiling remain in use. Host regressions and native packaging/import checks do not validate the new targets on hardware. See [stream quality](STREAM_QUALITY.md) for targets and remaining 4K120 HDR work.

00.002.012 was installed on the user’s console on 2026-10-04. The previous 00.002.011 files matched their earlier installation hashes and were saved locally; the complete old title tree remains in a private console backup. All six staged and activated files matched the package SHA-256 hashes in confirmed raw SELF mode. No OpenNOW sandbox was present before replacement. This verifies installation integrity, not live performance of the new profiles.

## Periodic pixelation reported at 1080p60 (00.002.013)

The user reports smooth fullscreen video/audio in 00.002.012, but brief recurring pixelation at 1080p60 / 75 Mb/s. The retrieved bounded media log shows approximately 60 decoded frames per second, zero decoder errors, zero SRTP rejection and zero audio underruns during its captured interval. Actual decoded dimensions, assembled-AU losses, queue overflow and reference concealment were not logged, so those counters do not establish the cause.

00.002.013 fixes a reference-recovery gap: an AU discarded by the RTP assembler now invalidates queued/in-flight output and waits for an IDR. The decoder flushes its old references on its own thread before the recovered epoch. Queue overflow and decoder corruption/concealment flags use the same recovery path. The last presented picture remains visible during recovery. Keyframe requests are rate-limited to 250 ms. Host tests cover rejecting predictive frames after reference loss and suppressing output from stale epochs.

Additional bounded diagnostics record actual dimensions/changes, received access-unit bytes, IDRs, assembler loss, sequence gaps, late packets, skipped packets, NACKs, queue losses, corrupt frames and recovery resets. Dynamic streaming settings, 1080p60 target, 75 Mb/s ceiling and audio behavior are unchanged. Native build/import checks pass; live acceptance of the pixelation fix is pending.

00.002.013 was installed after the user closed OpenNOW and the title sandbox was confirmed absent. All six staged and activated files were hash-verified in raw SELF mode. The complete 00.002.012 title tree and local copies were retained. The user reports that periodic pixelation persists. The subsequently retrieved session requested 1080p30/25 Mb/s (the startup default), maintained 1920 × 1080 decode dimensions and recorded no AU loss, sequence gaps, corrupt frames, decoder errors or recovery resets. IDR frequency later approached two per second; this only correlates with the symptom and does not locate its cause.

## Experimental hardware decoder foundation

The user wants to target 4K120 HDR directly and identifies the display as probably a LG OLED CX 55-inch. A native Videodec2 backend now implements H.264/HEVC/Main10 mode configuration, allocation, decode/flush and leased GPU-visible surfaces. Host fault-injection tests pass with ASan/UBSan; the backend cross-compiles for PS5, and the existing app still builds with its import audit passing. The live client does not select the backend. No new console installation was made. GPU presentation, HEVC GFN/RTP negotiation, HDR scanout and actual sustained hardware qualification remain required. See [native hardware video](NATIVE_HARDWARE_VIDEO.md) for precise status and pinned references.


## 00.002.014 — native hardware video and GPU presentation

Videodec2 hardware decode, zero-copy GPU imports, 2160p120 VideoOut and HDR10/PQ scanout are integrated with HEVC session/SDP/NVST negotiation and bounded RTP assembly. Startup qualifications gate exposed profiles. Software profiles remain available. Host transport/metadata/lifetime tests and ten VPS tooling regressions pass; native GPU and fallback builds/import audits are recorded separately.

Installed on 2026-10-04, with all 29 files SHA-256 verified in confirmed raw SELF mode and complete .013 rollback trees retained. Console startup subsequently decoded/imported/presented the original 4K120 Main10 fixture without errors. Actual VideoOut reported 3840 × 2160, 119.88 Hz (code 13), HDR active. 4K120/1080p60 H.264 fixtures also passed. This establishes startup hardware capability and output mode, not GFN’s actual game codec, sustained frame rate, visual quality or removal of the periodic pixelation. User game validation remains pending.


### GFN acceptance and .015 correction

The user’s .014 GFN session receives H.265 (payload 107) and continuous stereo audio. RTP diagnostics show no AU loss, but video is rejected with -1001 before decoding. The startup test-picture pipeline still passes; the live stream’s SPS metadata/dimensions require inspection. .015 adapts the decoder to supported server-provided HDR dimensions and records a single bounded SPS codec-configuration file (no game VCL pictures/audio), plus parsed color/dimension counters. Repeated rejection messages no longer exhaust the diagnostics budget.


## 00.002.016 — Main10 SDR and dynamic resolution

The installed .015 session reproduced DECODE 0 / ERR -1001 with normal audio. Its bounded SPS capture established 10-bit profile 2, BT.709 primaries/transfer/matrix and full range: initially 3840 × 2160, then 2880 × 1620. Those signals describe SDR, so the HDR-only metadata gate rejected the stream before the native decoder was called. No AU losses, sequence gaps or SRTP rejection were recorded in the measured interval; the exact trigger for the server’s downscale is unknown. The port currently requests dynamic-resolution adaptation.

Version .016 implements full/limited-range 10-bit BT.709 rendering, carries actual color/dimensions on each picture lease, and retains the UHD decoder/DPB allocation across supported resolution changes. Returned surfaces can retain the allocation’s larger pitch/rows, but must fit the exact owned slot and allocation envelope. HDR monitor negotiation now includes the upstream desktop client’s preferred content-luminance hints. Presentation diagnostics report actual stream HDR rather than the requested target. ASan/UBSan regressions cover both compact and retained UHD surfaces, foreign pointers, bounds, format mismatches and full-range Main10 SDR negotiation. Installed on 2026-10-04 after the title sandbox disappeared, with all 29 activated files hash-verified in confirmed raw SELF mode. The complete .015 title tree and local copies were retained. Live game presentation remains pending.


### .016 live result and .017 DPB correction

Startup qualification still passed .016 native Main10 import/flip and 2160p119.88 output. The live GFN stream then reached Videodec2 but initially failed with 0x811d0302. After server downscaling, counters recorded decoded/presented 2880 × 1620 SDR frames with no new errors or recovery resets in the later intervals. The user reports that flicker persists. The bounded SPS files establish a five-picture DPB for both observed live resolutions; the locally generated test fixture needs three. The backend reserved only four, incorrectly equating the requested reference count with the SPS buffer requirement.

Version .017 reserves six HEVC DPB pictures and ten leased output slots, retains H.264’s four-picture configuration, parses/logs the SPS DPB requirement and rejects demands above the allocation budget before decode. Bounded native diagnostics record API return, decode/flush stage, acceptance, output flags, geometry, pitch and frame format. Host ASan/UBSan transport, metadata and ownership tests and the native import audit pass. Installed on 2026-10-04 after closure/sandbox verification; all 29 activated files were hash-verified in raw SELF mode and the complete .016 title tree was retained. Live acceptance is pending. The DPB mismatch is established; elimination of the initial API error still requires live proof.


### .017 live acceptance and remaining scintillation

The HEVC DPB correction is live-confirmed: GFN’s five-picture SPS is accepted, the native decoder returns a valid 3840 × 2176 low-aligned Main10 surface, and presentation crops it to 3840 × 2160. Later counters record thousands of decoded/presented 4K frames, API error zero, no RTP/AU loss or audio underruns, and one initial queue recovery. The user initially thought the image was good, then reported persistent once-per-second compression/pixelation pulses and unusable visual quality. This is not a completed image-quality fix. Received color signals remain full-range BT.709 SDR; HDMI HDR mode and a 120 Hz target do not establish actual HDR game content or sustained 120 FPS. Test game: Minecraft Dungeons II.

## 00.002.018 — source-pinned NVST QoS and bounded capture

The port previously sent RTP Receiver Reports and input heartbeat but no NVST QoS reports. OpenNOW’s PR #908 separately documents repeated bitrate reductions as a possible consequence of incorrect QoS accounting. The PS5 implementation now requests the source-pinned unordered, 300 ms partial control stream (SID 6), keeps the existing SID 0 input stream, and sends QoS reports at roughly 18 Hz only after DCEP acknowledgment. Reports contain completed received-AU byte/frame counts, the newest authenticated video RTP timestamp, and successive successfully queued byte samples. Counter wrap, saturation, warm-up and failed queue attempts follow the pinned upstream implementation. Unknown delay metrics remain zero. SCTP now retains complete labels and the locally requested reliability parameters, and tracks per-channel acknowledgment. The legacy WebRTC server’s acceptance of these NVST reports and their effect on compression are still unverified. No new frame-ACK identifiers are guessed.

An optional private `capture-video.request` marker arms a raw-video diagnostic after ten seconds, starting on an IDR and stopping after three seconds or 32 MiB. It captures neither audio nor network credentials and writes only inside the private OpenNOW data directory. Public packages do not include captured game media. This can distinguish encoded quality variation from PS5 presentation defects if QoS does not solve the symptom. Host ASan/UBSan QoS/capture regressions and Linux SCTP channel-state tests pass; native build/import audit passes. Installed on 2026-10-04 after closure/sandbox verification; all 29 activated files were hash-verified in raw SELF mode, with a complete .017 rollback tree retained. The optional private capture is armed for the next stream. Startup hardware qualification passes, but live QoS acceptance and visual quality remain pending.


### .018 live result and .019 late capture

The legacy server acknowledges SID 6; QoS reports are queued continuously. Live 4K Main10 SDR decoding has no API errors, with zero measured RTP/AU loss and two initial queue recoveries. The user still reports severe pixelation, clarifying that it appears after about two minutes. The three-second private source capture was retrieved and decoded with FFmpeg without reported bitstream errors, but covers an early loading/window transition, so it cannot establish the cause of the late defect. Its capture marker was removed; the clip remains outside the repository and public package.

Version .019 consumes a private capture request once, polls for new requests during an active stream, and starts a late capture on the next IDR without restarting the session. It retains the three-second / 32 MiB bound. A small private capture status file reports completion, and a replaced live-video snapshot every two seconds preserves current counters beyond the startup log budget. Host ASan/UBSan tests cover a two-minute late request, no truncation during an active capture, marker consumption, repeat capture and bounds; native build/import audit passes. Image quality is not yet corrected. Installed on 2026-10-04 after closure/sandbox verification; all 29 activated files were hash-verified in raw SELF mode, with the complete .018 rollback tree retained. Late in-game capture remains pending.


### .019 late source capture and .020 quality negotiation

At the user's live report, the received stream had fallen to 2880 × 1620 with decoder error zero, no RTP/AU gaps or losses, one initial queue drop and two total resets. A private capture requested during the active session retrieved 1,048,326 bytes over three seconds (roughly 2.8 Mbps of assembled HEVC video), with 214 independently decoded frames and no FFmpeg bitstream warnings. Its decoded Minecraft Dungeons II menu exhibits the same compression pulse outside the PS5 pipeline. HEVC header tracing finds slice base QP 50 at each key picture, then falling toward 29–30 between key pictures. This establishes periodic compression in the received stream, rather than only a PS5 presentation defect; it does not identify the server's entire bandwidth-control algorithm.

The native quality profile previously allowed a 4 Mbps floor, 25 Mbps initialization at a 100 Mbps ceiling, and dynamic resolution control. Version .020 requests a 75% bitrate floor/initial rate, preserves the ceiling, and disables DRC/DFC/GRC/CPM resolution changes with fixed-resolution protocol attributes cross-checked against OpenNOW-vita's pinned source. Native CloudMatch requests omit the dynamicStreamingMode override, matching the upstream desktop reference's official-client behavior. Software compatibility profiles retain their existing negotiation. The server's enforcement of these requests still requires live validation. Source protocol reference pin: 654a9356a9f01df4a67ea802b8f8c96af7ea2ca3; no Vita source files or runtime are copied into the app. Host ASan/UBSan tests pass, including native/compatibility profile separation and CloudMatch requests. Native build/import audit passes. Installed on 2026-10-04 after closure/sandbox verification; all 29 activated files were hash-verified in raw SELF mode and the complete .019 rollback tree was retained. Live enforcement and visual quality beyond two minutes remain pending.


### .020 quality acceptance and throughput regression

The user confirms that the image no longer degrades, but estimates about 15 FPS. Live counters preserve 3840 × 2160, zero decoder API errors and zero RTP/AU losses, while the two-AU application queue records hundreds of overflow/recovery events (1,046 by about 238 seconds). Later measured presentation intervals fall to roughly five FPS. The high-quality stream exposed a local queue/reset storm; removal of source compression alone did not complete the task.

Version .021 replaces the compressed queue with eight bounded native AU slots (software retains two) and one decoder-owned spare. Dequeue swaps allocation ownership, eliminating native per-frame AVPacket allocation and its compressed-data copy. Producer refill, ring wrap and recovery clearing cannot overwrite the decoder's in-flight AU. Overflow still invalidates references, but an arriving IDR can immediately recover instead of being discarded and requested again. Per-session snapshots now report received AUs, current/peak queue depth, maximum queue residence, and cumulative/max native decode and GPU draw times. These diagnose sustained throughput rather than assuming that a larger queue establishes 120 FPS. The .020 quality negotiation is retained. Host ASan/UBSan regressions pass, including burst admission, held-buffer ownership, wrap, padding, bounds, and loss/IDR recovery. Native build/import audit passes. Installed on 2026-10-04 after closure/sandbox verification; all 29 activated files were hash-verified in raw SELF mode and the complete .020 rollback tree was retained. Sustained-cadence console validation remains pending.


### .021 throughput measurement

The user reports that lag persists. In a measured ten-second HEVC Main10 interval, the port receives 92.32 completed AUs/s, decodes/presents 30.97 pictures/s, and records 44 queue overflow/recovery events despite zero RTP loss or decoder API error. Cumulative native decode timing for that interval averages 20.52 ms per call; GPU import/draw averages 0.45 ms, with maximum queue residence about 101 ms. Eight compressed slots and owned handoff do not solve sustained decoder throughput. This measurement includes native input preparation, API decode/optional flush and blocked-slot waits, so it does not yet isolate the native API alone. The existing qualified 4K120 H.264 native profile has been requested as a live codec comparison, with the same fixed-quality bitrate negotiation; result pending.


### H.264 comparison and .022 requested 90 FPS profiles

The user reports improvement in SDR but persistent lag. A ten-second H.264 interval receives 94.45 AUs/s, decodes/presents 51.57 FPS, averages 15.51 ms in the native decode path and 0.31 ms in GPU draw, and records 32 queue recoveries without RTP loss/API errors. This is faster than the prior HEVC sample but still insufficient for the requested cadence.

The user requests 4K90 SDR/HDR modes and a four-profile comparison while retaining 4K120 HDR as the final objective. Version .022 adds both 90 FPS profiles at the existing 100 Mbps ceiling and 75 Mbps floor. Profiles are appended to preserve existing enum identities, inserted into L1 cycling, carried through CloudMatch/network-test/NVST and allowed by native mode contracts. Startup independently qualifies the new decoder/import/flip profiles at the existing 2160p120 HDMI output; this is not sustained-90-FPS proof. Native timing adds separate cumulative input-copy/cache-publication/API-decode/API-flush counters plus surface-pool blocked attempts. Host ASan/UBSan tests pass, including both 90 FPS contracts, high-frame-rate level requirements, request consistency, bounded profile cycling and native ownership/flush accounting. The expanded 4K H.264 mock exposed an old test-only fixed-2048 pitch assumption, corrected to the native 256-byte stride alignment. Native build/import audit passes. Installed on 2026-10-04 after closure/sandbox verification; all 29 activated files were hash-verified in raw SELF mode and the complete .021 rollback tree was retained. Four-profile live comparison and native-stage bottleneck identification remain pending.


### .022 native-stage observation

Startup on the console qualifies both new 4K90 profiles for decoder/import/flip geometry at the existing 2160p120 output. The first in-game 4K120 HDR-requested interval receives 92.93 AUs/s, decodes/presents 30.28 FPS and records 45 queue overflow/recovery events in 10.007 seconds, without RTP/AU loss or API error. Actual input remains Main10 SDR BT.709. Native input copy averages 0.004 ms, cache publication 0.002 ms and the synchronous Videodec2 API call 21.038 ms; there are no flush calls or blocked surface-pool attempts. GPU import/draw averages 0.46 ms. This isolates native API occupancy as the immediate bottleneck in that interval, not compressed-memory copying, cache publication or output-slot exhaustion. The subsequent snapshot reaches 104 seconds, so this sample is not yet a two-minute endurance result. The four-profile comparison remains in progress.


The 4K120 SDR/H.264 comparison was measured after the stream reached 136 seconds. Over 10.004 seconds it receives 93.96 AUs/s and decodes/presents 60.47 FPS, with 26 queue overflow/recovery events, no RTP/AU loss in that interval and no API error. Native API decode averages 13.974 ms, immediate flush 0.170 ms, input copy 0.003 ms, cache publication 0.002 ms and GPU import/draw 0.31 ms; surface-pool blocked attempts remain zero. Actual dimensions remain 3840 × 2160. H.264 improves sustained throughput in this sample but does not attain the requested 120 FPS.


The 4K90 HDR-requested/HEVC interval was measured after the stream reached 124 seconds. GFN delivers 59.98 AUs/s despite the 90 FPS request; the port decodes/presents 41.18 FPS at 3840 × 2160. Over 10.004 seconds it records 17 queue overflow/recovery events, no RTP/AU loss or API error and no surface-pool blocking. Native API decode averages 21.712 ms, input copy 0.004 ms, cache publication 0.002 ms and GPU import/draw 0.42 ms. Actual color metadata remains SDR. The 90 FPS selection establishes the request, not server enforcement or achieved cadence.

### .023 decoder worker configuration

The pinned ProsperoLight pipeline reference documents adjacent SMT siblings and a title CPU envelope of 0x1fff: the historical 0x3f decoder mask therefore covers three physical cores. Version .023 reads the current title thread's allowed CPU mask through public cpuset_getaffinity (eight-byte mask), bounds it to that documented envelope and selects at most five complete physical-core pairs while leaving at least two logical CPUs for other app work. It preserves the synchronous depth-one pipeline and all codec/quality settings. A refused decoder query/create retries the previous 0x3f configuration; allocation, geometry or busy-ownership failures do not trigger that retry. The successful worker mask is recorded with live timing counters. Host ASan/UBSan tests cover CPU-policy bounds, decoder query/create refusal, fallback and complete resource cleanup. Native build/import audit passes on the VPS, including the public affinity-query import; the signed 29-file package and previous .022 package are preserved locally. Installed on 2026-10-04 after closure/sandbox verification, with all 29 activated files hash-verified in raw SELF mode and the complete .022 rollback tree retained. Console worker-mask acceptance and performance improvement remain unverified. No thread affinity is changed outside the decoder's own configuration. Protocol source/reference pins are unchanged.


The final .022 4K90 SDR/H.264 comparison was measured after 126 seconds. Over 10.006 seconds GFN delivers 59.97 AUs/s and the port decodes/presents 60.27 FPS (the short-window difference reflects queue drainage). There are no queue overflow/recovery events, RTP/AU losses, API errors or surface-pool blocked attempts in that interval. Native decode averages 16.107 ms plus 0.169 ms immediate flush; input copy averages 0.004 ms, cache publication 0.002 ms and GPU import/draw 0.31 ms. Dimensions remain 3840 × 2160. Both 90 FPS selections delivered approximately 60 AUs/s during their measurements; their requested 90 FPS is not yet verified as server output. All four observed streams carry SDR metadata, including the HDR-requested Main10 profiles. The 4K120 HDR objective remains unresolved; .023 has been installed after the user closed OpenNOW; the subsequent 4K120 HDR-requested retest remains pending.


Console .023 startup and the subsequent Main10 stream accept worker mask 0x3ff (ten logical CPUs / five physical cores), retaining pipeline depth one and a 3840 × 2160 input. In a 10.005-second interval after 132 seconds, the port receives 80.76 AUs/s, decodes 80.36 and presents 79.16 pictures/s; there are no queue overflow/recovery events, RTP/AU losses, API errors or surface-pool blocked attempts in that interval. Native decode averages 9.141 ms, input copy 0.002 ms, cache publication 0.001 ms and GPU import/draw 1.75 ms. Actual color metadata remains SDR. Payload is only 8.21 Mbps, versus 32.37 Mbps in the .022 HEVC interval, so these samples do not isolate a causal worker-count speedup under matched content. A second attempted measurement found the snapshot stale or the stream restarted and was rejected. Representative matched-scene 4K120 HDR acceptance remains unresolved.


A subsequent matched-load .023 interval after a stream restart receives 92.77 AUs/s at 32.55 Mbps, decodes/presents 30.69 FPS and records 45 queue overflow/recovery events in 10.003 seconds. Native decode averages 20.811 ms, GPU import/draw 0.47 ms; there are no RTP/AU losses, API errors or surface-pool blocks in this interval. Worker mask 0x3ff remains accepted. This is close to the .022 HEVC result (32.37 Mbps, 30.28 FPS, 21.038 ms native decode); the wider worker configuration provides no useful demonstrated improvement under comparable load. The earlier 79 FPS interval used a lighter source and is not evidence of a matched-load fix.

### .023 source layout and .024 controlled prediction test

A fresh private three-second capture contains 12,075,982 bytes and 279 independently decoded pictures. HEVC header tracing identifies Main10, 3840 × 2160, full-range BT.709 SDR, one slice per picture, no tiles or wavefront parallel processing, and a five-picture SPS DPB. The PPS defaults to four active prediction references; initial P pictures override this with one, two, then three references after each IDR. Fourteen IDRs occur in the capture alongside the measured local queue/reset storm. These are properties of this received sample, not a general decoder throughput limit.

Version .024 requests one reference picture for native profiles through the existing video.maxNumReferenceFrames attribute. Value one is cross-checked against the pinned OpenNOW-vita protocol reference; no Vita source or runtime is copied. Software compatibility keeps four. Resolution, FPS, HDR request, codec, 100 Mbps ceiling, 75% bitrate floor, depth-one decoding and the worker policy remain unchanged. Server enforcement, source quality and any performance improvement require a new comparable live sample. Host ASan/UBSan regressions pass for native/compatibility request separation.

The same build queries Main10 decoder memory requirements for copied depth-two and depth-three configurations once, recording return codes and sizes. It does not create those decoders or enable asynchronous decoding; depth one remains active. Tests verify that refused capability queries do not prevent the normal decoder from opening. A successful query alone would not establish safe input-buffer reuse, successful creation or sustained throughput.

The user's PS5_Vulkan reference was reviewed at revision 4271e2e309d9a7eea3a893f5b94427bb78e586ed. Its [documented platform gaps](https://github.com/mihawk-99/PS5_Vulkan/blob/4271e2e309d9a7eea3a893f5b94427bb78e586ed/docs/CTS_GAPS.md) exclude Vulkan Video: the exported Videodec2 API owns stream references, while RADV's Vulkan Video implementation requires direct video-ring access unavailable through those exports. Its fence/presentation work is useful for later graphics measurement, but it supplies no alternative HEVC decoder for the measured API bottleneck. No Vulkan source or runtime is integrated.

The .024 native build/import audit passes on the VPS. Installed on 2026-10-04 after confirming the OpenNOW sandbox was absent; all 29 activated files were read back and hash-verified in raw SELF mode, with the complete .023 rollback tree retained. A bounded target-only system-dump watcher reported ready before the relaunch request; it is best-effort collection, not an attached debugger. Live source enforcement and sustained performance remain pending.

### .024 enforcement and throughput result

The live SPS now announces two DPB pictures. A new private three-second source capture independently decodes 279 pictures: the PPS defaults to one active reference, with no overrides in its 264 P pictures. It retains 3840 × 2160 Main10 full-range BT.709 SDR, one slice per picture, no tiles or wavefront processing, and 15 IDRs during the recovery storm. GFN enforces the simpler prediction request in this sample.

After 120 seconds, a 10.007-second interval receives 92.23 AUs/s at 32.73 Mbps but decodes/presents 31.08 FPS, with 43 queue overflow/recovery events and no RTP/AU loss, API error or surface-pool blocking. Native decode averages 20.268 ms and GPU import/draw 0.46 ms. This is comparable to .023 and demonstrates no useful speedup under the loaded scene. The reference reduction does not resolve the final 4K120 HDR objective.

Console .024 depth-two/depth-three memory queries both return zero. They request about 53557248/66096768 CPU bytes, 343384064/343501824 GPU bytes, and 126267904/159930624 shared bytes, respectively. These are successful memory queries only; no deeper decoder was created or used. The pinned Kodi source reports that its Main10 deeper creation fails and warns about black output after resets in other profiles; the exact current tuple must be measured independently.

### .025 deeper configuration creation probe

After normal startup picture/import/flip qualification succeeds, a separate closed decoder instance attempts to allocate, create, reset and close Main10 depth-two and depth-three configurations, using the already observed worker mask and native mode. It never submits compressed input or presents candidate output. Active game decoding stays at depth one. A refusal is logged and the allocations are cleaned up; failure to stop an engine preserves its memory. Decode explicitly refuses a deeper instance, preventing accidental use of the single-input-buffer path. Host ASan/UBSan tests pass for query refusal, creation refusal, accepted creation, active-instance rejection, return to depth-one decoding and retained memory after failed cleanup. Real creation and asynchronous throughput remain unverified.

The .025 native build/import audit passes on the VPS. Installed on 2026-10-04 after confirming the OpenNOW sandbox was absent, with all 29 activated files read back and hash-verified in raw SELF mode and the complete .024 rollback tree retained. A bounded target-only system-dump watcher reported ready before the relaunch request; it remains best-effort collection, not an attached debugger. Console creation-probe acceptance remains pending; active game decoding stays at depth one.

Console .025 startup accepts, resets and closes both depth-two and depth-three Main10 configurations at worker mask 0x3ff, with zero return codes and complete cleanup. This is stronger than a memory query, but no compressed input was submitted to these candidates. It does not establish valid asynchronous output or sustained throughput.

### .026 qualified asynchronous Main10 pipeline

The next build gives each pending compressed input its own 8 MiB slot (up to four), retains it until the corresponding FIFO output completes, and associates that output with its submitted timestamp and color/geometry mode. Twelve caller-owned output slots preserve decoder/presenter ownership through GPU completion. A drain must finish before any further input is submitted; idle input uses a short timed wait, then drains without interleaving new decode calls. Decode/drain failures release pending presentation safely and reopen depth one before requesting fresh references. Successful reset/delete precedes input/surface reuse or unmapping.

Startup first qualifies the existing classic path. A controlled Main10 candidate then submits and independently GPU-imports 32 fixture pictures across two reset cycles and two full drain/resume bursts per cycle. Every visible luma/chroma sample fingerprint and FIFO timestamp must match the classic baseline, including the first output after resets; coded padding is excluded. A final flip and successful cleanup are required before selecting depth three, or depth two if three fails. Otherwise live decoding stays at depth one. This validates a controlled fixture contract, not representative 120 FPS or real HDR from GFN. Live status records active pipeline depth and pending inputs, with measurement helpers rejecting fallback/counter changes during an interval.

Host ASan/UBSan tests pass for delayed compressed-buffer consumption, FIFO metadata across geometry/color changes, complete drain before resume, reset of pending work, blocked GPU leases, deeper query/create/decode/drain refusal, fallback to depth one and cleanup failure. Fingerprint regressions distinguish black/chroma corruption, ignore coded padding and reject invalid buffers. Native build and console qualification remain pending.

The .026 native build/import audit passes on the VPS, including the idle timed-wait path. Installed on 2026-10-04 after confirming OpenNOW's sandbox was absent; all 29 activated files were read back and hash-verified in raw SELF mode, with the complete .025 rollback tree retained. The target-only bounded system-dump watcher reported ready before the launch request. Console qualification and sustained performance remain pending.

Console .026 startup passes depth-three qualification: 32 submitted and completed fixture pictures, matching sampled visible Y/UV values and FIFO timestamps across both reset cycles and full drain/resume bursts, a successful GPU flip and successful decoder cleanup. The process therefore selects depth three for qualified UHD Main10 streaming. This does not yet establish performance with the live game's source or actual HDR metadata; the two-minute live measurement remains pending.

After 130 seconds, a 10.008-second loaded 4K120 HDR-requested interval receives, decodes and presents 92.13 pictures/s at 3840 × 2160 and 52.15 Mbps. There are zero queue overflows/resets, RTP/AU losses, API errors or surface-pool blocks in the interval. Pipeline depth three and worker mask 0x3ff remain active, with two pending inputs. Native API occupancy averages 1.573 ms per decode call and GPU import/draw 0.61 ms; this is overlapping submission throughput, not a measured 1.573 ms input-to-decoded-picture latency. The user reports that motion now looks fluid. A later snapshot at 284 seconds still records no losses, queue overflows/resets or API errors. The app keeps pace with this live source, a substantial improvement from the prior roughly 31 FPS loaded intervals. GFN still supplies approximately 92 rather than 120 pictures/s and signals Main10 SDR BT.709. The final real-4K120-HDR objective remains unfulfilled; in-game FPS/VSync/HDR settings have been requested while negotiation is inspected.

The user subsequently disables in-game VSync and raises the game's FPS limit from 120 to 240; no in-game HDR option is visible. A new 10.007-second interval still receives, decodes and draws 92.43 pictures/s at 3840 × 2160 and 51.13 Mbps, with zero queue drops/resets, RTP/AU loss, API errors or pool blocks. Native API occupancy averages 1.202 ms and GPU import/draw 0.55 ms. At 1170.69 seconds of session time, cumulative loss, queue drops, resets and API errors remain zero. The game's new limit therefore does not establish a 120 FPS source in this test. The `presented` counter records successful application GPU draws before the renderer's subsequent buffer swap; these measurements are not independently calibrated physical screen cadence or end-to-end latency. The remaining source cadence/HDR cause is unresolved; a same-game comparison with the official Mac client's stream statistics has been requested.

### .027 persistent NVIDIA login

Public prerelease `0.0.2-alpha` adds an owner-only NVIDIA credential cache at
`/data/opennow/account.bin`, outside the title replaced during updates. The device identity
is restored with the account. Client-token renewal falls back to OAuth refresh when
available, preserves an omitted identity token, and saves rotated credentials before
further network requests. Transient network/server errors preserve disk credentials and
retry. Explicit sign-out removes the cache; app closure and game-session stop retain it.
Native storage binds directly to libkernel and synchronizes the file and parent directory.
Authentication renewal runs between games to avoid blocking media processing.

Host ASan/UBSan tests cover process restart, token rotation and fallback, temporary errors,
malformed responses, corrupt caches, failed replacement, owner-only permissions and
sign-out. Existing port regressions and the VPS GPU build/import audit pass. On 2026-10-04,
after confirming the OpenNOW sandbox was absent, the complete previous .026 title was
preserved locally and in a console rollback directory. All 31 staged and activated .027
files were read back and SHA-256 verified in confirmed raw SELF mode. Fresh sign-in,
relaunch, full console reboot and subsequent update acceptance remain unverified. The
historical .026 streaming measurements do not establish new .027 performance results.

## 0.0.3-alpha catalog, controls and artwork

Native `00.002.030` fixes empty-search HTTP 400 responses, removes the startup
Minecraft filter and adds controller-operated search with pagination. L1+R1 signs
out; Circle in the menu closes the application without deleting the account cache.
The revised close path stops the worker and render loop before requesting system
exit from the main thread; console acceptance of that path is pending.

The GPU build/import audit and synthetic host regressions passed. All 32 console
package files were hash-verified with a complete rollback tree. The user confirmed
the ALPHA launcher icon after reboot and the home-screen background without a
further reboot once its missing registered background references were repaired.
That title-specific repair is private and is not included in the release package.
No new streaming performance or HDR acceptance is claimed.

## 00.002.032 controller icons and search keyboard

Controller hints now use antialiased outline symbols of consistent size, with
labels aligned in shared columns. Search has separate underscore and SPACE keys;
the redundant space legend is removed. Navigation wraps through the additional
space row. Catalog and search previews were visually checked, the keyboard
regression passed with ASan/UBSan, and the VPS GPU build/import audit passed.
All 32 installed files were SHA-256 verified with the previous title retained.
The user confirmed the resulting console UI.

A private, title-specific launcher repair restored one missing background
reference and added persistent database guards for NULL updates and reinsertion
of that OpenNOW entry. Tests on a database copy verified both recovery paths,
preservation of unrelated rows and database integrity. Console read-back verified
the reference and guards; the user confirmed the background works. Acceptance
after a full console reboot remains unverified. The repair and its rollback
backup are console-local and are not part of the application package.


## 00.002.035 keyboard layout and profile labels

The search keyboard now has one space key between hyphen and period, a literal
underscore key, and a separate 123/456/789/0 numeric keypad. Directional navigation
uses the visual key positions. The output label fits the screen margins, and all
streaming profiles explicitly state HDR or SDR. Keyboard regressions passed with
ASan/UBSan; layout previews and the VPS GPU build/import audit passed. All 32 files
of the final .035 installation were SHA-256 verified with rollback copies. User
acceptance of these latest refinements remains pending; no new streaming or HDR
measurements were collected.
