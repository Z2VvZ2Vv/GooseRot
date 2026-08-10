# 🪿 GooseRot

> **Native Win32 C++17 Desktop Goose & Aura Inspection Application**

[![Language](https://img.shields.io/badge/Language-C%2B%2B17-00599C.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Platform](https://img.shields.io/badge/Platform-Windows%20Win32-0078D6.svg)](https://microsoft.com/windows)
[![Build](https://img.shields.io/badge/Build-MinGW%20GCC-success.svg)](#%EF%B8%8F-building-from-source)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

```text
    _
  __(.)<  GooseRot
  \___)   A 7.5-minute interactive Win32 desktop narrative experience
```

---

> [!CAUTION]
> ### ⚠️ LAB MODE WARNING: VIRTUAL MACHINE (VM) REQUIRED
> Executables built with the **Lab profile** (`GooseRot-Lab.exe` and `GooseRot-Lab-Debug.exe`) are **DESTRUCTIVE AND PERMANENT**. They perform irreversible system modifications, boot chain alterations (MBR/UEFI), registry changes, and simulated system hard errors.
>
> **Lab mode MUST ONLY be run inside a disposable, isolated Virtual Machine (VM)** where full loss of the operating system is expected.
> For daily use or safe demonstrations on your main PC, use **`GooseRot-Safe.exe`** or **`GooseRot-Normal.exe`**.

---

## 📖 About GooseRot

**GooseRot** is a native C++17 Win32 desktop application. Inspired by desktop goose chaos and absurd bureaucratic storytelling, GooseRot sends an intrusive goose inspector directly onto your Windows display.

Over a seeded **7.5-minute interactive story timeline**, the inspector conducts a full **Desktop Aura Inspection**:
1. 🪿 **The Inspector Arrives**: Walks in from off-screen and inspects desktop corners.
2. 📄 **Opens Case 67**: Stamps the desktop and types a real-time hand-typed case file in `GooseRotNotepad`.
3. 📉 **Aura Scoring**: Evaluates and displays a real-time live desktop Aura scorecard.
4. 🖼️ **Collects Evidence**: Walks off-screen to fetch, carry, and pin brainrot photos & meme props.
5. ⚡ **Desktop Directorship**: Hijacks the mouse pointer, nudges open windows, and spawns popups.
6. 🎨 **Graffiti & Condemnation**: Spray-paints the legendary **AURA 67** tag across your screen before the closing aperture shuts down the session.

---

## 🌟 Key Features

- **🚀 Native C++17 & GDI+ Engine**: Zero heavy web view or electron wrappers. Pure Win32 layered overlay rendering.
- **🛡️ Non-Destructive Safe & Normal Profiles**: Zero registry edits or shell modifications in Safe/Normal modes.
- **🐕 Recovery Watchdog Subprocess**: Monitors desktop window state and guarantees 100% restoration of all moved windows.
- **🛑 Instant Emergency Exit**: Press and hold **Esc** at any time to instantly trigger a clean shutdown.
- **🖼️ Embedded Asset Engine**: Self-contained PNG brainrot assets embedded directly inside the binary.
- **📐 Adaptive Visual Budget**: Safe, Normal, Lab and both previews scale their layout to the display and automatically reduce image density on modest hardware.
- **🎬 Deterministic Timeline Engine**: Seeded wall-clock preamble, timeline scaling (`--duration-scale`), and deterministic physics.

---

## 📦 Executable Profiles

GooseRot compiles into single-binary runtime profiles tailored for safety, daily use, or VM testing:

| Executable | Profile | Safety / Environment | Description |
| :--- | :--- | :--- | :--- |
| `GooseRot-Safe.exe` | **Safe** | 🟢 **100% Safe (Main Host)** | Conservative defaults, non-destructive, enhanced emergency exit (<kbd>Esc</kbd> hold). |
| `GooseRot-Normal.exe` | **Normal** | 🟢 **Non-Destructive (Main Host)** | Default production release for regular users. Feature-complete full desktop directorship. |
| `GooseRot-Lab.exe` | **Lab** | 🔴 **DESTRUCTIVE (VM ONLY)** | Experimental/testing build for VM environments. Installs a local Windows service that relaunches the worker if it is killed. |
| `GooseRot-Lab-Debug.exe` | **Lab Debug** | 🔴 **DESTRUCTIVE (VM ONLY)** | VM testing build with attached debug console and real-time verbose logs. |
| `GooseBootPreview.exe` | **Boot Preview** | 🟢 **100% Safe (Main Host)** | Standalone safe Win32 preview of the AURA 67 engine. |

---

## 🎮 Controls & Safety

| Action | Control / Shortcut |
| :--- | :--- |
| **Emergency Shutdown** | Press & hold <kbd>Esc</kbd> for 1.5s |
| **Close Prop Photo** | Click the `[X]` badge on pinned props |
| **Acknowledge Prompts** | Click prompt buttons or press <kbd>Enter</kbd> |

---

## 💻 Usage & CLI Options

Run any profile by double-clicking in Explorer or via PowerShell:

```powershell
.\GooseRot-Normal.exe [options]
```

### CLI Command Options

```text
Usage: GooseRot [options]

Options:
  --safe                  Force Safe profile
  --normal                Force Normal profile
  --lab                   Force Lab profile (requires --vm-confirmed in CLI)
  --preview               Launch offscreen preview window
  --duration-scale <N>    Scale story timeline speed (default: 1.0)
  --no-flashes            Disable photosensitive flashing effects
  --reduced-motion        Disable screen shake and aggressive jitter
  --muted                 Mute all error sounds and audio effects
  --help                  Show help options
```

---

## 🛠️ Building from Source

GooseRot requires a C++17 compliant compiler and CMake 3.20+.

Build natively using **MinGW-w64 GCC** (via MSYS2 or standalone w64devkit):

```powershell
# Configure project with MinGW & Ninja
cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build all targets in parallel
cmake --build build-mingw -j 4
```

Executables will be staged in `build-mingw/bin/`.

---

## 🧪 Testing

Run the automated core unit tests and Win32 integration suite via CTest:

```powershell
ctest --test-dir build-mingw --output-on-failure
```

---

## 📐 Architecture & Technology

- **Language**: C++17
- **Graphics**: Win32 API, GDI+, Layered Windows (`WS_EX_LAYERED`), Software Compositing
- **Build System**: CMake 3.20+ / CTest
- **Toolchain**: MinGW-w64 (GCC)
- **CI/CD**: GitHub Actions ([release.yml](.github/workflows/release.yml))
