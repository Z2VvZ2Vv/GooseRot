#include "shell_surface.hpp"

#include <algorithm>
#include <array>
#include <cwchar>
#include <string>

namespace gooserot {
namespace {

const wchar_t* BaseName(const wchar_t* path) {
  if (!path) return L"";
  const wchar_t* slash = std::wcsrchr(path, L'\\');
  const wchar_t* forwardSlash = std::wcsrchr(path, L'/');
  if (!slash || (forwardSlash && forwardSlash > slash)) slash = forwardSlash;
  return slash ? slash + 1 : path;
}

std::wstring NormalizePath(const wchar_t* path) {
  std::wstring normalized = path ? path : L"";
  std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
  while (normalized.size() > 3U && normalized.back() == L'\\') normalized.pop_back();
  return normalized;
}

bool ContainsDotSegment(const std::wstring& path) {
  std::size_t offset = 0;
  while (offset <= path.size()) {
    const std::size_t separator = path.find(L'\\', offset);
    const std::size_t length = separator == std::wstring::npos
                                   ? path.size() - offset
                                   : separator - offset;
    if ((length == 1U && path[offset] == L'.') ||
        (length == 2U && path[offset] == L'.' && path[offset + 1U] == L'.')) {
      return true;
    }
    if (separator == std::wstring::npos) break;
    offset = separator + 1U;
  }
  return false;
}

bool EqualsPath(const std::wstring& left, const std::wstring& right) {
  return left.size() == right.size() && _wcsicmp(left.c_str(), right.c_str()) == 0;
}

bool IsPathInside(const std::wstring& path, const std::wstring& directory) {
  if (path.size() <= directory.size() || ContainsDotSegment(path) ||
      ContainsDotSegment(directory)) {
    return false;
  }
  return _wcsnicmp(path.c_str(), directory.c_str(), directory.size()) == 0 &&
         path[directory.size()] == L'\\';
}

bool QueryWindowsDirectory(std::wstring& directory) {
  std::array<wchar_t, MAX_PATH> buffer{};
  const UINT length = GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
  if (length == 0 || length >= buffer.size()) return false;
  directory = NormalizePath(buffer.data());
  return !directory.empty();
}

bool IsKnownModernShellProcess(const wchar_t* path) {
  const wchar_t* name = BaseName(path);
  constexpr std::array<const wchar_t*, 4> processes = {
      L"StartMenuExperienceHost.exe", L"SearchHost.exe",
      L"SearchApp.exe", L"ShellExperienceHost.exe"};
  for (const wchar_t* process : processes) {
    if (_wcsicmp(name, process) == 0) return true;
  }
  return false;
}

bool IsExplorerPath(const std::wstring& processPath, const std::wstring& windowsDirectory) {
  return EqualsPath(processPath, windowsDirectory + L"\\explorer.exe");
}

}  // namespace

bool IsKnownShellSurfaceIdentity(const wchar_t* className, const wchar_t* processPath) {
  std::wstring windowsDirectory;
  return QueryWindowsDirectory(windowsDirectory) &&
         IsKnownShellSurfaceIdentityForWindowsDirectory(
             className, processPath, windowsDirectory.c_str());
}

bool IsKnownShellSurfaceIdentityForWindowsDirectory(const wchar_t* className,
                                                    const wchar_t* processPath,
                                                    const wchar_t* windowsDirectory) {
  if (!className || !*className || !processPath || !*processPath ||
      !windowsDirectory || !*windowsDirectory) {
    return false;
  }
  const std::wstring normalizedPath = NormalizePath(processPath);
  const std::wstring normalizedWindows = NormalizePath(windowsDirectory);
  if (normalizedPath.empty() || normalizedWindows.empty()) return false;

  const bool modernHost = IsKnownModernShellProcess(normalizedPath.c_str()) &&
                          IsPathInside(normalizedPath,
                                       normalizedWindows + L"\\SystemApps");
  if (_wcsicmp(className, L"Windows.UI.Core.CoreWindow") == 0) return modernHost;
  if (_wcsicmp(className, L"DV2ControlHost") == 0) {
    return modernHost || IsExplorerPath(normalizedPath, normalizedWindows);
  }
  return false;
}

bool IsKnownShellSurfaceWindow(HWND window) {
  if (!window) return false;
  wchar_t className[128]{};
  if (GetClassNameW(window, className, static_cast<int>(std::size(className))) == 0) {
    return false;
  }
  if (_wcsicmp(className, L"Windows.UI.Core.CoreWindow") != 0 &&
      _wcsicmp(className, L"DV2ControlHost") != 0) {
    return false;
  }

  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  if (!processId) return false;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (!process) return false;
  wchar_t processPath[1024]{};
  DWORD length = static_cast<DWORD>(std::size(processPath));
  const bool accepted = QueryFullProcessImageNameW(process, 0, processPath, &length) != 0 &&
                        IsKnownShellSurfaceIdentity(className, processPath);
  CloseHandle(process);
  if (!accepted) return false;

  // The legacy Start host must be the same Explorer instance that owns the
  // desktop shell, not an unrelated executable copied under a familiar name.
  if (_wcsicmp(className, L"DV2ControlHost") == 0) {
    std::wstring windowsDirectory;
    if (!QueryWindowsDirectory(windowsDirectory)) return false;
    const std::wstring normalizedPath = NormalizePath(processPath);
    if (IsExplorerPath(normalizedPath, windowsDirectory)) {
      DWORD shellProcessId = 0;
      const HWND shellWindow = GetShellWindow();
      if (!shellWindow) return false;
      GetWindowThreadProcessId(shellWindow, &shellProcessId);
      if (shellProcessId == 0 || shellProcessId != processId) return false;
    }
  }
  return true;
}

bool IsWindowAboveInZOrder(HWND candidate, HWND reference) {
  if (!candidate || !reference || candidate == reference) return false;
  HWND current = candidate;
  constexpr unsigned kMaximumWindows = 4096;
  for (unsigned visited = 0; visited < kMaximumWindows; ++visited) {
    HWND next = GetWindow(current, GW_HWNDNEXT);
    if (!next || next == current || next == candidate) return false;
    if (next == reference) return true;
    current = next;
  }
  return false;
}

bool ShouldDismissShellSurfaceState(bool foreground, bool visible, bool iconic,
                                    bool intersectsOverlay, bool knownIdentity,
                                    bool aboveOverlay) {
  return visible && !iconic && intersectsOverlay && knownIdentity &&
         (foreground || aboveOverlay);
}

}  // namespace gooserot
