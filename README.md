<p align="center">
  <img src="docs/branding/opennow-icon-alpha.png" width="128" height="128" alt="OpenNOW PS5 app icon" />
</p>

<h1 align="center">OpenNOW PS5</h1>

<p align="center">
  Native GeForce NOW cloud gaming on PlayStation 5.<br />
  An open-source homebrew client from <a href="https://github.com/OpenCloudGaming">Open Cloud Gaming</a>.
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

**This is an experimental, console-tested alpha.** It requires a PS5 that can run native homebrew. It is not a retail PlayStation application and is not affiliated with Sony or NVIDIA. Expect bugs, incomplete features, and limited firmware and loader coverage.

## Downloads

**[Open Cloud Gaming releases](https://github.com/OpenCloudGaming/OpenNOW-PS5/releases)** is the download location for new versions. Development, issues, and pull requests now live in **[OpenCloudGaming/OpenNOW-PS5](https://github.com/OpenCloudGaming/OpenNOW-PS5)**.

The first release in this repository is pending. Until it is published, the **[legacy 0.0.7 prerelease](https://github.com/Portablelle/OpenNOW-PS5/releases/tag/0.0.7)** remains available from the previous repository. Download `OpenNOW-PS5-0.0.7.zip` and `SHA256SUMS` from that release's Assets section. The manifest and source archive are also available there.

Legacy releases come from a separate development history. They do **not** include this repository's newer signaling and sandbox-login fixes, and their controls and setup can differ. Follow the notes attached to the version you download. A matching PS5 content-version number does not mean that two packages contain the same code.

## What works

- NVIDIA device-code sign-in, saved login, and automatic credential renewal.
- Game and store catalog browsing, search, session allocation, and cancellation.
- Native H.264 and HEVC Main10 video decoding, GPU presentation, and Opus stereo audio.
- DualSense gamepad input and selectable streaming profiles, including 4K targets, SDR, and HDR output paths.
- Qualified asynchronous UHD Main10 decoding with owned input buffers and GPU surface leases.

One recorded console test received and presented a 3840 × 2160 Main10 SDR stream at approximately **92 FPS and 51–52 Mb/s** for more than 19 minutes, with no recorded RTP/AU loss, queue overflow, decoder reset, or API error. See [stream quality and measurements](docs/STREAM_QUALITY.md) for the test conditions.

### Known limits

- The interface and controller navigation are still under development.
- Sustained **120 FPS gameplay and real HDR source content are not validated**. A selected profile or HDMI output mode is not proof of the stream's actual format or frame rate.
- The server can supply a different frame rate, bitrate, or dynamic range from the requested profile. Some earlier 90 FPS requests received 60 FPS.
- The deeper decode pipeline applies to qualified UHD Main10. H.264 still uses depth one.
- Audio on this branch is stereo. Broader game, display, firmware, and loader compatibility needs testing.

## Install

You need a compatible native-homebrew PS5 environment, a GeForce NOW account with access to the games and streaming modes you want, and a suitable display. This is a **directory package**, not a retail PKG installer.

1. Download the application ZIP and `SHA256SUMS` from the [release page](https://github.com/OpenCloudGaming/OpenNOW-PS5/releases). If no release is available there yet, use the legacy link above and read its version-specific instructions.
2. Run `sha256sum <downloaded-file.zip>` on Linux or `shasum -a 256 <downloaded-file.zip>` on macOS. Compare the result with the ZIP's entry in `SHA256SUMS`.
3. Close OpenNOW before replacing an installation. Keep a backup of the existing title folder.
4. Extract the archive and install the complete `PPSA99082` folder through a compatible native homebrew directory loader, such as ShadowMountPlus. Follow the loader's registration procedure and make sure another app does not use that title ID.
5. Open the app and use the NVIDIA authorization page and code shown on screen to sign in. On current development builds, check for `ACCOUNT SAVED` before closing the app.

Current development builds need no `launch-age.txt` setup. If the home-screen icon or background stays stale after an update, use the loader's supported refresh or re-registration procedure.

## Controls

These mappings describe the current development build, not every legacy release. During gameplay, normal gamepad input goes to the streamed game.

| Control | Menu action |
| --- | --- |
| Cross | Sign in or launch the selected game/store |
| D-pad | Select a game/store |
| L1 | Cycle stream profiles before launch |
| Square | Load the catalog |
| Triangle | Open catalog search |
| R1 | Load the next catalog page |
| Circle | Close the app while keeping the saved login; cancel search text entry |
| L1 + R1 | Sign out and remove the saved login |
| Options + touchpad | Stop streaming, cancel queueing, or retry session cleanup |

In catalog search, use the D-pad to select a character and Cross to type it. Square deletes a character, Triangle clears the text, Options submits, and Circle cancels. R1 advances through search results. Square in the catalog returns to the full catalog.

## Privacy and saved login

Starting with development build `00.002.039`, the app saves NVIDIA credentials and its device identity in **`/download0/opennow/account.bin`**, inside the title's writable sandbox. Earlier builds attempted to use `/data/opennow`. If an existing legacy account file is readable, the app continues using it without copying or deleting it. Otherwise, sign in once to create the sandbox account file.

The account file has owner-only permissions but is **not encrypted**. Keep it private. Ordinary app closure and replacement of application files retain the saved login. Removing the title or clearing its download data can erase it. Full console-reboot persistence still needs live validation.

The app restores and renews the login automatically. Temporary network failures retain the credentials and retry. NVIDIA can require sign-in again if credentials expire or are revoked. The UI reports `ACCOUNT SAVED` or `ACCOUNT NOT SAVED`. L1 + R1 signs out and removes the saved login; closing the app or stopping a game does not.

Private diagnostics also live under `/download0/opennow`; an explicit local marker enables a short troubleshooting video capture. **Do not upload this directory, account files, or unredacted session logs in an issue.** Public source and release packages exclude credentials, personal configuration, console dumps, and captured gameplay.

## Build and test

Clone the active repository:

```sh
git clone https://github.com/OpenCloudGaming/OpenNOW-PS5.git
cd OpenNOW-PS5
```

Host tests use synthetic fixtures and need no console or NVIDIA account. Install Clang with AddressSanitizer and UndefinedBehaviorSanitizer support, Python 3, and libcurl development headers, then run:

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
OPENNOW_PREVIEW_CATALOG=1 bash tools/preview-port.sh
```

The image is written to `build/preview.png`. Linux needs libcurl development headers and Python Pillow; macOS uses `sips`. The preview uses fixture data, not an authenticated session or video stream.

See [contributing](CONTRIBUTING.md) for the development workflow, [native hardware video](docs/NATIVE_HARDWARE_VIDEO.md) for the GPU path, and [development build notes](docs/DEVELOPMENT_BUILDS.md) for launch recovery, signaling fixes, and the alternative SSH build workflow.

## Documentation

| Guide | Contents |
| --- | --- |
| [Stream quality](docs/STREAM_QUALITY.md) | Profiles, measurements, and validation limits |
| [Port status](docs/PORT_STATUS.md) | Implementation history and console results |
| [Development builds](docs/DEVELOPMENT_BUILDS.md) | Behavior changes since the original alpha |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Build, loader, and deployment checks |
| [Third-party notices](THIRD_PARTY_NOTICES.md) | Component licenses and source provenance |

Some tooling guides describe optional workflows inherited from the native application boilerplate. For the OpenNOW GPU package, start with the build command above and the notes for your release.

## Credits and license

Development continues here under [Open Cloud Gaming](https://github.com/OpenCloudGaming), following the [original PS5 repository](https://github.com/Portablelle/OpenNOW-PS5). Thanks to the [OpenNOW](https://github.com/OpenCloudGaming/OpenNOW) and [OpenNOW-Switch](https://github.com/OpenCloudGaming/OpenNOW-Switch) authors and contributors for the authentication, streaming protocol, and transport foundations.

The PS5 platform, decoder, and GPU work also builds on ProsperoLight, public PS5 hardware-video research, Kodi PS5, ps5-opengl, and the native application boilerplate. Their roles, licenses, and source revisions remain in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), [upstream-lock.json](upstream-lock.json), and the source headers.

The combined native application is **[GPL-3.0-or-later](LICENSE)**. Third-party components retain their own licenses and notices. PlayStation, DualSense, NVIDIA, and GeForce NOW are trademarks of their respective owners.
