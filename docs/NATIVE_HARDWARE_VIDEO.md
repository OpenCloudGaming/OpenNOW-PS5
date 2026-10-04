# Experimental native hardware video

> Current public release: **0.0.1-alpha**, development build **00.002.026**. Live 4K Main10 SDR decoding keeps pace with the measured approximately 92 FPS source, with no cumulative loss/reset/API error after 19 minutes 30 seconds. Real source HDR and 120 FPS remain unverified. The qualified Main10 path now uses depth three, four retained input slots and twelve output slots. The architecture notes below retain the earlier development context; see [current stream quality](STREAM_QUALITY.md) and [release notes](releases/0.0.1-alpha.md) for current results.


Target: GFN 3840 × 2160 / 120 FPS, HEVC Main10, 4:2:0, HDR10 (BT.2020 non-constant luminance, PQ, limited range). The native decoder, GPU presenter, HDR scanout and HEVC negotiation are integrated in **00.002.014**, installed on 2026-10-04. Startup on the user’s console successfully decoded/imported/flipped the original 4K120 Main10 test picture. VideoOut reported 3840 × 2160, refresh code 0x0d (119.88 Hz) and HDR output. GFN subsequently sent HEVC Main10 SDR (BT.709, full range), first at 3840 × 2160 and then 2880 × 1620. The strict HDR gate in .014/.015 rejected it before decoding with -1001. Version **00.002.016** accepts this SDR format and resolution changes inside the original 4K allocation. The .016 live session initially failed native decoding with 0x811d0302, then decoded/presented 2880 × 1620 SDR after adaptation; the user still reports flicker. GFN’s SPS asks for five DPB pictures while the decoder reserved four. Version **00.002.017** reserves six for HEVC and adds bounded native output diagnostics. The .017 live session subsequently decoded/presented native 3840 × 2160 SDR without API errors. The user still reports recurring severe pixelation; image quality and sustained frame rate remain unresolved. Version .018 adds source-pinned NVST QoS feedback and an optional bounded private capture to locate the defect. Actual PQ/HDR delivery remains unconfirmed.

## Implemented locally

`src/stream/native/hardware_decoder.*` implements the public Videodec2 call sequence: module load, compute queue, flexible CPU workspace, direct GPU-visible memory, decoder creation, synchronous decode/immediate flush, reset and teardown. Native modes include H.264 High, HEVC Main and Main10 at up to 3840 × 2160 and 120 FPS; even server dimensions inside this envelope are supported, including 2880 × 1620. Configurations remain requests to the console API, not measurements of sustainable performance. Only profiles qualified on that console at startup are exposed to GFN.

The decoder validates returned dimensions, byte pitch, codec, picture count, allocation bounds and exact slot pointers. Main10 uses low-aligned 10-bit components in 16-bit words; treating it as conventional MSB-aligned P010 produces incorrect colors. Coded 2160p output can be 2176 rows, and the chroma plane begins after the coded height. Presentation must crop to 2160 visible rows.

Ten slots cover the six-picture HEVC DPB (four for H.264), pipeline depth one and presentation overlap. GFN’s four-reference setting still produces an SPS asking for a five-picture DPB; counting references alone under-allocated the .014–.016 decoder. A returned picture holds a unique lease. The presenter must release it after the GPU sampling fence completes. Pool exhaustion returns `blocked` without consuming input. Stale or duplicate releases are rejected. Reset and shutdown refuse to free memory while the presenter owns a surface; failed decoder/compute teardown also preserves memory. The instance must outlive every GPU submission and its picture lease.

`hardware_video_contract.hpp` separately requires actual decoder output validation, GPU import, a completed flip, the requested display mode and HDR output before a native profile is qualified. Memory-query success alone cannot enable 4K or HDR. Ten-bit samples alone cannot establish HDR10: stream color signals must agree.

The GPU app compiles, links its active Videodec2/AGC imports, signs, packages and passes its import audit on the VPS. Host tests with ASan/UBSan cover allocation failure cleanup, failed decoder creation, busy shutdown/reset, stale leases, pool exhaustion, foreign/interior pointers, duplicate GPU-owned output and delayed output. These mocks do not establish actual decoding, import linking, display compatibility or frame rate on the console.

## Integrated pipeline and remaining acceptance

The main thread owns EGL/OpenGL and VideoOut for both UI and video. The patched public ps5-opengl 1.0.0 runtime imports caller-owned GPU-visible NV12/R16+RG16 planes through Kodi’s public foreign-memory extensions. A GLSL shader crops coded padding and converts full/limited-range BT.709 SDR at 8 or 10 bits, or limited-range BT.2020/PQ HDR. Each leased picture carries its own color and visible-dimension metadata; the importer honors the returned pitch and coded rows. HDR preserves PQ and packs the resulting RGB into the 10-bit scanout words; framebuffer sRGB and dithering are disabled. GPU completion precedes decoder surface release. Stop/reset wait for in-flight presentation and retain safe buffer ownership.

