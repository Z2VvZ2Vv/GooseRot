# AURA 67: POST Runner — portable core, Preview, and firmware adapters

This directory contains the deterministic mini-game core, its 640×360 software
renderer, fixed-memory helpers, core tests, a safe windowed Win32 Preview, and
experimental freestanding adapters for UEFI x64 and legacy BIOS.

The firmware artifacts compile, pass the static layout checks, and now boot in
an emulator: both targets were run under QEMU 8.2.2 (TCG, no KVM) with OVMF
2024.02 for UEFI x64 and SeaBIOS 1.16.3 for legacy BIOS. Observed there: the
game reaches its render loop, the HUD and goose draw correctly, the keyboard
restarts a run, `R` performs a real platform reset, and one `Esc` press returns
the UEFI application to the firmware menu with the original GOP mode restored.

They remain unsigned and non-installable, and they are still unvalidated on
physical hardware and on every other hypervisor. This directory contains no
installer, physical-disk writer, boot-manager modification, UEFI-variable
update, persistence mechanism, or physical-machine deployment procedure.

The Preview does not overlay or control the desktop. Pressing `R` only produces
a `ResetRequested` value inside the core; the Preview ignores it. The firmware
adapters map that signal to a platform reset only on the kernel-panic screen
that follows a crash, and only when the player explicitly presses `R`; that path
remains runtime-untested.

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
build artifact for disposable-VM testing, not an instruction or tool for writing
a physical disk.

### Cross-building the firmware from Linux

The firmware targets are PE/COFF, so a Linux host needs the mingw-w64 toolchain
and can then use the same rules:

```bash
apt-get install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64
cmake -S boot -B build-firmware -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=boot/cmake/toolchain-mingw-w64.cmake \
      -DGOOSEBOOT_BUILD_FIRMWARE=ON -DGOOSEBOOT_BUILD_PREVIEW=OFF
cmake --build build-firmware --parallel
```

A cross build produces every firmware binary except the raw `gooseboot-bios.img`
and skips `gooseboot_firmware_bundle`, because that one artifact is assembled by
a tool that has to run on the build host. The two bootable ISOs do not need it.

### Bootable test images

`boot/tools/make_boot_isos.sh` turns built firmware into two ISOs for a
throwaway virtual machine. It needs `xorriso`, `mtools` and `dosfstools`, and it
only ever writes ordinary files inside the output directory it is given:

```bash
./boot/tools/make_boot_isos.sh build-firmware/firmware build-firmware/iso
```

| Image | El Torito entry | What the firmware runs |
|---|---|---|
| `gooseboot-uefi.iso` | EFI platform, no emulation, points at a FAT ESP image | `EFI/BOOT/BOOTX64.EFI` |
| `gooseboot-bios.iso` | BIOS platform, no emulation, 128 virtual sectors at the default 0x7C00 | `gooseboot-bios-cdstub.bin` followed by stage 2 |

Stage 1 is not used on optical media: its `INT 13h/AH=42h` path assumes 512-byte
sectors, while a no-emulation CD boot exposes 2048-byte sectors. The CD stub in
`platform/bios/cdrom_stub.S` replaces it with a pure in-memory move — the
firmware is asked to load the whole 128-sector image, so the stub performs no
disk call at all, and the layout verifier asserts the sector contains no `INT
13h` opcode. It relies only on the default 0x7C00 load address, so no firmware
has to honour a custom El Torito load segment.

## The game

An endless side-scrolling run, in the spirit of the offline dinosaur, played on
a motherboard. The goose runs by itself, the board scrolls faster and faster,
and the run only ends when something hits her. There is no timer and no finish
line: the score is the whole point, and the previous best stays on the HUD.

| Input | Action |
|---|---|
| `Space`, `Up` or `W` | Jump. Press again in mid-air for one wing flap |
| `Down` or `S` | Duck on the ground, dive when airborne |
| `Space` or `Enter` on the panic screen | Start the next run immediately |
| `R` on the panic screen | Emit a reset request (ignored by Preview) |
| Hold `Esc` for two seconds | Close the Preview safely |

