foreach(required_variable
        OBJDUMP UEFI_IMAGE BIOS_STAGE1 BIOS_STAGE2 BIOS_IMAGE BIOS_STAGE2_PE)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

foreach(required_file
        "${OBJDUMP}" "${UEFI_IMAGE}" "${BIOS_STAGE1}" "${BIOS_STAGE2}"
        "${BIOS_IMAGE}" "${BIOS_STAGE2_PE}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing firmware verification input: ${required_file}")
    endif()
endforeach()

file(SIZE "${UEFI_IMAGE}" uefi_size)
if(uefi_size LESS 4096 OR uefi_size GREATER 262144)
    message(FATAL_ERROR "Unexpected UEFI image size: ${uefi_size}")
endif()
file(READ "${UEFI_IMAGE}" uefi_signature OFFSET 0 LIMIT 2 HEX)
string(TOLOWER "${uefi_signature}" uefi_signature)
if(NOT uefi_signature STREQUAL "4d5a")
    message(FATAL_ERROR "GooseBootX64.efi is not a PE image")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -f "${UEFI_IMAGE}"
    RESULT_VARIABLE uefi_file_result
    OUTPUT_VARIABLE uefi_file_headers
    ERROR_VARIABLE uefi_file_error)
if(NOT uefi_file_result EQUAL 0)
    message(FATAL_ERROR "objdump -f failed for UEFI: ${uefi_file_error}")
endif()
if(NOT uefi_file_headers MATCHES "file format pei-x86-64" OR
   NOT uefi_file_headers MATCHES "architecture: i386:x86-64" OR
   uefi_file_headers MATCHES "start address 0x0+([\r\n]|$)")
    message(FATAL_ERROR "GooseBootX64.efi has an unexpected architecture or entry point")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -p "${UEFI_IMAGE}"
    RESULT_VARIABLE uefi_headers_result
    OUTPUT_VARIABLE uefi_headers
    ERROR_VARIABLE uefi_headers_error)
if(NOT uefi_headers_result EQUAL 0)
    message(FATAL_ERROR "objdump -p failed for UEFI: ${uefi_headers_error}")
endif()
if(NOT uefi_headers MATCHES "Magic[ \t]+020b")
    message(FATAL_ERROR "GooseBootX64.efi is not PE32+")
endif()
if(NOT uefi_headers MATCHES "Subsystem[ \t]+0000000a")
    message(FATAL_ERROR "GooseBootX64.efi is not an EFI application")
endif()
if(uefi_headers MATCHES "DLL Name:")
    message(FATAL_ERROR "Hosted DLL import found in the freestanding UEFI image")
endif()
if(NOT uefi_headers MATCHES "AddressOfEntryPoint[ \t]+0*[^0 \t\r\n][0-9A-Fa-f]*")
    message(FATAL_ERROR "GooseBootX64.efi has a zero PE entry point")
endif()
if(NOT uefi_headers MATCHES "SizeOfUninitializedData[ \t]+0*[^0 \t\r\n][0-9A-Fa-f]*")
    message(FATAL_ERROR "GooseBootX64.efi has no uninitialized-data allocation")
endif()
if(NOT uefi_headers MATCHES "DIR64")
    message(FATAL_ERROR "UEFI relocation table contains no x86-64 DIR64 relocation")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -h "${UEFI_IMAGE}"
    RESULT_VARIABLE uefi_sections_result
    OUTPUT_VARIABLE uefi_sections
    ERROR_VARIABLE uefi_sections_error)
if(NOT uefi_sections_result EQUAL 0)
    message(FATAL_ERROR "objdump -h failed for UEFI: ${uefi_sections_error}")
endif()
string(REGEX MATCH "\\.reloc[ \t]+([0-9A-Fa-f]+)" reloc_match "${uefi_sections}")
set(reloc_size "${CMAKE_MATCH_1}")
if(reloc_match STREQUAL "" OR reloc_size MATCHES "^0+$")
    message(FATAL_ERROR "UEFI image has no usable base relocation section")
endif()
string(REGEX MATCH "\\.bss[ \t]+([0-9A-Fa-f]+)" bss_match "${uefi_sections}")
set(bss_size "${CMAKE_MATCH_1}")
if(bss_match STREQUAL "" OR bss_size MATCHES "^0+$")
    message(FATAL_ERROR "UEFI image has no uninitialized framebuffer storage")
endif()
math(EXPR bss_size_decimal "0x${bss_size}")
if(bss_size_decimal LESS 921600)
    message(FATAL_ERROR "UEFI BSS is too small for the 640x360 framebuffer: ${bss_size}")
endif()
string(REGEX MATCH "\\.bss[^\r\n]*[\r\n]+[^\r\n]*" bss_block "${uefi_sections}")
if(NOT bss_block MATCHES "ALLOC" OR bss_block MATCHES "CONTENTS")
    message(FATAL_ERROR "UEFI framebuffer BSS must be allocated but absent from file contents")
endif()

