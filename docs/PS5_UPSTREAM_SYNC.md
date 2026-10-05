# Original PS5 upstream functionality for .045

The upstream for this comparison is [Portablelle/OpenNOW-PS5](https://github.com/Portablelle/OpenNOW-PS5), not the Android or desktop OpenNOW branches. The common ancestor is `b8f9e66a30db48691b148b277b4a51aab0e003c9`. Two upstream commits follow it:

- [`123eb46`](https://github.com/Portablelle/OpenNOW-PS5/commit/123eb46d42f8da96f1cf6665eaa8257cb4563eb2) adds launcher keyboard and mouse controls.
- [`9304039`](https://github.com/Portablelle/OpenNOW-PS5/commit/93040397d39dc81f62546aa03289af87afc8d9a0) adds multichannel audio.

The `Portablelle-OpenNOW-PS5` entry in `upstream-lock.json` records this comparison. It is not a claim that every upstream file was copied. The separate [Android audit](UPSTREAM_SYNC.md) records the earlier .044 work and is not this sync's scope.

## Local adaptations

The launcher mode uses Options + R3 instead of upstream's touchpad toggle. Physical touchpad clicks remain mouse clicks, and the existing keyboard, Back/View, and stop shortcuts retain priority. Right-stick movement, trigger clicks, directional scrolling, and speed selection are available only in launcher mode. The game receives ordinary right-stick input outside that mode.

The keyboard keeps the local US QWERTY layout and Triangle-as-Space behavior. It does not retain or display typed text. Its controls and the launcher HUD are drawn over video rather than replacing the picture. The hardware path retains rendered pixels while an overlay is open, not decoder surfaces. Static video can be redrawn when the selection changes. HDR overlay conversion uses a 203-nit reference white without changing the video's output format.

Input transmission retains failed keyboard, mouse, and wheel work for retry. Successful modifier and button transitions are tracked separately, so cancellation releases only input accepted by the transport. Key holds and release gaps remain at least 32 ms. Cursor-capture requests use the existing control channel after it opens.

Audio choices live in Settings → Display & audio and are saved with the stream settings. Stereo remains the default, including migration of older settings. Auto requests the widest layout the PS5 AudioOut API accepts. An eight-channel port is not proof of an eight-channel speaker or HDMI configuration. Session requests preserve their launch-time settings, and video presets preserve the selected audio preference.

The audio implementation negotiates the offered Opus or multiopus format and its payload IDs rather than assuming fixed RTP payload numbers. Codec parameters belong to their own audio section. Offers with multiple audio or video sections are rejected, even if some sections are disabled, because this peer and its answer alignment support one section of each type. The receiver bounds its packet and PCM buffers and uses RED, FEC, and PLC recovery.

This import does not replace the Library, Browse, or Settings interface, the sandbox login path, custom video controls, or artwork caching. It does not copy upstream release numbers or restore the legacy `/data/opennow` write path. It adds no USB keyboard or mouse support, additional controllers, AV1 decoder, or Vulkan renderer.

## Verification scope

Host tests exercise synthetic input failures, protocol framing, audio negotiation, real libopus decoding, bounded recovery, settings migration, and GPU presenter calls. They do not prove audible speaker placement, NVIDIA's live audio offers, launcher behavior, or HDR appearance on a console. Those remain hardware acceptance checks. A native build and import audit establish package compatibility, not gameplay performance.

The host suite runs with:

```sh
CC=clang CXX=clang++ bash tools/test-port.sh
```

To reproduce the source comparison after fetching the original PS5 repository as `ps5-upstream`:

```sh
git log --oneline b8f9e66a30db48691b148b277b4a51aab0e003c9..ps5-upstream/main
git diff b8f9e66a30db48691b148b277b4a51aab0e003c9 93040397d39dc81f62546aa03289af87afc8d9a0
```
