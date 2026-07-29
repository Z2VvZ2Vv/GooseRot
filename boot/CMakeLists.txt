cmake_minimum_required(VERSION 3.20)

project(GooseBootCore VERSION 0.1.0 LANGUAGES CXX)

option(GOOSEBOOT_BUILD_PREVIEW "Build the safe Win32 preview" ON)
option(GOOSEBOOT_BUILD_TESTS "Build the deterministic core tests" ON)
option(GOOSEBOOT_BUILD_FIRMWARE
       "Build experimental UEFI x64 and BIOS artifacts without an installer" OFF)

add_library(gooseboot_core STATIC
    game/game.cpp
    game/renderer.cpp
)

target_include_directories(gooseboot_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gooseboot_core PUBLIC cxx_std_17)
set_target_properties(gooseboot_core PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    set_property(TARGET gooseboot_core PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(gooseboot_core PRIVATE /W4 /permissive- /GR- /EHs-c-)
    target_compile_definitions(gooseboot_core PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(gooseboot_core PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -fno-exceptions -fno-rtti
    )
endif()

if(GOOSEBOOT_BUILD_TESTS)
    enable_testing()
    add_executable(gooseboot_tests tests/game_tests.cpp)
    target_link_libraries(gooseboot_tests PRIVATE gooseboot_core)
    target_compile_features(gooseboot_tests PRIVATE cxx_std_17)
    set_target_properties(gooseboot_tests PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        set_property(TARGET gooseboot_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(gooseboot_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(gooseboot_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gooseboot_core_tests COMMAND gooseboot_tests)
endif()

if(GOOSEBOOT_BUILD_PREVIEW)
    if(WIN32)
        add_executable(GooseBootPreview WIN32 platform/preview/win32_preview.cpp)
        target_link_libraries(GooseBootPreview PRIVATE gooseboot_core gdi32 user32)
        target_compile_features(GooseBootPreview PRIVATE cxx_std_17)
        target_compile_definitions(GooseBootPreview PRIVATE
            UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
            WINVER=0x0601 _WIN32_WINNT=0x0601
        )
        set_target_properties(GooseBootPreview PROPERTIES CXX_EXTENSIONS OFF)
        if(COMMAND gooserot_set_runtime_output)
            gooserot_set_runtime_output(GooseBootPreview)
        else()
            set_target_properties(GooseBootPreview PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
        endif()
        if(MSVC)
            set_property(TARGET GooseBootPreview PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            target_compile_options(GooseBootPreview PRIVATE /W4 /permissive-)
        else()
            target_compile_options(GooseBootPreview PRIVATE -Wall -Wextra -Wpedantic)
            target_link_options(GooseBootPreview PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    else()
        message(STATUS "GooseBootPreview is Win32-only; the portable core and tests remain available")
    endif()
endif()

if(GOOSEBOOT_BUILD_FIRMWARE)
    if(NOT MINGW OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
       NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR
            "Experimental GooseBoot firmware currently requires 64-bit MinGW GNU binutils")
    endif()

    enable_language(ASM)
    get_filename_component(GOOSEBOOT_TOOLCHAIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(GOOSEBOOT_LINKER NAMES ld
        PATHS "${GOOSEBOOT_TOOLCHAIN_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_program(GOOSEBOOT_OBJCOPY NAMES objcopy
        PATHS "${GOOSEBOOT_TOOLCHAIN_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_program(GOOSEBOOT_OBJDUMP NAMES objdump
        PATHS "${GOOSEBOOT_TOOLCHAIN_DIR}" NO_DEFAULT_PATH REQUIRED)

    set(GOOSEBOOT_FIRMWARE_DIR "${CMAKE_CURRENT_BINARY_DIR}/firmware")
    set(GOOSEBOOT_INTERMEDIATE_DIR "${GOOSEBOOT_FIRMWARE_DIR}/intermediate")
    file(MAKE_DIRECTORY "${GOOSEBOOT_FIRMWARE_DIR}" "${GOOSEBOOT_INTERMEDIATE_DIR}")

    set(GOOSEBOOT_UEFI_IMAGE "${GOOSEBOOT_FIRMWARE_DIR}/GooseBootX64.efi")
    set(GOOSEBOOT_BIOS_STAGE1 "${GOOSEBOOT_FIRMWARE_DIR}/gooseboot-bios-stage1.bin")
    set(GOOSEBOOT_BIOS_STAGE2 "${GOOSEBOOT_FIRMWARE_DIR}/gooseboot-bios-stage2.bin")
    set(GOOSEBOOT_BIOS_IMAGE "${GOOSEBOOT_FIRMWARE_DIR}/gooseboot-bios.img")
    set(GOOSEBOOT_BIOS_STAGE1_PE "${GOOSEBOOT_INTERMEDIATE_DIR}/stage1.pe")
    set(GOOSEBOOT_BIOS_STAGE2_PE "${GOOSEBOOT_INTERMEDIATE_DIR}/stage2.pe")

    add_library(gooseboot_uefi_objects OBJECT
        game/game.cpp
        game/renderer.cpp
        platform/uefi/efi_main.cpp
        platform/uefi/uefi_clock.cpp
        platform/uefi/uefi_graphics.cpp
        platform/uefi/uefi_input.cpp
        platform/uefi/uefi_crt.cpp
    )
    target_include_directories(gooseboot_uefi_objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(gooseboot_uefi_objects PRIVATE cxx_std_17)
    set_target_properties(gooseboot_uefi_objects PROPERTIES CXX_EXTENSIONS OFF)
    # Do not add -fdata-sections here.  MinGW's default PE linker script turns
    # orphan .bss.* sections into file-backed data, inflating the EFI by ~900 KiB.
    target_compile_options(gooseboot_uefi_objects PRIVATE
        -O2 -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident
        -mno-red-zone -fshort-wchar
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )

    add_custom_command(
        OUTPUT "${GOOSEBOOT_UEFI_IMAGE}"
        COMMAND "${GOOSEBOOT_LINKER}"
            -mi386pep --subsystem 10 --entry efi_main --image-base 0
            --enable-reloc-section --file-alignment 512 --section-alignment 4096
            --no-insert-timestamp --disable-runtime-pseudo-reloc --no-undefined
            --strip-all -o "${GOOSEBOOT_UEFI_IMAGE}"
            $<TARGET_OBJECTS:gooseboot_uefi_objects>
        DEPENDS gooseboot_uefi_objects $<TARGET_OBJECTS:gooseboot_uefi_objects>
        COMMENT "Linking the freestanding GooseBoot x64 UEFI application"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_library(gooseboot_bios_stage1_object OBJECT platform/bios/stage1.S)
    target_compile_options(gooseboot_bios_stage1_object PRIVATE
        -m32 -ffreestanding -fno-pic -fno-pie)

    add_library(gooseboot_bios_entry_object OBJECT platform/bios/stage2_entry.S)
    target_compile_options(gooseboot_bios_entry_object PRIVATE
        -m32 -ffreestanding -fno-pic -fno-pie)

    add_library(gooseboot_bios_objects OBJECT
        game/game.cpp
        game/renderer.cpp
        platform/bios/bios_main.cpp
        platform/bios/bios_graphics.cpp
        platform/bios/bios_input.cpp
        platform/bios/bios_clock.cpp
    )
    target_include_directories(gooseboot_bios_objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(gooseboot_bios_objects PRIVATE cxx_std_17)
    set_target_properties(gooseboot_bios_objects PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(gooseboot_bios_objects PRIVATE
        -m32 -Os -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident
        -fno-pic -fno-pie -ffunction-sections -fdata-sections
        -mno-sse -mno-sse2 -mno-mmx -msoft-float
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )

    add_custom_command(
        OUTPUT "${GOOSEBOOT_BIOS_STAGE1}"
        BYPRODUCTS "${GOOSEBOOT_BIOS_STAGE1_PE}"
        COMMAND "${GOOSEBOOT_LINKER}"
            -mi386pe --image-base 0 --entry _stage1_start
            --no-insert-timestamp --disable-runtime-pseudo-reloc --no-undefined
            -T "${CMAKE_CURRENT_SOURCE_DIR}/platform/bios/stage1.ld"
            -o "${GOOSEBOOT_BIOS_STAGE1_PE}"
            $<TARGET_OBJECTS:gooseboot_bios_stage1_object>
        COMMAND "${GOOSEBOOT_OBJCOPY}" -O binary -j .boot
            "${GOOSEBOOT_BIOS_STAGE1_PE}" "${GOOSEBOOT_BIOS_STAGE1}"
        DEPENDS gooseboot_bios_stage1_object
                $<TARGET_OBJECTS:gooseboot_bios_stage1_object>
                platform/bios/stage1.ld
        COMMENT "Building the read-only 512-byte GooseBoot BIOS stage 1"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${GOOSEBOOT_BIOS_STAGE2}"
        BYPRODUCTS "${GOOSEBOOT_BIOS_STAGE2_PE}"
        COMMAND "${GOOSEBOOT_LINKER}"
            -mi386pe --image-base 0 --entry _stage2_start
            --no-insert-timestamp --disable-runtime-pseudo-reloc --no-undefined
            -T "${CMAKE_CURRENT_SOURCE_DIR}/platform/bios/linker.ld"
            -o "${GOOSEBOOT_BIOS_STAGE2_PE}"
            $<TARGET_OBJECTS:gooseboot_bios_entry_object>
            $<TARGET_OBJECTS:gooseboot_bios_objects>
        COMMAND "${GOOSEBOOT_OBJCOPY}" -O binary --gap-fill 0 --pad-to 0x1fe00
            "${GOOSEBOOT_BIOS_STAGE2_PE}" "${GOOSEBOOT_BIOS_STAGE2}"
        DEPENDS gooseboot_bios_entry_object gooseboot_bios_objects
                $<TARGET_OBJECTS:gooseboot_bios_entry_object>
                $<TARGET_OBJECTS:gooseboot_bios_objects>
                platform/bios/linker.ld
        COMMENT "Building the freestanding GooseBoot BIOS stage 2"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_executable(gooseboot_image_builder tools/make_bios_image.cpp)
    target_compile_features(gooseboot_image_builder PRIVATE cxx_std_17)
    set_target_properties(gooseboot_image_builder PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(gooseboot_image_builder PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

    add_custom_command(
        OUTPUT "${GOOSEBOOT_BIOS_IMAGE}"
        COMMAND "$<TARGET_FILE:gooseboot_image_builder>"
            "${GOOSEBOOT_BIOS_STAGE1}" "${GOOSEBOOT_BIOS_STAGE2}"
            "${GOOSEBOOT_BIOS_IMAGE}"
        DEPENDS gooseboot_image_builder
                "${GOOSEBOOT_BIOS_STAGE1}" "${GOOSEBOOT_BIOS_STAGE2}"
        COMMENT "Assembling a build-directory-only GooseBoot BIOS image"
        VERBATIM
    )

    set(GOOSEBOOT_VERIFY_STAMP "${GOOSEBOOT_FIRMWARE_DIR}/verified.stamp")
    add_custom_command(
        OUTPUT "${GOOSEBOOT_VERIFY_STAMP}"
        COMMAND "${CMAKE_COMMAND}"
            "-DOBJDUMP=${GOOSEBOOT_OBJDUMP}"
            "-DUEFI_IMAGE=${GOOSEBOOT_UEFI_IMAGE}"
            "-DBIOS_STAGE1=${GOOSEBOOT_BIOS_STAGE1}"
            "-DBIOS_STAGE2=${GOOSEBOOT_BIOS_STAGE2}"
            "-DBIOS_IMAGE=${GOOSEBOOT_BIOS_IMAGE}"
            "-DBIOS_STAGE2_PE=${GOOSEBOOT_BIOS_STAGE2_PE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_firmware.cmake"
        COMMAND "${CMAKE_COMMAND}" -E touch "${GOOSEBOOT_VERIFY_STAMP}"
        DEPENDS "${GOOSEBOOT_UEFI_IMAGE}" "${GOOSEBOOT_BIOS_STAGE1}"
                "${GOOSEBOOT_BIOS_STAGE2}" "${GOOSEBOOT_BIOS_IMAGE}"
                "${GOOSEBOOT_BIOS_STAGE2_PE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_firmware.cmake"
        COMMENT "Verifying GooseBoot firmware layout and import boundaries"
        VERBATIM
    )

    add_custom_target(gooseboot_firmware ALL DEPENDS "${GOOSEBOOT_VERIFY_STAMP}")

    add_custom_target(gooseboot_firmware_bundle
        COMMAND "${CMAKE_COMMAND}"
            "-DUEFI_IMAGE=${GOOSEBOOT_UEFI_IMAGE}"
            "-DBIOS_STAGE1=${GOOSEBOOT_BIOS_STAGE1}"
            "-DBIOS_STAGE2=${GOOSEBOOT_BIOS_STAGE2}"
            "-DBIOS_IMAGE=${GOOSEBOOT_BIOS_IMAGE}"
            "-DREADME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/firmware-dist"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_firmware.cmake"
        DEPENDS gooseboot_firmware
        COMMENT "Staging an unsigned, non-installing experimental firmware bundle"
        VERBATIM
    )

    if(GOOSEBOOT_BUILD_TESTS)
        add_test(NAME gooseboot_firmware_layout
            COMMAND "${CMAKE_COMMAND}"
                "-DOBJDUMP=${GOOSEBOOT_OBJDUMP}"
                "-DUEFI_IMAGE=${GOOSEBOOT_UEFI_IMAGE}"
                "-DBIOS_STAGE1=${GOOSEBOOT_BIOS_STAGE1}"
                "-DBIOS_STAGE2=${GOOSEBOOT_BIOS_STAGE2}"
                "-DBIOS_IMAGE=${GOOSEBOOT_BIOS_IMAGE}"
                "-DBIOS_STAGE2_PE=${GOOSEBOOT_BIOS_STAGE2_PE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_firmware.cmake")
        set_tests_properties(gooseboot_firmware_layout PROPERTIES TIMEOUT 10)
    endif()
endif()
