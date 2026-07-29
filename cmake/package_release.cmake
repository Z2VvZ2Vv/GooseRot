if(NOT DEFINED GOOSEROT_EXE OR NOT EXISTS "${GOOSEROT_EXE}")
  message(FATAL_ERROR "GOOSEROT_EXE does not name a built GooseRot executable")
endif()
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
  "${OUTPUT_DIR}/GooseBootPreview.exe"
  "${OUTPUT_DIR}/README.txt"
  "${OUTPUT_DIR}/SHA256SUMS.txt")
configure_file("${GOOSEROT_EXE}" "${OUTPUT_DIR}/GooseRot.exe" COPYONLY)
configure_file("${README_SOURCE}" "${OUTPUT_DIR}/README.txt" COPYONLY)

file(SHA256 "${OUTPUT_DIR}/GooseRot.exe" gooserot_sha256)
set(checksums "${gooserot_sha256}  GooseRot.exe\n")

if(DEFINED GOOSEBOOT_PREVIEW AND NOT GOOSEBOOT_PREVIEW STREQUAL "" AND
   EXISTS "${GOOSEBOOT_PREVIEW}")
  configure_file("${GOOSEBOOT_PREVIEW}" "${OUTPUT_DIR}/GooseBootPreview.exe" COPYONLY)
  file(SHA256 "${OUTPUT_DIR}/GooseBootPreview.exe" preview_sha256)
  string(APPEND checksums "${preview_sha256}  GooseBootPreview.exe\n")
endif()

file(WRITE "${OUTPUT_DIR}/SHA256SUMS.txt" "${checksums}")
message(STATUS "GooseRot bundle staged in ${OUTPUT_DIR}")
