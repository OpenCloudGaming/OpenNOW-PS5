<p align="center">
  <img src="docs/branding/opennow-icon-alpha.png" width="128" height="128" alt="OpenNOW PS5 app icon" />
</p>

<h1 align="center">OpenNOW PS5</h1>

<p align="center">
  Native GeForce NOW cloud gaming on PlayStation 5.<br />
  Original PS5 port by <a href="https://github.com/Portablelle">Portablelle</a>.<br />
  Development continues at <a href="https://github.com/OpenCloudGaming">Open Cloud Gaming</a>.
</p>

<p align="center">
  <a href="https://github.com/OpenCloudGaming/OpenNOW-PS5/releases"><strong>Downloads</strong></a> ·
  <a href="#install">Installation</a> ·
  <a href="#build-and-test">Build from source</a> ·
  <a href="https://github.com/OpenCloudGaming/OpenNOW-PS5/issues">Report an issue</a> ·
  <a href="CONTRIBUTING.md">Contribute</a>
</p>

<p align="center">
  <a href="https://github.com/OpenCloudGaming/OpenNOW-PS5/actions/workflows/tooling.yml"><img src="https://github.com/OpenCloudGaming/OpenNOW-PS5/actions/workflows/tooling.yml/badge.svg" alt="Host validation" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="License: GPL-3.0-or-later" /></a>
  <img src="https://img.shields.io/badge/platform-PS5-006FCD" alt="Platform: PS5" />
  <img src="https://img.shields.io/badge/status-experimental_alpha-orange" alt="Status: experimental alpha" />
</p>

