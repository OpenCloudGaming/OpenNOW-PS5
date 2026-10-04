# Contributing

Use English for documentation, issue reports and UI text. Keep changes focused and distinguish host tests, native build checks and actual console results.

- Run `bash tools/test-port.sh` for streaming, protocol, decoder ownership and synthetic fixture regressions. Add meaningful regressions for changed behavior.
- Run Docker builds on the configured remote Linux build host. Do not run this project's Docker builds on the Mac. GPU builds require the prepared public SDK/runtime described in `docs/NATIVE_HARDWARE_VIDEO.md`.
- Preserve upstream licenses, SPDX headers, source pins and build metadata. New original code is GPL-3.0-or-later unless its directory requires another compatible license.
- Do not commit `.env`, `.local/`, `.deps/`, `build/`, `dist/`, generated runtime binaries, SDK binaries, proprietary modules, game files, console dumps, keys, credentials or captured media.
- Report actual stream dimensions, codec, color metadata and measured cadence. A requested profile or HDMI mode is not proof of source HDR or sustained FPS.
- Keep installation changes separate from builds, retain rollback copies and verify transferred files. Close the app before replacing active files.

Public release tags use semantic versions such as `0.0.1-alpha`. The PS5 `sce_sys/param.json` content version remains in the platform's `NN.NNN.NNN` format; release notes and the release manifest record the mapping. A public release may package the already tested development build without changing its executable.

The native tooling inherited from the boilerplate has additional deterministic runtime and format checks under `tests/` and `tooling/native/`. Loader-visible changes need console validation before claiming hardware compatibility.
