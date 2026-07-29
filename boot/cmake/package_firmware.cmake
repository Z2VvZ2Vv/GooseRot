foreach(required_variable
        UEFI_IMAGE BIOS_STAGE1 BIOS_STAGE2 BIOS_IMAGE README_SOURCE OUTPUT_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
    if(required_variable STREQUAL "OUTPUT_DIR")
        continue()
    endif()
    if(NOT EXISTS "${${required_variable}}")
        message(FATAL_ERROR "Missing firmware bundle input: ${${required_variable}}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(known_outputs
    GooseBootX64.efi
    gooseboot-bios-stage1.bin
    gooseboot-bios-stage2.bin
    gooseboot-bios.img
    gooseboot-manifest.json
    README.txt
    SHA256SUMS.txt)
foreach(known_output IN LISTS known_outputs)
    file(REMOVE "${OUTPUT_DIR}/${known_output}")
endforeach()

configure_file("${UEFI_IMAGE}" "${OUTPUT_DIR}/GooseBootX64.efi" COPYONLY)
configure_file("${BIOS_STAGE1}" "${OUTPUT_DIR}/gooseboot-bios-stage1.bin" COPYONLY)
configure_file("${BIOS_STAGE2}" "${OUTPUT_DIR}/gooseboot-bios-stage2.bin" COPYONLY)
configure_file("${BIOS_IMAGE}" "${OUTPUT_DIR}/gooseboot-bios.img" COPYONLY)
configure_file("${README_SOURCE}" "${OUTPUT_DIR}/README.txt" COPYONLY)

set(artifact_names
    GooseBootX64.efi
    gooseboot-bios-stage1.bin
    gooseboot-bios-stage2.bin
    gooseboot-bios.img)
foreach(artifact IN LISTS artifact_names)
    file(SIZE "${OUTPUT_DIR}/${artifact}" "${artifact}_size")
    file(SHA256 "${OUTPUT_DIR}/${artifact}" "${artifact}_sha")
endforeach()

set(manifest "{\n")
string(APPEND manifest "  \"schema\": 1,\n")
string(APPEND manifest "  \"game\": \"aura-67\",\n")
string(APPEND manifest "  \"version\": \"0.1.0\",\n")
string(APPEND manifest "  \"trust\": \"experimental-unsigned\",\n")
string(APPEND manifest "  \"installable\": false,\n")
string(APPEND manifest "  \"runtimeValidated\": false,\n")
string(APPEND manifest "  \"artifacts\": [\n")
string(APPEND manifest
    "    {\"file\": \"GooseBootX64.efi\", \"platform\": \"uefi\", \"architecture\": \"x86_64\", \"size\": ${GooseBootX64.efi_size}, \"sha256\": \"${GooseBootX64.efi_sha}\"},\n")
string(APPEND manifest
    "    {\"file\": \"gooseboot-bios-stage1.bin\", \"platform\": \"bios-stage1\", \"architecture\": \"i386\", \"size\": ${gooseboot-bios-stage1.bin_size}, \"sha256\": \"${gooseboot-bios-stage1.bin_sha}\"},\n")
string(APPEND manifest
    "    {\"file\": \"gooseboot-bios-stage2.bin\", \"platform\": \"bios-stage2\", \"architecture\": \"i386\", \"size\": ${gooseboot-bios-stage2.bin_size}, \"sha256\": \"${gooseboot-bios-stage2.bin_sha}\"},\n")
string(APPEND manifest
    "    {\"file\": \"gooseboot-bios.img\", \"platform\": \"bios-image\", \"architecture\": \"i386\", \"size\": ${gooseboot-bios.img_size}, \"sha256\": \"${gooseboot-bios.img_sha}\"}\n")
string(APPEND manifest "  ]\n}\n")
file(WRITE "${OUTPUT_DIR}/gooseboot-manifest.json" "${manifest}")

set(checksums "")
foreach(bundle_file IN LISTS artifact_names)
    file(SHA256 "${OUTPUT_DIR}/${bundle_file}" bundle_sha)
    string(APPEND checksums "${bundle_sha}  ${bundle_file}\n")
endforeach()
foreach(bundle_file gooseboot-manifest.json README.txt)
    file(SHA256 "${OUTPUT_DIR}/${bundle_file}" bundle_sha)
    string(APPEND checksums "${bundle_sha}  ${bundle_file}\n")
endforeach()
file(WRITE "${OUTPUT_DIR}/SHA256SUMS.txt" "${checksums}")

message(STATUS "Experimental, unsigned GooseBoot firmware bundle staged in ${OUTPUT_DIR}")
