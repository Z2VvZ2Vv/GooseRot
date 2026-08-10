#include <windows.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "resource.h"

namespace {

struct ExpectedResource {
  int id;
  const wchar_t* label;
  const wchar_t* path;
};

bool ReadFileBytes(const wchar_t* path, std::vector<unsigned char>& bytes,
                   std::wstring& error) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    error = L"Unable to open firmware input: " + std::wstring(path);
    return false;
  }

  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
      size.QuadPart > static_cast<LONGLONG>(MAXDWORD)) {
    CloseHandle(file);
    error = L"Firmware input has an invalid size: " + std::wstring(path);
    return false;
  }

  bytes.resize(static_cast<std::size_t>(size.QuadPart));
  DWORD bytesRead = 0;
  const bool read = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                             &bytesRead, nullptr) != FALSE;
  CloseHandle(file);
  if (!read || bytesRead != bytes.size()) {
    error = L"Unable to read the complete firmware input: " + std::wstring(path);
    return false;
  }
  return true;
}

bool VerifyResource(HMODULE module, const ExpectedResource& expected,
                    std::wstring& error) {
  HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(expected.id),
                                 MAKEINTRESOURCEW(10));  // RT_RCDATA
  if (!resource) {
    error = std::wstring(expected.label) + L" is not embedded in GooseRot-Lab.exe";
    return false;
  }
  const DWORD resourceSize = SizeofResource(module, resource);
  HGLOBAL loaded = LoadResource(module, resource);
  const void* resourceBytes = loaded ? LockResource(loaded) : nullptr;
  if (!resourceBytes || resourceSize == 0) {
    error = std::wstring(expected.label) + L" has an empty embedded resource";
    return false;
  }

  std::vector<unsigned char> firmwareBytes;
  if (!ReadFileBytes(expected.path, firmwareBytes, error)) return false;
  if (firmwareBytes.size() != resourceSize ||
      std::memcmp(resourceBytes, firmwareBytes.data(), firmwareBytes.size()) != 0) {
    error = std::wstring(expected.label) +
            L" does not exactly match the firmware built in this workflow";
    return false;
  }
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 5) {
    std::wcerr << L"usage: gooserot_lab_resource_tests <Lab.exe> <UEFI> <BIOS1> <BIOS2>\n";
    return 2;
  }

  // LOAD_LIBRARY_AS_DATAFILE prevents the Lab executable from being executed;
  // this process only reads its PE resource directory.
  HMODULE module = LoadLibraryExW(
      argv[1], nullptr,
      LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  if (!module) {
    std::wcerr << L"Unable to open GooseRot-Lab.exe as PE data. Error: "
               << GetLastError() << L"\n";
    return 3;
  }

  const std::array<ExpectedResource, 3> expected = {{
      {IDR_GOOSEBOOT_UEFI, L"GooseBootX64.efi", argv[2]},
      {IDR_GOOSEBOOT_BIOS_STAGE1, L"gooseboot-bios-stage1.bin", argv[3]},
      {IDR_GOOSEBOOT_BIOS_STAGE2, L"gooseboot-bios-stage2.bin", argv[4]},
  }};

  for (const ExpectedResource& resource : expected) {
    std::wstring error;
    if (!VerifyResource(module, resource, error)) {
      std::wcerr << error << L"\n";
      FreeLibrary(module);
      return 4;
    }
  }

  FreeLibrary(module);
  std::wcout << L"GooseRot-Lab.exe contains all three exact firmware resources.\n";
  return 0;
}
