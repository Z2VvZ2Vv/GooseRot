# AURA 67: Firmware Frenzy — portable core, Preview, and firmware adapters

This directory contains the deterministic mini-game core, its 640×360 software
renderer, fixed-memory helpers, core tests, a safe windowed Win32 Preview, and
experimental freestanding adapters for UEFI x64 and legacy BIOS.

The firmware artifacts compile and pass static layout checks, but they have not
been run under QEMU/OVMF or SeaBIOS because those tools are unavailable in the
current validation environment. They are unsigned, non-installable, and not
runtime-validated. This directory contains no installer, physical-disk writer,
boot-manager modification, UEFI-variable update, persistence mechanism, or
physical-machine deployment procedure.

The Preview does not overlay or control the desktop. Pressing `R` only produces
a `ResetRequested` value inside the core; the Preview ignores it. The firmware
adapters map that signal to a platform reset only after the 67-second result
screen and only when the player explicitly presses `R`; that path remains
runtime-untested.

## Build and test

The sub-project does not depend on the repository's root CMake project:

```powershell
cmake -S boot -B build/boot -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/boot --parallel
ctest --test-dir build/boot --output-on-failure
```

On Windows this produces `GooseBootPreview.exe` and `gooseboot_tests.exe`.
The portable library and tests can also be built on non-Windows hosts by setting
`-DGOOSEBOOT_BUILD_PREVIEW=OFF`.

The experimental firmware build requires 64-bit MinGW GNU and GNU `ld`,
`objcopy`, and `objdump`. From the repository root:

```powershell
cmake -S . -B build-firmware -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DGOOSEROT_BUILD_BOOT_FIRMWARE=ON
cmake --build build-firmware --parallel
ctest --test-dir build-firmware --output-on-failure
cmake --build build-firmware --target gooseboot_firmware_bundle
```

The verified images are generated in `build-firmware/boot/firmware/`. The
explicit bundle target stages the following files in
`build-firmware/boot/firmware-dist/`:

```text
GooseBootX64.efi
gooseboot-bios-stage1.bin
gooseboot-bios-stage2.bin
gooseboot-bios.img
gooseboot-manifest.json
SHA256SUMS.txt
README.txt
```

The manifest deliberately records `trust: experimental-unsigned`,
`installable: false`, and `runtimeValidated: false`. The raw BIOS image is a
build artifact for future disposable-VM testing, not an instruction or tool for
writing a physical disk.

## Controls

| Input | Action |
|---|---|
| Arrows or `WASD` | Move the goose |
| `Space` | Cardinal dash of exactly 67 pixels |
| `Enter` | Start the same seeded round again after 67 seconds |
| `R` | Emit a reset request after the round (ignored by Preview) |
| Hold `Esc` for two seconds | Close the Preview safely |

The game starts at `-10000 AURA`. Green badges award `+9999`; red `NPC`
blocks and the hostile cursor deduct `67`. A dash performed close to the cursor
pushes it by exactly 67 pixels. Palette waves cycle through Matrix green, neon
pink, and critical red until the 67-second result screen.

## Determinism and memory contract

- simulation runs at a fixed 30 ticks/second;
- positions and velocities use Q24.8 fixed-point integers;
- xorshift32 is the only random source and its full state lives in `GameState`;
- entity pools and the complete game state are fixed-size, trivially copyable
  values;
- `game_initialize`, `game_tick`, and `game_render` never allocate;
- framebuffer storage belongs to the platform and must be exactly 640×360,
  32-bit BGRA or RGBA;
- the Win32 Preview uses one static 640×360 pixel array allocated before entry
  to the message loop.

The tests cover equal-seed replay, seed divergence, the exact 67-pixel dash and
cursor push, scoring, the 67-second boundary, end controls, the held-Escape
exit, framebuffer bounds guards, identical rendered frames, and fixed-arena
alignment.

## Clean-room goose renderer

DesktopGoose v0.31 is used only as a behavioral reference. The renderer in
`game/renderer.cpp` is an original integer rasterizer. Its broad silhouette and
locomotion scale were informed by values intentionally exposed in the supplied
public `GooseModdingAPI` source (for example the 22-pixel body radius, procedural
feet, and 80-pixel/second walk tier). No code or asset was extracted from or
copied out of `GooseDesktop.exe`.

## Firmware status and limits

The UEFI x64 adapter stays in Boot Services and uses GOP, Simple Text Input, a
periodic timer, and `ResetSystem` only for the explicit post-game reset request.
If the active GOP mode is smaller than 640×360 it enumerates modes, selects the
smallest compatible one, and restores the original mode on return. Direct
32-bit modes use integer scaling and letterboxing; a Blt-only fallback is
centered at 1:1 because GOP Blt does not scale. Simple Text Input has no key-up
events, so directional keys use a bounded repeat lease and one explicit Escape
press returns to firmware. A bounded `GetTime` catch-up compensates for timer
event coalescing. The inherited image watchdog is disabled before the game.
No IA32 UEFI image is built.

The BIOS chain consists of a 512-byte stage 1 and a fixed 127-sector stage 2.
Stage 1 uses EDD read command `INT 13h/AH=42h` only to load its own stage 2; it
contains no disk-write path and retries its fixed 127-sector EDD transfer up to
three times. Stage 2 requires at least 2 MiB RAM, A20, VBE 2.0, and a linear 32-bit
direct-color mode of at least 640×360 with 8-bit RGB/BGR channels and a
framebuffer at or above 2 MiB. It enters 32-bit protected mode, assumes a PS/2
keyboard or firmware legacy emulation using scan-code set 1, and derives 30 Hz
ticks by polling PIT channel 2 without enabling the speaker.

The layout verifier checks that the UEFI image is PE32+ subsystem 10, has no DLL
imports, and contains usable `.reloc` and `.bss` sections. It also checks the
BIOS sizes and `55 AA` signature, exact stage composition, i386 stage-2 entry at
`0x10000`, and all real-mode VBE/GDT state inside the CS-relative 64 KiB
window. The UEFI checks also require x86-64, a nonzero entry point, a real
`DIR64` relocation, and file-free framebuffer BSS. These are structural checks only:
boot, graphics, keyboard, timer, and reset behavior still require runtime tests
under OVMF and SeaBIOS on blank disposable virtual media.

The adapters remain display/input/time-only during gameplay. They do not
implement disk writes, ESP/MBR edits, BCD changes, UEFI-variable changes,
installation, persistence, or physical-machine deployment.
