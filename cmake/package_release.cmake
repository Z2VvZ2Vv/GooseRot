foreach(profile SAFE NORMAL LAB LAB_DEBUG)
  if(NOT DEFINED GOOSEROT_${profile}_EXE OR
     NOT EXISTS "${GOOSEROT_${profile}_EXE}")
    message(FATAL_ERROR "GOOSEROT_${profile}_EXE does not name a built GooseRot executable")
  endif()
endforeach()
if(NOT DEFINED README_SOURCE OR NOT EXISTS "${README_SOURCE}")
  message(FATAL_ERROR "README_SOURCE is missing")
endif()
if(NOT DEFINED OUTPUT_DIR OR OUTPUT_DIR STREQUAL "")
  message(FATAL_ERROR "OUTPUT_DIR is missing")
endif()
if(REQUIRE_RELEASE AND NOT BUILD_CONFIGURATION STREQUAL "Release")
  message(FATAL_ERROR "gooserot_release must be built with --config Release")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(REMOVE
  "${OUTPUT_DIR}/GooseRot.exe"
  "${OUTPUT_DIR}/GooseRot-Safe.exe"
  "${OUTPUT_DIR}/GooseRot-Normal.exe"
  "${OUTPUT_DIR}/GooseRot-Lab.exe"
  "${OUTPUT_DIR}/GooseRot-Lab-Debug.exe"
  "${OUTPUT_DIR}/GooseBootX64.efi"
  "${OUTPUT_DIR}/gooseboot-bios-stage1.bin"
  "${OUTPUT_DIR}/gooseboot-bios-stage2.bin"
  "${OUTPUT_DIR}/gooseboot-bios.img"
  "${OUTPUT_DIR}/GooseBootPreview.exe"
  "${OUTPUT_DIR}/README.txt"
  "${OUTPUT_DIR}/SHA256SUMS.txt")
configure_file("${GOOSEROT_SAFE_EXE}" "${OUTPUT_DIR}/GooseRot-Safe.exe" COPYONLY)
configure_file("${GOOSEROT_NORMAL_EXE}" "${OUTPUT_DIR}/GooseRot-Normal.exe" COPYONLY)
configure_file("${GOOSEROT_LAB_EXE}" "${OUTPUT_DIR}/GooseRot-Lab.exe" COPYONLY)
configure_file("${GOOSEROT_LAB_DEBUG_EXE}" "${OUTPUT_DIR}/GooseRot-Lab-Debug.exe" COPYONLY)
configure_file("${README_SOURCE}" "${OUTPUT_DIR}/README.txt" COPYONLY)

set(checksums "")
foreach(exe_name IN ITEMS
    GooseRot-Safe.exe
    GooseRot-Normal.exe
    GooseRot-Lab.exe
    GooseRot-Lab-Debug.exe)
  file(SHA256 "${OUTPUT_DIR}/${exe_name}" gooserot_sha256)
  string(APPEND checksums "${gooserot_sha256}  ${exe_name}\n")
endforeach()

if(DEFINED GOOSEBOOT_PREVIEW AND NOT GOOSEBOOT_PREVIEW STREQUAL "" AND
   EXISTS "${GOOSEBOOT_PREVIEW}")
  configure_file("${GOOSEBOOT_PREVIEW}" "${OUTPUT_DIR}/GooseBootPreview.exe" COPYONLY)
  file(SHA256 "${OUTPUT_DIR}/GooseBootPreview.exe" preview_sha256)
  string(APPEND checksums "${preview_sha256}  GooseBootPreview.exe\n")
endif()

file(WRITE "${OUTPUT_DIR}/SHA256SUMS.txt" "${checksums}")
message(STATUS "GooseRot bundle staged in ${OUTPUT_DIR}")
