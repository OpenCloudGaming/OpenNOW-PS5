# Android upstream fixes reviewed for .044

The PS5 port is a native C++ implementation, not a build of the Android app. A reference pin records the source reviewed; it does not mean every Android feature has been ported.

This review compares the previous reference, `bb3200644fa2b6c7e8f6a1064a45f36759537d08`, with `android-native` revision `793574439c8f82414a869201fd4ed540b5bde26d`, observed on October 5, 2026. The range contains two commits and 27 changed files:

- [1f426331](https://github.com/OpenCloudGaming/OpenNOW/commit/1f426331): controller-state handling and optional multi-controller support.
- [79357443](https://github.com/OpenCloudGaming/OpenNOW/commit/793574439c8f82414a869201fd4ed540b5bde26d): keyboard-overlay handling, Android presentation/storage changes, tests and version metadata.

## Applicable corrections

**Controller timing.** Upstream's `GamepadStateBurstLimiter` sends a stick's return to center immediately, even when the other stick remains active or earlier movement was rate-limited. The PS5 port previously applied its 16 ms gate before examining any state. `.044` observes dead-zone-adjusted sticks and bypasses the gate for neutral transitions, buttons, triggers, and connection changes. Ordinary analog motion remains rate-limited. Both v2 and v3 packet framing remain unchanged.

**Controller declaration.** Both the old and new Android references advertise `availableSupportedControllers: [2]`, `remoteControllersBitmap: 1`, and gamepad-friendly launch mode `2` by default. The PS5 request had an empty supported-controller array. `.044` supplies upstream's default type `2` without advertising additional controller slots. This closes an older parity gap; it is not a claim that the empty array caused an observed server failure.

**Signaling identity.** Android retains established local/remote peer IDs when a message lacks a valid ID. The PS5 numeric helper instead substituted zero. `.044` requires an integral, in-range JSON number before updating IDs or suppressing a self acknowledgment, and uses Android's initial remote-peer default of `1`. This is an older robustness gap, not a new signaling change in the two commits or a demonstrated server incident.

## Other changes reviewed

| Upstream change | PS5 disposition |
| --- | --- |
| Per-slot controller state, timers, presence bitmap and four-slot session advertisement | New multi-controller feature. The PS5 port still exposes one native controller; it does not advertise four slots without implementing them. |
| Saved keyboard overlay must not override a native-touch session | No corresponding native-touch provisioning in PS5. Its touchpad supplies mouse input, and its keyboard is explicitly opened and session-scoped. |
| Android external-device checks, virtual-search/virtual-remote filtering, live dead-zone preferences and TV right-stick mouse auto-arm | Android device-routing APIs and preferences are not used by the PS5 native pad path. The PS5 right stick remains gamepad input. |
| Recording destination, SAF permissions and URI persistence; cancellation-safe recording startup | Android recording workflow. Not the PS5 port's opt-in diagnostic capture. |
| Immersive system bars, safe insets, status-bar dragging, touch-layout appearance and exit-button styling | Android window/touch UI only. No PS5 transport fix to port. |
| Android version and diagnostics-schema updates | Platform metadata, not PS5 protocol behavior. |

Server heartbeat replies and ordinary/self-peer acknowledgments were already implemented in the PS5 port. The compared commits do not change those Android paths. Regression coverage preserves that behavior; this sync does not alter the PS5 heartbeat cadence or introduce automatic session reconnection. No decoder or codec implementation was added by this upstream range.

## Verification

`tests/stream_lifecycle_test.cpp` exercises the production send path for immediate stick centering, single-stick release, button/trigger transitions, disconnect/reconnect, dead-zone boundaries, and retained ordinary-motion throttling in both protocol versions. It also covers malformed peer IDs, the remote-peer default, and heartbeat/self-ack behavior. `tests/cloud_test.cpp` checks the controller declaration for default, preset and custom launches.

The controller-centering, controller-declaration and malformed-peer-info regressions were each run against the previous implementation and failed before the corresponding fix. Run the complete suite with:

```sh
CC=clang CXX=clang++ bash tools/test-port.sh
```

To reproduce the upstream comparison in an OpenNOW checkout:

```sh
git diff bb3200644fa2b6c7e8f6a1064a45f36759537d08 793574439c8f82414a869201fd4ed540b5bde26d -- android
```
