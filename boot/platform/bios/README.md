# GooseBoot BIOS adapter

This directory is a minimal, read-only BIOS platform adapter for the existing
`boot/game` AURA 67 core. It is not an installer and has no code for writing a
disk, changing a partition table, installing a boot sector, or modifying an
existing boot chain.

## Platform contract

- `stage1.S` is one 512-byte sector. It uses EDD `INT 13h/AH=42h` only to read
  LBAs 1 through 127 from the device that loaded it, with three bounded retries
  and disk reset after a transient failure. There is no disk-write interrupt
  in either stage.
- `stage2_entry.S` enumerates VBE 2.0 modes, selects the smallest compatible
  linear 32-bit direct-color framebuffer, verifies A20 (fast gate then bounded
  8042 fallback), enters flat 32-bit protected mode, and calls `bios_main`.
  All local real-mode references are CS-relative and VBE ROM lists stay as
  segment:offset pointers.
- `bios_graphics.cpp` gives the common renderer a 640x360 BGRA surface at
  physical `0x00100000`, then integer-scales and letterboxes it into VBE video
  memory.
- `bios_input.cpp` polls scan-code set 1 from the PS/2 controller. BIOS USB
  legacy emulation is therefore required for a USB-only keyboard.
- `bios_clock.cpp` polls PIT channel 2 at 30 Hz. Interrupts remain disabled, so
  this first adapter needs no PIC remap or IDT.
- Holding Escape for two seconds halts safely. After the 67-second result
  screen, pressing `R` explicitly requests an 8042/chipset reset. Neither path
  writes persistent state.

Memory is deliberately non-overlapping:

```text
0x00007c00                  stage 1 / temporary real-mode stack
0x00010000..0x0001fdff     fixed 127-sector stage 2 load and zero-padded state
                            (all real-mode offsets asserted below 0xfe00)
0x0001fe00..<0x00080000    unused
0x00090000 downward        protected-mode stack
0x000a0000..0x000fffff     legacy video/firmware area, unused
0x00100000..0x001e0fff     640x360x4 software framebuffer
```

Stage 2 checks for at least 1024 KiB of extended memory, so the complete map
fits below 2 MiB.

## Local GNU build

The adapter is integrated into the opt-in firmware section of
`boot/CMakeLists.txt`. The following commands show the equivalent low-level
build with the repository's current MinGW GNU tools. The PE files are linker
intermediates only; `objcopy` emits the raw BIOS artifacts.

```powershell
$BiosBuild = Join-Path $env:TEMP "gooserot-bios"
New-Item -ItemType Directory -Force $BiosBuild | Out-Null

$CxxFlags = @(
  "-m32", "-std=c++17", "-Os", "-ffreestanding",
  "-fno-exceptions", "-fno-rtti", "-fno-stack-protector",
  "-fno-threadsafe-statics", "-fno-use-cxa-atexit",
  "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
  "-fno-ident", "-fno-builtin", "-ffunction-sections", "-fdata-sections",
  "-mno-sse", "-mno-sse2", "-mno-mmx", "-msoft-float",
  "-Wall", "-Wextra", "-Wpedantic", "-Wconversion", "-Wshadow", "-Iboot"
)

g++ -m32 -ffreestanding -c boot/platform/bios/stage1.S -o "$BiosBuild/stage1.o"
ld -mi386pe --image-base 0 -T boot/platform/bios/stage1.ld `
  -o "$BiosBuild/stage1.pe" "$BiosBuild/stage1.o"
objcopy -O binary -j .boot "$BiosBuild/stage1.pe" "$BiosBuild/stage1.bin"

g++ -m32 -ffreestanding -c boot/platform/bios/stage2_entry.S -o "$BiosBuild/stage2_entry.o"
g++ @CxxFlags -c boot/platform/bios/bios_main.cpp -o "$BiosBuild/bios_main.o"
g++ @CxxFlags -c boot/platform/bios/bios_graphics.cpp -o "$BiosBuild/bios_graphics.o"
g++ @CxxFlags -c boot/platform/bios/bios_input.cpp -o "$BiosBuild/bios_input.o"
g++ @CxxFlags -c boot/platform/bios/bios_clock.cpp -o "$BiosBuild/bios_clock.o"
g++ @CxxFlags -c boot/game/game.cpp -o "$BiosBuild/game.o"
g++ @CxxFlags -c boot/game/renderer.cpp -o "$BiosBuild/renderer.o"

$Stage2Objects = @(
  "$BiosBuild/stage2_entry.o", "$BiosBuild/bios_main.o",
  "$BiosBuild/bios_graphics.o", "$BiosBuild/bios_input.o",
  "$BiosBuild/bios_clock.o", "$BiosBuild/game.o", "$BiosBuild/renderer.o"
)
ld -mi386pe --image-base 0 -e _stage2_start -T boot/platform/bios/linker.ld `
  -o "$BiosBuild/stage2.pe" @Stage2Objects
objcopy -O binary --gap-fill 0 --pad-to 0x1fe00 `
  "$BiosBuild/stage2.pe" "$BiosBuild/stage2.bin"

if ((Get-Item "$BiosBuild/stage1.bin").Length -ne 512) { throw "invalid stage1 size" }
if ((Get-Item "$BiosBuild/stage2.bin").Length -ne 65024) { throw "invalid stage2 size" }
```

The two raw files concatenate to exactly 128 sectors. Testing must use a new,
disposable blank image under QEMU/SeaBIOS. This environment currently has no
QEMU executable, so hardware-level VBE, PS/2, PIT, halt, and reset behavior has
not yet been exercised.
