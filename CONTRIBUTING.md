# Contributing

Development now lives in [OpenCloudGaming/OpenNOW-PS5](https://github.com/OpenCloudGaming/OpenNOW-PS5). Open issues and pull requests here rather than in the previous repository. Existing contributors can keep their checkout and point `origin` at the new location:

```sh
git remote set-url origin https://github.com/OpenCloudGaming/OpenNOW-PS5.git
git fetch origin
```

If `origin` is your personal fork, keep it and use the Open Cloud Gaming URL for your `upstream` remote instead. Fetch and compare your branch with `origin/main` or `upstream/main` before continuing work; the old repository and this one have different development histories.

Use English for documentation, issue reports and UI text. Keep changes focused and distinguish host tests, native build checks and actual console results.

- Run `bash tools/test-port.sh` for streaming, protocol, decoder ownership and synthetic fixture regressions. Add meaningful regressions for changed behavior.
- Run Docker builds on Linux, either locally with `bash tools/gpu/build-local.sh` or on a configured remote build host. Do not run this project's Docker builds on the Mac. See [build and test](README.md#build-and-test) for the local GPU workflow and [development build notes](docs/DEVELOPMENT_BUILDS.md#alternative-ssh-build-workflow) for the remote workflow.
- Preserve upstream licenses, SPDX headers, source pins and build metadata. New original code is GPL-3.0-or-later unless its directory requires another compatible license.
- Do not commit `.env`, `.local/`, `.deps/`, `build/`, `dist/`, generated runtime binaries, SDK binaries, proprietary modules, game files, console dumps, keys, credentials or captured media.
- Report actual stream dimensions, codec, color metadata and measured cadence. A requested profile or HDMI mode is not proof of source HDR or sustained FPS.
- Keep installation changes separate from builds, retain rollback copies and verify transferred files. Close the app before replacing active files.

Publish new versions under [Open Cloud Gaming releases](https://github.com/OpenCloudGaming/OpenNOW-PS5/releases). Public release tags use semantic versions such as `0.0.1-alpha`. The PS5 `sce_sys/param.json` content version remains in the platform's `NN.NNN.NNN` format; release notes and the release manifest record the mapping and exact source revision. A public release may package the already tested development build without changing its executable. Do not infer package identity from a legacy content-version number alone.

Include the complete title-folder ZIP, a file manifest, `SHA256SUMS`, and corresponding source with licenses and pinned dependency information. State which checks passed, which behaviors were verified on a console, and which remain unverified. Exclude account data, private diagnostics, captures, and local configuration. Mark experimental builds as prereleases.

The native tooling inherited from the boilerplate has additional deterministic runtime and format checks under `tests/` and `tooling/native/`. Loader-visible changes need console validation before claiming hardware compatibility.
