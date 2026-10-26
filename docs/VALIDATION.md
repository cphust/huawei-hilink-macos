# Validation record

**English** | [简体中文](VALIDATION.zh-CN.md) · [User guide](../README.md)

This record separates retained hardware evidence, earlier project notes, and checks repeated while preparing the GitHub repository. Dates below use the reference environment's `America/Los_Angeles` time zone (`UTC−07:00` on 2026-10-25).

## Reference configuration

| Item | Value |
| --- | --- |
| Mac | M4 Mac mini |
| macOS | 15.1.1, build `24B2091` |
| Modem / recorded firmware | Huawei E8372h-155 / `21.335.03.02.233` |
| Build | arm64, Apple clang 16.0.0, macOS SDK from Command Line Tools |
| Source baseline | Local import `1c20fdd`, upstream `f4f0aac8d33a152cf60db53cecd555daa1d6dae1` |
| Implementation tested | `791c5ce` |
| Remote access | NoMachine over the existing `en0` path; Wi-Fi and VPN remained enabled during the recorded USB checks |

The GitHub preparation changes documentation only. Source and scripts remain those of `791c5ce`. Firmware/model attribution follows the existing project record; this documentation pass did not interrogate or change modem firmware.

## Retained hardware evidence: 2026-10-25

Raw files remain in the ignored local `artifacts/` directory and are not distributed in this repository. The filenames identify the basis of this summary; a clone does not include those files.

| Evidence | Observation | What it establishes |
| --- | --- | --- |
| `before-switch.json` at 16:53:51 | `12d1:1f01`, `state=cdrom`, no driver/interface/IPv4 | Actual initial device state |
| `manual-switch-2.log`, 16:55:55–16:56:00 | Direct DiskArbitration unmount denied; same-target `diskutil unmountDisk` succeeded; one request sent; states included `absent`, `waiting_address`, then `address_ready` | One successful hardware switch, including the permission fallback |
| `after-switch-2.json` at 16:56:00 | `12d1:14db`, `AppleUserECM`, `en9`, usable IPv4 | Network-mode device, ECM driver, and interface/address observed |
| `stability-10min.jsonl`, 16:59:21–17:09:21 | 11 JSONL rounds; all `address_ready`; direct USB DNS results and HTTPS `HTTP/1.1 200 OK`; peer route recorded as `en0` throughout | Repeated USB-bound connectivity over approximately ten minutes |
| `before-switch.json` vs `final-network.json` at 17:10:35 | Original network-service objects unchanged; original relative order preserved; one `HUAWEI_MOBILE` service added; existing NoMachine connection lines unchanged | Recorded original configuration/session continuity across those snapshots |
| Same before/final snapshots | NoMachine peer route remained `en0`; default route remained `utun4` | USB testing did not imply default application traffic moved to USB |

The stability records explicitly state `isolation_test=pending_wifi_and_vpn_remain_enabled`. They demonstrate interface-bound requests while other networks remain enabled. They do not establish an isolated USB-only configuration, throughput, or long-term reliability.

## Earlier project notes

The previous README at implementation commit `791c5ce` additionally recorded:

- A management-page `GET` returned HTTP 200. The automated script uses `HEAD`, which can return 405 on that endpoint.
- The current-user LaunchAgent was installed and observed idle with exit code 0 and its event subscription active.
- Installation, uninstallation, and reinstallation completed; the USB interface stayed ready during removal and the automatic job was restored.
- Wi-Fi and Shadowrocket remained enabled.

These are earlier reported results, not actions repeated during GitHub preparation. An installed/idle LaunchAgent alone does not verify future insertion, login, or wake events.

## Checks repeated for GitHub preparation: 2026-10-25

| Check | Result | Scope |
| --- | --- | --- |
| `sh test.sh` | Passed | Rebuild, policy executable, invalid-argument exit, shell syntax, plist syntax, Python compilation |
| arm64 build with `-Wall -Wextra -Werror` | Passed | Native compilation against the available macOS SDK |
| `codesign --verify --strict --verbose=2 build/hilink-switch` | Passed | Local ad-hoc signature is valid; no Developer ID/notarization claim |
| `sh status.sh` | Exit 0, `address_ready`, `AppleUserECM` | Live read-only device/interface/address query; no Internet test |
| Retained JSON/JSONL inspection | Confirmed the observations above | Historical evidence review, not a new hardware run |

`tests/test_policy.c` covers state classification (including disappearance and ambiguity), zero/link-local IPv4 rejection, the policy deciding whether a read-only volume may be force-unmounted, and lock contention/reacquisition. Timeout/permission inputs to the force policy are synthetic; they are not end-to-end tests of a blocked USB call or a real disk-unmount failure. The test runner also checks that invalid CLI arguments return 2.

No manual switch, installation/removal, network-service change, Wi-Fi/VPN change, firmware operation, or network probe was performed for documentation preparation.

## Outstanding acceptance tests

| Test | Status | Suggested evidence |
| --- | --- | --- |
| Three physical unplug/replug cycles | Not completed | State/log sequence and USB-bound connectivity for each cycle |
| Logout and login | Not completed | Job loading/event subscription and device state after login |
| Sleep and wake | Not completed | Interface/address recovery and a new connectivity check |
| USB-only isolation with Mac Wi-Fi and VPN disabled | Not completed | Recovery access prepared in advance; actual application routes and USB connectivity checked |
| Extended operation or throughput | Not completed | Define duration/load and record failures, latency, and connection continuity |
| Other Mac/OS/modem/firmware combinations | Not validated | Separate configuration-specific evidence |
| Intel build | Not provided by the build script | A separately adapted and tested build would be required |

Perform tests that could interrupt remote access only when the operator has a working recovery path. The fixed diagnostic guard must match the real deployment; do not assign the example IP addresses to a different system to make the guard pass.

## Reproducing the non-disruptive checks

From the project directory on the reference Mac:

```sh
sh test.sh
codesign --verify --strict --verbose=2 build/hilink-switch
sh status.sh
```

The first two commands do not require the modem to be ready. The third reports actual readiness and can return a documented nonzero state. To perform hardware/network tests, follow the [user guide](../README.md), review its deployment guard, and keep new raw evidence local. Never substitute policy-test results for physical acceptance results.

**English** | [简体中文](VALIDATION.zh-CN.md) · [User guide](../README.md)
