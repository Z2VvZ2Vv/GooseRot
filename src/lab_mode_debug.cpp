#include "lab_mode.hpp"

#include <windows.h>

#include <array>
#include <cstring>
#include <string>

#include "resource.h"

namespace gooserot {
namespace {

LabLogSink g_labLogSink = nullptr;

void EmitLabLog(const std::wstring& message) {
  if (g_labLogSink) g_labLogSink(message);
}

enum class FirmwareKind {
  Unknown = 0,
  Bios = 1,
  Uefi = 2,
};

FirmwareKind DetectFirmwareKind() {
  EmitLabLog(L"[lab-debug] Detecting firmware kind...");
  using GetFirmwareTypeProcedure = BOOL(WINAPI*)(DWORD*);
  const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
  if (kernel) {
    const FARPROC address = GetProcAddress(kernel, "GetFirmwareType");
    if (address) {
      GetFirmwareTypeProcedure getFirmwareType = nullptr;
      static_assert(sizeof(getFirmwareType) == sizeof(address));
      std::memcpy(&getFirmwareType, &address, sizeof(getFirmwareType));
      DWORD value = 0;
      if (getFirmwareType(&value)) {
        if (value == 1) return FirmwareKind::Bios;
        if (value == 2) return FirmwareKind::Uefi;
      }
    }
  }

  SetLastError(ERROR_SUCCESS);
  GetFirmwareEnvironmentVariableW(
      L"", L"{00000000-0000-0000-0000-000000000000}", nullptr, 0);
  const DWORD firmwareError = GetLastError();
  if (firmwareError == ERROR_INVALID_FUNCTION) return FirmwareKind::Bios;
  if (firmwareError != ERROR_SUCCESS) return FirmwareKind::Uefi;
  return FirmwareKind::Unknown;
}

bool LabTempDirectory(std::wstring& directory) {
  std::array<wchar_t, 32768> temp{};
  const DWORD length =
      GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
  if (length == 0 || length >= temp.size()) return false;
  directory.assign(temp.data(), length);
  if (!directory.empty() && directory.back() != L'\\') directory += L'\\';
  directory += L"GooseRot-Lab";
  if (CreateDirectoryW(directory.c_str(), nullptr)) return true;
  if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
  const DWORD attributes = GetFileAttributesW(directory.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ExtractResourceToFile(int resourceId, const wchar_t* artifactName,
                           const std::wstring& destination,
                           std::wstring& error) {
  EmitLabLog(L"[lab-debug] Looking up RCDATA resource " +
             std::to_wstring(resourceId) + L": " + artifactName);
  const HMODULE module = GetModuleHandleW(nullptr);
  const HRSRC resource =
      module ? FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA)
             : nullptr;
  if (!resource) {
    error = std::wstring(artifactName) +
            L" is not embedded in GooseRot-Lab-Debug.exe.";
    EmitLabLog(L"[lab-debug] Resource lookup failed with Win32 error " +
               std::to_wstring(GetLastError()));
    return false;
  }

  const DWORD size = SizeofResource(module, resource);
  const HGLOBAL loaded = LoadResource(module, resource);
  const void* bytes = loaded ? LockResource(loaded) : nullptr;
  if (size == 0 || !bytes) {
    error = L"Unable to read the embedded " + std::wstring(artifactName) + L".";
    return false;
  }

  const HANDLE file =
      CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                  FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    error = L"Unable to create " + destination + L".";
    return false;
  }

  const auto* cursor = static_cast<const BYTE*>(bytes);
  DWORD remaining = size;
  bool success = true;
  while (remaining != 0) {
    DWORD written = 0;
    if (!WriteFile(file, cursor, remaining, &written, nullptr) || written == 0) {
      success = false;
      break;
    }
    cursor += written;
    remaining -= written;
  }
  CloseHandle(file);
  if (!success) {
    DeleteFileW(destination.c_str());
    error = L"Unable to extract " + std::wstring(artifactName) + L".";
    return false;
  }

  EmitLabLog(L"[lab-debug] Extracted " + std::wstring(artifactName) + L" (" +
             std::to_wstring(size) + L" bytes) to " + destination);
  return true;
}

bool PublishArtifacts(const LabStartupArtifacts& artifacts,
                      std::wstring& error) {
  const wchar_t* uefi =
      artifacts.uefiImage.empty() ? nullptr : artifacts.uefiImage.c_str();
  const wchar_t* stage1 =
      artifacts.biosStage1.empty() ? nullptr : artifacts.biosStage1.c_str();
  const wchar_t* stage2 =
      artifacts.biosStage2.empty() ? nullptr : artifacts.biosStage2.c_str();
  const wchar_t* primary =
      artifacts.firmware == LabFirmwareKind::Uefi ? uefi : stage2;

  if (!SetEnvironmentVariableW(
          L"GOOSEROT_FIRMWARE_TYPE",
          artifacts.firmware == LabFirmwareKind::Uefi ? L"UEFI" : L"BIOS") ||
      !SetEnvironmentVariableW(L"GOOSEROT_LAB_DIRECTORY",
                               artifacts.tempDirectory.c_str()) ||
      !SetEnvironmentVariableW(L"GOOSEROT_LAB_UEFI", uefi) ||
      !SetEnvironmentVariableW(L"GOOSEROT_LAB_BIOS_STAGE1", stage1) ||
      !SetEnvironmentVariableW(L"GOOSEROT_LAB_BIOS_STAGE2", stage2) ||
      !SetEnvironmentVariableW(L"GOOSEROT_LAB_GAME", primary)) {
    error = L"Unable to publish the Lab extraction information.";
    return false;
  }
  return true;
}

}  // namespace

void SetLabLogSink(LabLogSink sink) { g_labLogSink = sink; }

bool RunLabStartup(LabStartupArtifacts& artifacts, std::wstring& error) {
  EmitLabLog(L"[lab-debug] Starting extraction-only diagnostics");
  EmitLabLog(L"[lab-debug] No Windows restriction, disk mount or boot write will run");
  artifacts = {};

  const FirmwareKind firmware = DetectFirmwareKind();
  if (firmware == FirmwareKind::Unknown) {
    error = L"Unable to determine whether this machine uses BIOS or UEFI.";
    return false;
  }
  EmitLabLog(firmware == FirmwareKind::Uefi
                 ? L"[lab-debug] Firmware detection reports UEFI"
                 : L"[lab-debug] Firmware detection reports BIOS");

  if (!LabTempDirectory(artifacts.tempDirectory)) {
    error = L"Unable to prepare the Lab temporary directory.";
    return false;
  }
  EmitLabLog(L"[lab-debug] Temporary directory ready: " +
             artifacts.tempDirectory);

  if (firmware == FirmwareKind::Uefi) {
    artifacts.firmware = LabFirmwareKind::Uefi;
    artifacts.uefiImage = artifacts.tempDirectory + L"\\GooseBootX64.efi";
    if (!ExtractResourceToFile(IDR_GOOSEBOOT_UEFI, L"GooseBootX64.efi",
                               artifacts.uefiImage, error)) {
      return false;
    }
  } else {
    artifacts.firmware = LabFirmwareKind::Bios;
    artifacts.biosStage1 =
        artifacts.tempDirectory + L"\\gooseboot-bios-stage1.bin";
    artifacts.biosStage2 =
        artifacts.tempDirectory + L"\\gooseboot-bios-stage2.bin";
    if (!ExtractResourceToFile(IDR_GOOSEBOOT_BIOS_STAGE1,
                               L"gooseboot-bios-stage1.bin",
                               artifacts.biosStage1, error) ||
        !ExtractResourceToFile(IDR_GOOSEBOOT_BIOS_STAGE2,
                               L"gooseboot-bios-stage2.bin",
                               artifacts.biosStage2, error)) {
      return false;
    }
  }

  if (!PublishArtifacts(artifacts, error)) return false;
  EmitLabLog(L"[lab-debug] Extraction diagnostics completed successfully");
  return true;
}

bool RunLabConclusion(const LabStartupArtifacts& artifacts,
                      std::wstring& error) {
  (void)artifacts;
  (void)error;
  EmitLabLog(L"[lab-debug] Conclusion reached; no system action configured");
  return true;
}

}  // namespace gooserot
