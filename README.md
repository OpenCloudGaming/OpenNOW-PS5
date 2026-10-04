<p align="center">
  <img src="docs/branding/opennow-icon.png" width="144" height="144" alt="OpenNOW PS5 app icon" />
</p>

# OpenNOW PS5

![OpenNOW PS5 — native cloud gaming, 0.0.1-alpha functional prototype](docs/branding/opennow-social-preview.png)

*Social preview artwork for the independent native PS5 prototype.*

An experimental native PS5 homebrew client for GeForce NOW, based on the public [OpenNOW](https://github.com/OpenCloudGaming/OpenNOW) projects. Native hardware video decoding, GPU presentation, stereo audio and DualSense input are integrated.

**This is a working, console-tested prototype, not a finished application.** You can sign in, launch a game and play. The interface is still rough and primarily exists to test that authentication, streaming, audio, video and controls work together. Expect an incomplete user experience, limited navigation and functionality that still needs further testing.

**First public release: [0.0.1-alpha](https://github.com/Portablelle/opennow-ps5/releases/tag/0.0.1-alpha)** — console-tested development build `00.002.026`, title ID `PPSA99082`. This project is unofficial and unaffiliated with Sony or NVIDIA.

## What works

- NVIDIA device authorization, game/store catalog, session allocation and cancellation.
- Native H.264 and HEVC Main10 decoding, including 10-bit SDR and HDR10 presentation paths.
- Qualified asynchronous Main10 decoding with owned input buffers and safe GPU surface leases.
- Selectable 4K120, 4K90 and 4K60 HDR targets, 4K120/90 SDR targets, and lower-resolution compatibility profiles, with up to 100 Mb/s requested bitrate.
- Full-screen GPU presentation, Opus stereo audio and DualSense controls.

In the latest console test, a 3840 × 2160 Main10 SDR stream was received, decoded and drawn at approximately **92 FPS and 51–52 Mb/s**. No RTP/AU loss, queue overflow, decoder reset or API error was recorded after more than 19 minutes. The tester reported fluid motion and the earlier recurring compression degradation was resolved.

## Prototype limitations

- **The UI is a test interface.** It is not polished, feature-complete or representative of the final experience. Catalog navigation and the Minecraft search shortcut are intentionally basic.
- **Real 120 FPS and HDR game streaming are not yet validated.** The tested game supplied SDR despite an HDR request. Startup fixture tests passed 4K, 119.88 Hz output and HDR scanout; these checks do not establish sustained game FPS or HDR source content.
- The 90 FPS profiles are requests; earlier tests received 60 FPS. Server behavior can differ from the selected target.
- The deeper pipeline currently applies to qualified UHD Main10. H.264 remains at depth one and does not inherit its measured throughput improvement.
- Saved login and token refresh are not implemented. Audio is stereo only. Broad firmware/loader compatibility and more games still need testing.

Performance may differ by game, server, network, display and profile. Requested settings are targets, not guaranteed results. See [stream quality](docs/STREAM_QUALITY.md) for the measurements.

## Install the alpha

1. Download `OpenNOW-PS5-0.0.1-alpha.zip` and `SHA256SUMS` from [Releases](https://github.com/Portablelle/opennow-ps5/releases).
2. Verify the ZIP against its entry in `SHA256SUMS` using `shasum -a 256` or `sha256sum`. If you download every listed asset, you can use `shasum -a 256 -c SHA256SUMS`.
3. Extract the archive. Install the included `PPSA99082` folder through a compatible native homebrew directory loader, such as ShadowMountPlus. Follow your loader's registration procedure and check that this title ID is unused.
4. Before replacing an existing installation, close OpenNOW completely and retain a backup of its title folder.
5. Create `/data/opennow/launch-age.txt` containing your age as an integer from 0 to 120. This personal configuration is required for session requests and is excluded from the release.
6. Open the app and authenticate using NVIDIA's device authorization page shown on screen.

Requires a PS5 environment that can run native homebrew titles, a GeForce NOW account with suitable streaming capabilities, and a compatible display for the requested mode. The package has been tested in one homebrew console environment; compatibility with other firmware/loader combinations is unverified. This is a directory package, not a retail PS5 store application or a PKG installer.

## Controls

| Control | Action |
| --- | --- |
| Cross | Authenticate or launch the selected game/store |
| D-pad | Select a game/store |
| L1 | Cycle available stream profiles before launch |
| Square | Load the catalog |
| Triangle | Search Minecraft |
| R1 | Load the next catalog page |
| Circle | Cancel the session or sign out |
| Options + touchpad | Stop gameplay streaming |

## Privacy

Account tokens remain in memory and are cleared on sign-out/exit. Public source and packages exclude local configuration, account data, session logs, console dumps and captured gameplay. Private bounded diagnostics can be written under `/data/opennow`; an explicit local marker enables a short video capture for troubleshooting. Do not upload that directory when reporting an issue.

## Build and test

Host tests use synthetic fixtures and do not require a console or account:

```sh
bash tools/test-port.sh
```

Native Docker builds in this project's workflow run on a separate Linux build host over SSH. Configure the `vps` SSH alias and remote workspace for your environment. The GPU build additionally requires preparing the pinned public GPU SDK/runtime; see [native hardware video](docs/NATIVE_HARDWARE_VIDEO.md) and [dependency/source provenance](THIRD_PARTY_NOTICES.md).

```sh
# Software compatibility build
bash tools/build-vps.sh
# GPU build, after preparing .deps/gpu/sdk on the build host
bash tools/build-gpu-vps.sh
```

Generated packages are written to `dist/`. CI checks host behavior; it does not reproduce or validate PS5 hardware execution. No proprietary Sony SDK or firmware module is included.

## Development and licensing

### Credits

**Credit goes to [OpenCloudGaming](https://github.com/OpenCloudGaming), the [OpenNOW](https://github.com/OpenCloudGaming/OpenNOW) authors and contributors, and the [OpenNOW-Switch](https://github.com/OpenCloudGaming/OpenNOW-Switch) contributors.** Their open-source work provides the authentication, streaming protocol and transport foundations that made this native PS5 adaptation possible. This repository is an independent PS5 port prototype, not the official OpenNOW application.

The PS5 platform, decoder and GPU work also builds on public projects including ProsperoLight, the PS5 hardware video research, Kodi PS5, ps5-opengl and the native application boilerplate. Their specific roles, licenses and source revisions are retained in the third-party notices and source headers.

See [port history and measured results](docs/PORT_STATUS.md), [release notes](docs/releases/0.0.1-alpha.md) and [contributing](CONTRIBUTING.md). Other boilerplate documentation covers optional tooling and may describe workflows outside the GPU release path.

The combined native application is **GPL-3.0-or-later**. Third-party components retain their notices in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), `licenses/` and source headers. Exact upstream revisions and hashes are recorded in [upstream-lock.json](upstream-lock.json). Public dependency retrieval/build scripts and local GPU modifications are included; no captured game content is distributed.