OpenNOW PS5 lets you sign in to NVIDIA, browse the GeForce NOW catalog, and stream games with native hardware video decoding, GPU presentation, and DualSense controls. It builds on the public [OpenNOW](https://github.com/OpenCloudGaming/OpenNOW) projects.

**Credit for the original PS5 port and the base this repository builds on goes to [Portablelle](https://github.com/Portablelle).** He started the project in [Portablelle/OpenNOW-PS5](https://github.com/Portablelle/OpenNOW-PS5). This repository continues his work under Open Cloud Gaming.

**This is an experimental, console-tested alpha.** It requires a PS5 that can run native homebrew. It is not a retail PlayStation application and is not affiliated with Sony or NVIDIA. Expect bugs, incomplete features, and limited firmware and loader coverage.

## Downloads

**[Open Cloud Gaming releases](https://github.com/OpenCloudGaming/OpenNOW-PS5/releases)** is the download location for new versions. Development, issues, and pull requests now live in **[OpenCloudGaming/OpenNOW-PS5](https://github.com/OpenCloudGaming/OpenNOW-PS5)**.

The first release in this repository is pending. Until it is published, the **[legacy 0.0.7 prerelease](https://github.com/Portablelle/OpenNOW-PS5/releases/tag/0.0.7)** remains available from the previous repository. Download `OpenNOW-PS5-0.0.7.zip` and `SHA256SUMS` from that release's Assets section. The manifest and source archive are also available there.

Legacy releases come from a separate development history. They do **not** include this repository's newer signaling and sandbox-login fixes, and their controls and setup can differ. Follow the notes attached to the version you download. A matching PS5 content-version number does not mean that two packages contain the same code.

## What works

- NVIDIA device-code sign-in, saved login, and automatic credential renewal.
- Separate account Library and Browse sections, real cover art, search, session allocation, and cancellation.
- Native H.264 and HEVC Main10 video decoding, GPU presentation, and Opus stereo audio.
- DualSense gamepad input, touchpad mouse controls, and an in-game keyboard.
- Streaming presets and custom resolution, FPS, bitrate, codec, and hardware/software decoding settings.
- Bounded on-disk artwork caching that survives app restarts.
- Qualified asynchronous UHD Main10 decoding with owned input buffers and GPU surface leases.

One recorded console test received and presented a 3840 × 2160 Main10 SDR stream at approximately **92 FPS and 51–52 Mb/s** for more than 19 minutes, with no recorded RTP/AU loss, queue overflow, decoder reset, or API error. See [stream quality and measurements](docs/STREAM_QUALITY.md) for the test conditions.

### Known limits

- The interface and controller navigation are still under development.
- Sustained **120 FPS gameplay and real HDR source content are not validated**. A selected profile or HDMI output mode is not proof of the stream's actual format or frame rate.
- The server can supply a different frame rate, bitrate, or dynamic range from the requested profile. Some earlier 90 FPS requests received 60 FPS.
- The deeper decode pipeline applies to qualified UHD Main10. H.264 still uses depth one.
- Audio on this branch is stereo. Broader game, display, firmware, and loader compatibility needs testing.
- Software decoding supports H.264 SDR only. Hardware H.264 and HEVC Main10 modes are offered only after startup qualification. AV1 is not implemented; the current [PS5_Vulkan video limitations](https://github.com/mihawk-99/PS5_Vulkan/blob/f3cbf875f89978deae9f855b9be2610cec2818ae/docs/CTS_GAPS.md) do not provide an alternative decoder.

## Install

You need a compatible native-homebrew PS5 environment, a GeForce NOW account with access to the games and streaming modes you want, and a suitable display. This is a **directory package**, not a retail PKG installer.

1. Download the application ZIP and `SHA256SUMS` from the [release page](https://github.com/OpenCloudGaming/OpenNOW-PS5/releases). If no release is available there yet, use the legacy link above and read its version-specific instructions.
2. Run `sha256sum <downloaded-file.zip>` on Linux or `shasum -a 256 <downloaded-file.zip>` on macOS. Compare the result with the ZIP's entry in `SHA256SUMS`.
3. Close OpenNOW before replacing an installation. Keep a backup outside directories scanned by the loader, so a duplicate title ID cannot become the registered installation.
4. Extract the archive and install the complete `PPSA99082` folder through a compatible native homebrew directory loader, such as ShadowMountPlus. Follow the loader's registration procedure and make sure another app does not use that title ID.
5. Open the app and use the NVIDIA authorization page and code shown on screen to sign in. On current development builds, check that the app reports the NVIDIA account is saved before closing it.

Current development builds need no `launch-age.txt` setup. If the home-screen icon or background stays stale after an update, use the loader's supported refresh or re-registration procedure.

## Controls

These mappings describe the current development build, not every legacy release. During gameplay, normal gamepad input goes to the streamed game.

| Control | Menu action |
| --- | --- |
| Cross | Sign in, open game details, or activate the selected control |
| D-pad | Move through games or Settings controls |
| L1 / R1 | Switch top-level Library, Browse, and Settings sections |
| Square | Refresh Library/Browse; Revert a stream-settings draft |
| Triangle | Open catalog search |
| Circle | Back or Cancel inside a screen/dialog; close the app at the top level |
| Options | Save a valid stream-settings draft; submit catalog search |
| Options + touchpad | Stop streaming, cancel queueing, or retry session cleanup |

Move past the edge of a game page to load the next or previous page. In game details, choose a store and a preset or your saved default, then press Cross to play. That one-off choice does not overwrite the saved default. Sign-out is in **Settings → Account**, with confirmation; L1 + R1 no longer signs out.

In catalog search, use the D-pad to select a character and Cross to type it. Square deletes a character, Triangle clears the text, Options submits, and Circle cancels.

During streaming, the touchpad moves the mouse. A physical click with one finger is left-click; with two fingers it is right-click. **Options + Triangle** opens or closes the in-game keyboard, and **Options + Square** sends gamepad Back/View. In the keyboard, Cross presses a key, Square is Backspace, Triangle is Space, L1 toggles Shift, and Circle returns to the game. The keyboard temporarily replaces the picture while decoding and audio continue; typed text is not echoed or logged. Options can reach the game before the second button of a shortcut is pressed.

### Custom stream settings and artwork cache

Development build `00.002.043` adds editable stream settings. In **Settings → Stream**, left/right changes a value; Cross opens a digit editor for resolution, FPS, or bitrate. L1/R1 choose common values inside those editors. Options saves, Square reverts, and unsaved changes remain when leaving the pane. Saved settings apply to the next session, not an already-running game.

The application allows even dimensions from 320×180 up to 3840×2160 and integer frame rates from 30–120 with qualified hardware decoding. Software H.264 is limited to 1920×1080 and 30–60 FPS. The bitrate limit is 4–100 Mb/s. Hardware availability and the current output can narrow those ranges; invalid or unqualified combinations cannot be saved. These are request limits, not guarantees of server acceptance or sustained performance. Video keeps its aspect ratio instead of stretching non-16:9 resolutions.

Hardware H.264 requires fixed-resolution streaming. When switching from an adaptive configuration, the app asks before changing that policy. HEVC Main10 SDR and HDR have separate runtime qualification paths. Existing preset-only settings files are migrated without changing the saved NVIDIA login.

**Settings → Cover art & cache** controls persistent artwork storage under `/download0/opennow/artwork`. The cache has a 64 MiB budget and at most 128 images, with oldest-access eviction. Larger images can remain memory-only. Turning saving off retains existing disk content; Clear removes only cache files, even while saving is off. It does not remove your login, stream settings, or NVIDIA Library entries.

## Privacy and saved login

Starting with development build `00.002.039`, the app saves NVIDIA credentials and its device identity in **`/download0/opennow/account.bin`**, inside the title's writable sandbox. Earlier builds attempted to use `/data/opennow`. If an existing legacy account file is readable, the app continues using it without copying or deleting it. Otherwise, sign in once to create the sandbox account file.

The account file has owner-only permissions but is **not encrypted**. Keep it private. Ordinary app closure and replacement of application files retain the saved login. Removing the title or clearing its download data can erase it. Full console-reboot persistence still needs live validation.

The app restores and renews the login automatically. Temporary network failures retain the credentials and retry. NVIDIA can require sign-in again if credentials expire or are revoked. The UI shows whether the account is saved. **Settings → Account → Sign out** removes the saved login after confirmation; closing the app or stopping a game does not.

Private diagnostics also live under `/download0/opennow`; an explicit local marker enables a short troubleshooting video capture. **Do not upload this directory, account files, or unredacted session logs in an issue.** Public source and release packages exclude credentials, personal configuration, console dumps, and captured gameplay.

## Build and test

Clone the active repository:

```sh
git clone https://github.com/OpenCloudGaming/OpenNOW-PS5.git
cd OpenNOW-PS5
```

Host tests use synthetic fixtures and need no console or NVIDIA account. Install Clang with AddressSanitizer and UndefinedBehaviorSanitizer support, Python 3, and libcurl, EGL, and OpenGL development headers. On Ubuntu, the graphics-header packages are `libegl-dev` and `libgl-dev`. Then run:

```sh
CC=clang CXX=clang++ bash tools/test-port.sh
```

To build the native GPU package on a Linux Docker host:

```sh
bash tools/gpu/build-local.sh
```

The build retrieves and verifies pinned public dependencies and writes `dist/PPSA99082` and `dist/manifest.json`. It does not deploy to a console. No proprietary Sony SDK or firmware module is included. Passing host tests and a native build does not prove live streaming works on a PS5.

For a synthetic catalog preview on Linux or macOS:

```sh
bash tools/preview-port.sh
```

Screen images are written to `build/preview/`, with the Library also at `build/preview.png`. Linux needs Python Pillow; macOS uses `sips`. The preview downloads public artwork and uses fixture account/settings data, not an authenticated session or video stream.

See [contributing](CONTRIBUTING.md) for the development workflow, [native hardware video](docs/NATIVE_HARDWARE_VIDEO.md) for the GPU path, and [development build notes](docs/DEVELOPMENT_BUILDS.md) for launch recovery, signaling fixes, and the alternative SSH build workflow.

## Documentation

| Guide | Contents |
| --- | --- |
| [Stream quality](docs/STREAM_QUALITY.md) | Profiles, measurements, and validation limits |
| [Port status](docs/PORT_STATUS.md) | Implementation history and console results |
| [Development builds](docs/DEVELOPMENT_BUILDS.md) | Behavior changes since the original alpha |
| [Android upstream sync](docs/UPSTREAM_SYNC.md) | Reviewed fixes, ported behavior, and platform-specific exclusions |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Build, loader, and deployment checks |
| [Third-party notices](THIRD_PARTY_NOTICES.md) | Component licenses and source provenance |

Some tooling guides describe optional workflows inherited from the native application boilerplate. For the OpenNOW GPU package, start with the build command above and the notes for your release.

## Credits and license

[Portablelle](https://github.com/Portablelle) created the original PS5 port and established the base for this project. **The original PS5 work is his.** Development continues here under [Open Cloud Gaming](https://github.com/OpenCloudGaming), with the [original repository](https://github.com/Portablelle/OpenNOW-PS5) and its commit history preserved as the project's starting point.

Thanks also to the [OpenNOW](https://github.com/OpenCloudGaming/OpenNOW) and [OpenNOW-Switch](https://github.com/OpenCloudGaming/OpenNOW-Switch) authors and contributors for the authentication, streaming protocol, and transport foundations.

The PS5 platform, decoder, and GPU work also builds on ProsperoLight, public PS5 hardware-video research, Kodi PS5, ps5-opengl, and the native application boilerplate. Their roles, licenses, and source revisions remain in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), [upstream-lock.json](upstream-lock.json), and the source headers.

The combined native application is **[GPL-3.0-or-later](LICENSE)**. Third-party components retain their own licenses and notices. PlayStation, DualSense, NVIDIA, and GeForce NOW are trademarks of their respective owners.