file(SIZE "${BIOS_STAGE1}" stage1_size)
file(SIZE "${BIOS_STAGE2}" stage2_size)
file(SIZE "${BIOS_IMAGE}" bios_image_size)
if(NOT stage1_size EQUAL 512)
    message(FATAL_ERROR "BIOS stage 1 must be exactly 512 bytes, got ${stage1_size}")
endif()
if(NOT stage2_size EQUAL 65024)
    message(FATAL_ERROR "BIOS stage 2 must be exactly 127 sectors, got ${stage2_size}")
endif()
if(NOT bios_image_size EQUAL 65536)
    message(FATAL_ERROR "BIOS image must be exactly 128 sectors, got ${bios_image_size}")
endif()

file(READ "${BIOS_STAGE1}" stage1_signature OFFSET 510 LIMIT 2 HEX)
file(READ "${BIOS_IMAGE}" image_signature OFFSET 510 LIMIT 2 HEX)
string(TOLOWER "${stage1_signature}" stage1_signature)
string(TOLOWER "${image_signature}" image_signature)
if(NOT stage1_signature STREQUAL "55aa" OR NOT image_signature STREQUAL "55aa")
    message(FATAL_ERROR "BIOS boot signature is missing")
endif()

file(READ "${BIOS_STAGE1}" stage1_hex HEX)
file(READ "${BIOS_IMAGE}" image_stage1_hex OFFSET 0 LIMIT 512 HEX)
if(NOT stage1_hex STREQUAL image_stage1_hex)
    message(FATAL_ERROR "BIOS image does not begin with the verified stage 1")
endif()
file(READ "${BIOS_STAGE2}" stage2_hex HEX)
file(READ "${BIOS_IMAGE}" image_stage2_hex OFFSET 512 LIMIT 65024 HEX)
if(NOT stage2_hex STREQUAL image_stage2_hex)
    message(FATAL_ERROR "BIOS image does not contain the verified stage 2")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -f "${BIOS_STAGE2_PE}"
    RESULT_VARIABLE bios_headers_result
    OUTPUT_VARIABLE bios_headers
    ERROR_VARIABLE bios_headers_error)
if(NOT bios_headers_result EQUAL 0)
    message(FATAL_ERROR "objdump -f failed for BIOS stage 2: ${bios_headers_error}")
endif()
if(NOT bios_headers MATCHES "file format pei-i386" OR
   NOT bios_headers MATCHES "start address 0x00010000")
    message(FATAL_ERROR "BIOS stage 2 has an unexpected architecture or entry point")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -h "${BIOS_STAGE2_PE}"
    RESULT_VARIABLE bios_sections_result
    OUTPUT_VARIABLE bios_sections
    ERROR_VARIABLE bios_sections_error)
if(NOT bios_sections_result EQUAL 0)
    message(FATAL_ERROR "objdump -h failed for BIOS stage 2: ${bios_sections_error}")
endif()
if(NOT bios_sections MATCHES "\\.text[ \t]+[0-9A-Fa-f]+[ \t]+00010000")
    message(FATAL_ERROR "BIOS stage 2 does not begin at linear address 0x10000")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -t "${BIOS_STAGE2_PE}"
    RESULT_VARIABLE bios_symbols_result
    OUTPUT_VARIABLE bios_symbols
    ERROR_VARIABLE bios_symbols_error)
if(NOT bios_symbols_result EQUAL 0)
    message(FATAL_ERROR "objdump -t failed for BIOS stage 2: ${bios_symbols_error}")
endif()
foreach(real_mode_symbol
        _gdt_descriptor _memory_error_message _bios_boot_info
        _mode_list_offset _vbe_controller_info _vbe_mode_info)
    string(REGEX MATCH
        "0x([0-9A-Fa-f]+)[ \t]+${real_mode_symbol}([\r\n]|$)"
        symbol_match "${bios_symbols}")
    set(symbol_offset "${CMAKE_MATCH_1}")
    if(symbol_match STREQUAL "")
        message(FATAL_ERROR "Missing BIOS real-mode symbol: ${real_mode_symbol}")
    endif()
    math(EXPR symbol_offset_decimal "0x${symbol_offset}")
    if(symbol_offset_decimal GREATER_EQUAL 65024)
        message(FATAL_ERROR
            "BIOS real-mode symbol exceeds the 64 KiB CS-relative window: ${real_mode_symbol}")
    endif()
endforeach()

string(REGEX MATCH "0x([0-9A-Fa-f]+)[ \t]+_vbe_mode_info([\r\n]|$)"
    mode_info_match "${bios_symbols}")
math(EXPR mode_info_end "0x${CMAKE_MATCH_1} + 256")
if(mode_info_end GREATER 65024)
    message(FATAL_ERROR "BIOS VBE scratch buffer crosses the real-mode segment boundary")
endif()

message(STATUS
    "Firmware verified: UEFI=${uefi_size} bytes x64/DIR64, BIOS=512+65024 bytes, no UEFI DLL imports")
