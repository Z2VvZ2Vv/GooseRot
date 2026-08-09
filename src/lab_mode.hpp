#pragma once

#include <string>

namespace gooserot {

using LabLogSink = void (*)(const std::wstring& message);

void SetLabLogSink(LabLogSink sink);

enum class LabFirmwareKind {
  Bios,
  Uefi,
};

struct LabStartupArtifacts {
  LabFirmwareKind firmware = LabFirmwareKind::Bios;
  std::wstring tempDirectory;
  std::wstring uefiImage;
  std::wstring biosStage1;
  std::wstring biosStage2;
};

// Detects the active firmware interface and extracts only the matching
// embedded artifacts into %TEMP%. It does not mount, install or overwrite any
// disk, partition, firmware variable or boot configuration.
bool RunLabStartup(LabStartupArtifacts& artifacts, std::wstring& error);

// Lab-only extension point invoked on the final black frame.
bool RunLabConclusion(const LabStartupArtifacts& artifacts, std::wstring& error);

}  // namespace gooserot
