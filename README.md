# Cardputer emulator

Runs real M5Stack Cardputer ADV firmware on a Linux PC, to catch problems before anything goes near a device. It compiles the firmware's own source files, unchanged, against shims for the hardware, on a heap the size and shape of the real one. A firmware bug that runs the device out of RAM runs this out of RAM too, and it says where.

Built for [SupaChat](https://github.com/JayPloss/cardputer-adv-supachat), whose `emulator/` folder is something else: a browser mock of the UI and contract tests of library behaviour. This one runs the firmware.

## Use

    make check                  # builds, then runs every scenario; exit 0 only if all pass
    python3 tools/check.py card-book walkie
    make FIRMWARE=path/to/firmware TAG=name build
    SAN=asan make build         # or SAN=tsan: the same, under a sanitizer
    tools/seed.sh run/mine ~/my-card && build/current/cardputer-emulator --card run/mine/card --state run/mine/state --window

    python3 -I tools/font-specimen.py out/   # every candidate font in SupaChat's firmware/lab/font_samples.h, on real M5GFX: measurements and a contact sheet

`--window` is the interactive mode: the screen at 3x, the PC keyboard as the Cardputer's (arrows are the arrow cluster, Esc the top-left key, Left Ctrl is Fn, F1 the rear button). Paths default to a PlatformIO build of the firmware at `~/Work/cardputer-adv-supachat`; set `FIRMWARE`, `LIBDEPS` and `SECRETS_DIR` to change that. `CARD` names a folder laid out like the microSD. Short test audio is generated, so scenarios that only need a test book don't need one.

## What is real and what is modelled

Real: the firmware's code; M5GFX's drawing; M5Cardputer's keyboard logic; arduino-esp32's String, Print and Stream; ArduinoJson; Helix MP3 decoding; HTTP to a real server, when one is configured.

Modelled, with every figure in `src/device_model.h` and marked MEASURED, ESTIMATE or CALIBRATED:

- **The heap.** Sized from the firmware's ELF (`_heap_start` to the end of main SRAM, plus the 32 KB region at 0x3FCF0000, whose use the real crash dumps show). Blocks carry the device's overhead. Every `malloc`, `new` and task stack comes out of it, so `bad_alloc` and allocation failures happen as on the device.
- **What sits outside the firmware's sight.** The Wi-Fi driver, lwIP, system tasks, the speaker's DMA. These are estimates, plus a calibration of 14 KB, which is what it took for the two real crashes below to reproduce. The real device's STATUS screen shows its free heap; comparing that is the next step in removing the guess.
- **TLS.** Nothing is encrypted. A secure connection holds what mbedTLS would (two 16 KB records from sdkconfig, context, and 24 KB in small pieces over a 1.5 s handshake), and answers come from an in-process stand-in for the server, with the real response shapes and sizes and made-up messages. Production is never contacted.
- **Time and CPU.** A virtual clock, scalable with `--time-scale`. Decoding, SD and handshakes are charged estimated device time. Real sockets don't scale, so `stream` runs at 1x. The 5 s core 0 watchdog is enforced on what tasks do, but tasks are host threads, not scheduled by FreeRTOS priority.

## What it found

Run against two commits that crashed on a real Cardputer, with no change to either:

| Build | Real device, from its core dump | Emulator |
|---|---|---|
| `2b20a87` | `bad_alloc` on the library task, `vector<Item>::emplace_back` | Same task and frame, 3 of 3 runs, within 17 s |
| `0cee15c` | `bad_alloc` on loopTask, a small `Item` copy behind the key handler | `bad_alloc` in 2 of 3 runs, once on loopTask in a small allocation behind a key press (a different frame), once on chat's task. Slower than the real device's instant crash |

It also found, in the new firmware before any device saw it: a repeated chapter when one ended, a list answer arriving after its app had closed, a task deleting itself while being restarted, a JSON read timing out on a slow server, and a decoder probe that reported "not an MP3" when it had really run out of memory.

## Two tiers

`make check` runs both, and SupaChat's upload gate refuses an upload without them.

1. **Scenarios, native** (`tools/check.py`, below): the firmware's source on shims, on a device-sized heap. It sees app behaviour, RAM use over a scenario, keys, screens, audio and network behaviours.
2. **Real binary in QEMU** (`qemu/boot-check.sh`): the actual firmware binary under Espressif's ESP32-S3 QEMU, with the real ROM, bootloader, ESP-IDF startup, heap, newlib, FATFS and prebuilt libraries. It passes when the binary reaches the end of `setup()` and stays up with no panic, assert or corrupt heap. It lacks Wi-Fi, SPI SD (the card goes through SD_MMC instead, via the `qemu` build), display and keyboard hardware, and I2S, so it proves boot and little beyond it: no key is pressed, nothing plays, nothing connects.

## A pass doesn't prove the device boots

The native tier doesn't run ESP-IDF's startup, its real heap, or any prebuilt library (Wi-Fi, lwIP, mbedTLS, newlib, FATFS, the real M5Unified). Tasks, mutexes, the speaker, SD, NVS and the keyboard reader are all shims of what the firmware calls, and they behave as those shims were written. Code that only ran here has not run. A firmware OS layer that passed every scenario here boot-looped on the first real boot, because libc's `free` bypassed its `--wrap` of `malloc`. That exact failure now reproduces here: blocks carry canaries, an open file's memory goes in by `malloc` and out past any hook as libc's does on the device, and a free of a shifted pointer aborts the way `multi_heap_poisoning` does. `tools/repro-wrap-crash.sh` shows it. The next such failure will be a different one.

## What it can't tell you

Anything about the radio (range, retries, interference, the real cost of Wi-Fi buffers under traffic); real SD timing; I2S and the sound itself; the display's real speed; true task scheduling; exact stack use (host frames are larger, so high-water marks are indicative); and anything in the prebuilt ESP-IDF libraries. x86-64 pointers make pointer-heavy structures (a `String` is 24 bytes here, 16 there) somewhat larger than on the device, so those allocations err on the safe side. A scenario passing here means it is worth trying on a device, not that it works on one.

## Power

`--power devices/name.json` predicts battery voltage, the minimum under peak loads and hours left, from a model fitted to one device's own measurements, and fails a scenario when the prediction falls below the brownout level. Without a file, it reports "uncalibrated" and gives no numbers.

The measurements come from the `power-lab` firmware in the SupaChat repo (`firmware/lab/power_lab.cpp`), fitted by `tools/fit-power.py`. `devices/example-synthetic.json` comes from `tools/fake-power-log.py`, a made-up log with known answers: the fit recovers its resistance within 10%, its recovery time exactly and its currents within about 10%. That proves the method, and it describes no real device.

The limits are the hardware's. The device has no current sensor, so every current is inferred: from the discharge rate against the 1750 mAh rating (an aged cell makes all of them wrong by the same ratio) and from sag over resistance. The battery reading is a 12-bit ADC on GPIO10 through a 2:1 divider, single samples, noisy by tens of millivolts; the lab averages and keeps the minimum per 100 ms from about 1 kHz. Whether a fast speaker or radio peak can be seen at all depends on any filtering on the board's divider, which isn't known, so a measured minimum may miss the real one. The brownout level (`CONFIG_ESP32S3_BROWNOUT_DET_LVL 7`, 2.44 V by ESP-IDF's Kconfig) is on the regulated rail; its battery-side equivalent is assumed until the lab records a real brownout.

## Scenarios

Files in `scenarios/`, one command per line: `wait ms`, `press names`, `chord a b`, `type "text"`, `button`, `wifi on|off`, `wait-text "x" ms`, `expect-text`, `expect-no-text`, `show`, `shot file.png`, `report label`, `reboot`, `expect-file path "x"`, `expect-file-gone path`, `remove-file path`, `expect-audio seconds`, `expect-underruns n`, `expect-heap-free n`, `expect-heap-lowest n`, `expect-no-clipping`, `expect-power-ok`, `expect-crash "x"`, `squeeze bytes`, `repeat n` … `end-repeat`. A `# time-scale N` first line sets the speed. Each run leaves `run/<name>/log.txt`, `state/timeline.csv` (free heap and largest block every 5 s from boot) and `state/audio.wav`.

## The other ways to run firmware

[Wokwi](https://wokwi.com/) and [Espressif's QEMU](https://github.com/espressif/qemu) run the real binary, with real RAM use for everything, Wi-Fi driver included, which this can't match. Neither is used yet: QEMU's ESP32-S3 support doesn't cover this board's keyboard, codec or SD wiring, and Wokwi needs a custom part per chip here plus a paid plan for larger SD images. Either could later check this emulator's RAM figures.
