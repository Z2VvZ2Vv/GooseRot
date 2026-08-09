
   _
 __(.)<  GooseRot
 \___)   

GooseRot — Win32 desktop application (C++17)

About this project
------------------
GooseRot is a native Win32 desktop application written in C++17. The project contains single-binary runtime profiles for normal use, a safety-focused profile, and two lab/testing builds.

Release builds (what each executable is)
--------------------------------------
- `GooseRot-Safe.exe`: production-safe profile. Minimal risky features, emergency exit behaviour (Esc) and conservative defaults.
- `GooseRot-Normal.exe`: default production build for regular users; optimized and feature-complete.
- `GooseRot-Lab.exe`: lab/testing profile. Enables internal checks, test hooks, and behaviors intended for controlled environments.
- `GooseRot-Lab-Debug.exe`: lab debug build. Includes debug symbols and verbose logging for diagnosis (not for end users).

Where to get them
------------------
- Locally: build with CMake and find the binaries in your build output (for example `build/` or `build/dist/`).
- CI: pushes to the `release` branch trigger a GitHub Actions workflow that uploads the four executables as the `goose-executables` artifact.

Run them
-------
Double-click in Explorer or run from PowerShell:

```powershell
.\GooseRot-Normal.exe
```

Build (quick)
-------------
Recommended (MSVC x64):

```powershell
mkdir build
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target ALL_BUILD -- /m
```

Alternative (Visual Studio generator, Win32):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --target gooserot_release --parallel
```

CI workflow: [.github/workflows/release.yml](.github/workflows/release.yml#L1)

Stacks
------
- C++17
- CMake / CTest
- MSVC (Visual Studio) on Windows
- GitHub Actions

Want signing, auto-tagging, or publishing on GitHub Releases? Tell me which and I will add the workflow changes.
