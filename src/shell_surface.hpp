#pragma once

#include <windows.h>

namespace gooserot {

// Identity check using the actual Windows directory.
bool IsKnownShellSurfaceIdentity(const wchar_t* className, const wchar_t* processPath);

// Pure variant used by tests. Modern hosts must live below SystemApps and the
// legacy Explorer path must be the exact executable in the Windows directory.
bool IsKnownShellSurfaceIdentityForWindowsDirectory(const wchar_t* className,
                                                    const wchar_t* processPath,
                                                    const wchar_t* windowsDirectory);

// Resolves the owning executable before applying the strict class/process
// allow-list. A failed query is treated as an ordinary application window.
bool IsKnownShellSurfaceWindow(HWND window);

// Uses the documented GW_HWNDNEXT chain and a hard bound instead of assuming
// an undocumented EnumWindows order.
bool IsWindowAboveInZOrder(HWND candidate, HWND reference);

}  // namespace gooserot