A ground jump lasts exactly 25 ticks and peaks 86 pixels up. Every wave the
spawner rolls is guaranteed to fit inside that arc: the shortest gap it may
draw is 36 ticks, and patterns unlock progressively — memory modules and
capacitors first, then flying cursors to duck under, RAM clusters, high cursors
that punish a reflex jump, and finally full blue screens.

Scoring keeps the project's `67` economy:

- one AURA per five pixels travelled, which is the backbone of the score;
- badges pay `67` multiplied by the current chain;
- shaving an obstacle by 14 pixels or less is a `CLUTCH`: `67` and one chain step;
- the chain is built by risk only — never by picking badges up — and lapses
  after five quiet seconds;
- an overclock chip arms `ROOT MODE` for exactly 67 ticks: obstacles shatter for
  `67` each instead of ending the run;
- the firmware palette flips every `670` AURA, and `9999` lights `MAX BRAINROT`.

The scroll speed ramps from 345 to 622 pixels per second over roughly 79
seconds and then holds while obstacle density keeps its ceiling, so the endless
part is a flow state rather than an eventually impossible wall.

Crashing shows a `KERNEL PANIC` panel with the run's AURA, the best score, the
badge/clutch/cleared counters and how long the goose survived. A 20-tick lockout
stops the keypress that caused the crash from skipping the panel.

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

Runs never re-seed the generator, so a whole session — crash, restart, crash
again — replays identically from one seed while each individual run still gets a
fresh course.

The tests cover equal-seed replay, seed divergence, the fixed 25-tick/86-pixel
jump arc, the single wing flap per airtime, ducking under a head-height cursor,
the `67` economy and chain rules, ROOT MODE shattering and expiring on schedule,
a 10 000-tick run proving no timer can end it, spawner gaps staying above the
jump bound on six seeds, a reactive autopilot surviving the ramp on five seeds,
record keeping across restarts, the reset request being reachable only after a
crash, the held-Escape exit, framebuffer bounds guards, identical rendered
frames, and fixed-arena alignment.

## Clean-room goose renderer

DesktopGoose v0.31 is used only as a behavioral reference. The renderer in
`game/renderer.cpp` is an original integer rasterizer: side-view goose with a
four-frame run cycle, tucked airborne legs and a crouch pose, drawn over three
parallax layers of firmware scenery (setup grid and drifting dialogs, a memory
map histogram, then sockets, capacitor banks and DIMM slots). Its broad
silhouette and locomotion scale were informed by values intentionally exposed in
the supplied public `GooseModdingAPI` source (for example the 22-pixel body
radius, procedural feet, and 80-pixel/second walk tier). No code or asset was
extracted from or copied out of `GooseDesktop.exe`.

## Firmware status and limits

The UEFI x64 adapter stays in Boot Services and uses GOP, Simple Text Input, a
periodic timer, and `ResetSystem` only for the reset the panic screen offers.
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
`DIR64` relocation, and file-free framebuffer BSS. When a CD stub is present it
must be 512 bytes, carry `55 AA`, and contain no `INT 13h` opcode.

These are structural checks. Boot, graphics, keyboard, timer and reset behaviour
were confirmed separately by running both ISOs under QEMU 8.2.2 with OVMF
2024.02 and SeaBIOS 1.16.3: UEFI came up letterboxed at 1280×720 inside a
1280×800 GOP mode, BIOS selected a 640×400 VBE mode, both accepted keystrokes
and restarted runs, `R` reset the machine, and `Esc` returned the UEFI build to
the firmware menu with the original mode restored. Physical hardware, VMware,
VirtualBox and Hyper-V remain untested.

The adapters remain display/input/time-only during gameplay. They do not
implement disk writes, ESP/MBR edits, BCD changes, UEFI-variable changes,
installation, persistence, or physical-machine deployment.
