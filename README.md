# 🪿 GooseRot

> **Native Win32 C++17 Desktop Goose & Aura Inspection Application**

[![Build and publish Windows executables](https://github.com/GooseRot/GooseRot/actions/workflows/release.yml/badge.svg)](https://github.com/GooseRot/GooseRot/actions/workflows/release.yml)
[![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Platform](https://img.shields.io/badge/Platform-Windows%20Win32-0078D6.svg)](https://microsoft.com/windows)

```text
    _
  __(.)<  GooseRot
  \___)   A 7.5-minute interactive Win32 desktop narrative experience
```

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
- **🛡️ 100% Non-Destructive**: Zero registry edits, zero persistent disk alterations, zero shell modifications.
- **🐕 Recovery Watchdog Subprocess**: Monitors desktop window state and guarantees 100% restoration of all moved windows.
- **🛑 Instant Emergency Exit**: Press and hold **Esc** at any time to instantly trigger a clean shutdown.
- **🖼️ Embedded Asset Engine**: Self-contained PNG brainrot assets embedded directly inside the binary.
- **🎬 Deterministic Timeline Engine**: Seeded wall-clock preamble, timeline scaling (`--duration-scale`), and deterministic physics.

---

## 📦 Executable Profiles

GooseRot compiles into single-binary runtime profiles tailored for safety, daily use, or testing:

| Executable | Profile | Description |
| :--- | :--- | :--- |
| `GooseRot-Safe.exe` | **Safe** | **Recommended for first runs.** Conservative defaults, non-destructive, enhanced emergency exit (Esc hold). |
| `GooseRot-Normal.exe` | **Normal** | Default production release for regular users. Feature-complete full desktop directorship. |
| `GooseRot-Lab.exe` | **Lab** | Experimental/testing build for controlled environments with additional diagnostic hooks. |
| `GooseRot-Lab-Debug.exe` | **Lab Debug** | Development build with attached debug console and real-time verbose logs. |
| `GooseBootPreview.exe` | **Boot Preview** | Standalone safe Win32 preview of the AURA 67 engine. |

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
  --lab                   Force Lab profile
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

### Option 1: MinGW-w64 (Recommended)

Build natively using **MinGW GCC** (via MSYS2 or standalone w64devkit):

```powershell
# Configure project
cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build all targets in parallel
cmake --build build-mingw -j 4
```

Executables will be staged in `build-mingw/bin/`.

### Option 2: Visual Studio (MSVC)

Build using **Visual Studio 2022**:

```powershell
# Build x64 Release
cmake -S . -B build-x64 -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-x64 --config Release --parallel 4

# Build 32-bit (Win32) Release package
cmake -S . -B build-win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build-win32 --config Release --target gooserot_release --parallel 4
```

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
- **Toolchains**: MinGW-w64 (GCC), MSVC 2019/2022
- **CI/CD**: GitHub Actions ([release.yml](.github/workflows/release.yml))