Title metadata enables high refresh and HDR. `eglSetDisplayModePS5` / `eglSetDisplayRefreshPS5` request 2160p120; actual VideoOut resolution, refresh and dynamic range determine qualification after a completed test-picture flip. A passed test picture establishes that startup pipeline and mode, not sustained 120 FPS.

GFN allocation, SDP and NVST request the same codec, dimensions, frame rate, bitrate and HDR choice. HEVC RTP supports single NALs, aggregation and fragments without DON interleaving, caches VPS/SPS/PPS and uses the existing reorder/loss-recovery path. Unsupported offers fail visibly. Live SPS/VUI selects actual HDR10 only for Main10, BT.2020 non-constant luminance, PQ and limited range. Main10 BT.709 SDR is rendered as SDR even when HDR was requested. Server resolution changes retain the original decoder/DPB allocation and validate returned memory bounds, pitch and rows before importing. Unsupported transfer functions remain rejected. This implementation does not forward dynamic HDR metadata or implement surround sound. CloudMatch HDR monitor requests include the desktop client’s preferred content-luminance hints (1000/0/400), not a measured panel specification.

The bounded queue/assembler accepts access units up to 8 MiB. Since .021, native compressed buffering has eight slots plus one decoder-owned spare, with an allocation handoff instead of a per-AU AVPacket copy; software buffering retains two slots. Stereo Opus/AudioOut is retained. Presentation counters are separate from decoded-frame counters.

Startup on the user’s PS5 qualified 4K120 HDR, 4K120 H.264 and 1080p60 H.264; native API errors and GL import errors were zero. No 4K60 HDR startup test was attempted because the 120 Hz HDR test passed. Its separate profile remains hidden until qualified. GFN HEVC Main10 receipt is confirmed, but actual PQ/HDR game delivery, visual fidelity and throughput still need live validation. The measured game intervals before .016 recorded no assembled-AU losses, sequence gaps or SRTP failures; they do not explain the server’s resolution reduction. These are historical pre-.020 observations; since .020, native profiles request fixed resolution with dynamic resolution controls disabled.

## Rebuilding

Docker builds run on the VPS. `tools/build-gpu-vps.sh` builds the app using the prepared GPU SDK; `tools/gpu/build-runtime.sh SOURCE BUNDLE` reproduces the runtime patch/build from the SHA-256 verified 1.0.0 SDK source snapshots. Unpack `ps5-opengl`, `opengnm-psbc`, `SPIRV-Headers` and Mesa into the runtime’s `third_party` paths, retaining the Mesa archive for verification. The script generates missing PSBC headers, applies the pinned PS5 compiler patch and Kodi additions, builds Mesa/native runtime, materializes thin archives and installs them into ignored `.deps/gpu/sdk`. Link stubs describe native imports only; no firmware module or SDK implementation is copied to the console. Original qualification fixtures and their FFmpeg generation script are included.

Host ASan/UBSan tests cover decoder ownership/rollback, HEVC/H.264 transport, HDR SPS, profile negotiation and recovery; ten tooling tests pass on the VPS. Native import audits and one-picture hardware qualification complement those tests. They do not establish end-to-end game performance.

## Current image defect

The last retrieved 00.002.013 session requested **1080p30 at 25 Mb/s**, despite the earlier 1080p60 report; the profile resets on restart. Decoded dimensions remained 1920 × 1080. Its recorded intervals had no dropped access units, sequence gaps, corrupt frames, decoder errors or recovery resets. IDRs later arrived roughly twice per second, similar to the reported half-second pulse. This is a correlation, not proof that keyframes or the server cause the defect. The logs cannot establish whether the defect is encoded in the stream or introduced during presentation.

Changing to hardware decoding/GPU presentation may remove a defect in the local pipeline. It cannot repair quality variation already present in received pictures. Neither a decoder-only patch nor a higher bitrate request can be described as a confirmed flicker fix.

## Display

The user identifies the display as probably a **LG OLED CX 55-inch**. LG's CX specifications list 4K@120p, 10-bit and HDR support. The exact TV model is still approximate; the actual console VideoOut mode above was read successfully.

## Sources

- [Public PS5 decoder research](https://github.com/blackbearreloaded/ps5-hardware-video-decoding-research)
- [ProsperoLight native streaming implementation](https://github.com/blackbearreloaded/ProsperoLight)
- [Kodi PS5 hardware decoding, zero-copy driver additions and HDR](https://github.com/VivaLaVent/kodi-ps5)
- [Public PS5 OpenGL SDK](https://github.com/blackbearreloaded/ps5-opengl)
- [LG CX 55-inch specifications](https://www.lg.com/uk/tvs/oled-tv/oled55cx6la/)

Exact research revisions and SDK archive hash: `upstream-lock.json`. Research sources stay in ignored `.deps/`; Docker runs only on the VPS.
