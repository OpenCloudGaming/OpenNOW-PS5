# Development build notes

These notes describe the Open Cloud Gaming development history. Public release tags and PS5 content versions are separate identifiers. Legacy packages from the previous repository can share a content-version number without sharing the same code. Use each release's source revision and manifest to identify its contents.

## Launch and session recovery

Development build `00.002.036` preserves a stream failure message after cleanup so you can retry from the catalog. If cleanup fails, use Options + touchpad to retry **STOP SESSION** before starting another game. While the app remains open, it retains the session for another cleanup attempt. Explicitly closing the app attempts cleanup but still exits if the network or credentials prevent it; the remote session may remain active.

Development build `00.002.037` removes the PS5-only age dialog and follows [upstream OpenNOW's launch request](https://github.com/OpenCloudGaming/OpenNOW/blob/bee18c118dbc89f42319436dcdb172d5b9e15e0c/native/opennow-core/src/cloudmatch.rs#L1511), which sends the fixed compatibility value `userAge: 25`. This is not your age or a change to your NVIDIA account. The app no longer reads or writes `launch-age.txt`; an existing file is left untouched.

## Signaling and endpoint selection

Development build `00.002.038` aligns signaling with OpenNOW's Android native branch. Relative signaling paths use secure WebSocket port 443, separate from the media port; explicit `wss://` endpoints retain their own port. The client sends Android's browser User-Agent, accepts text and binary signaling messages, preserves public ICE candidates, and sends local candidates with the negotiated BUNDLE MID. Failed upgrades report the HTTP status or failed validation check without exposing session IDs, headers, or response bodies. Certificate and WebSocket challenge validation remain enabled.

Development build `00.002.039` refreshes signaling and media endpoints from each session response, so a provisioning-time control address cannot override a later streaming address. Streaming connections take priority over alternate connections regardless of array order. Failed upgrades include a fixed route category for troubleshooting without showing the endpoint or session credentials.

The same build moves saved login and diagnostics to writable sandbox storage. See [privacy and saved login](../README.md#privacy-and-saved-login) for the path, legacy-file behavior, and sign-out controls.

## Alternative SSH build workflow

The [local Linux Docker build](../README.md#build-and-test) needs no SSH alias. The alternative workflow runs Docker on a separate Linux host. Configure the `vps` SSH alias and remote workspace for your environment. The remote GPU build also requires the prepared public GPU SDK/runtime described in [native hardware video](NATIVE_HARDWARE_VIDEO.md) and the [third-party notices](../THIRD_PARTY_NOTICES.md).

For the software compatibility build:

```sh
bash tools/build-vps.sh
```

For the GPU build, after preparing `.deps/gpu/sdk` on the remote host:

```sh
bash tools/build-gpu-vps.sh
```

Generated packages are written to `dist/`. CI checks host behavior; it does not reproduce or validate PS5 hardware execution.
