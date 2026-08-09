cmake_minimum_required(VERSION 3.20)

project(GooseRot VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GOOSEROT_BUILD_TESTS "Build the deterministic core tests" ON)
option(GOOSEROT_BUILD_BOOT_PREVIEW "Build the safe AURA 67 portable core and Win32 preview" ON)
option(GOOSEROT_BUILD_BOOT_FIRMWARE
  "Build the experimental, non-installing GooseBoot UEFI/BIOS artifacts" OFF)
set(GOOSEROT_LAB_FIRMWARE_DIR "" CACHE PATH
  "Directory containing prebuilt GooseBoot firmware to embed in GooseRot-Lab")

set(GOOSEROT_RUNTIME_DIR "${CMAKE_BINARY_DIR}/bin")
function(gooserot_set_runtime_output target_name)
  if(CMAKE_CONFIGURATION_TYPES)
    foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
      string(TOUPPER "${configuration}" configuration_upper)
      set_property(TARGET ${target_name} PROPERTY
        "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
        "${GOOSEROT_RUNTIME_DIR}/${configuration}")
    endforeach()
  else()
    set_target_properties(${target_name} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${GOOSEROT_RUNTIME_DIR}")
  endif()
endfunction()

if(NOT WIN32)
  message(FATAL_ERROR "GooseRot is a native Win32 application and must be built on Windows.")
endif()

set(GOOSEROT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GOOSEROT_GENERATED_DIR}")

set(GOOSEROT_RESOURCE_INPUTS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/resource.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Generated/Brainrot/tralalero-tralala.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Generated/Brainrot/ballerina-cappuccina.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Generated/Brainrot/bombardiro-crocodilo.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Generated/Reactions/cat-shocked-orange.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Generated/Reactions/cat-judgmental-gray.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Generated/Reactions/cat-vacant-white.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Generated/Reactions/cat-suspicious-black.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/User/GooseChaos/goose-ice-cream.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/User/GooseChaos/goose-worker.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/User/GooseChaos/goose-call.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/User/GooseChaos/goose-store-brainrot.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/User/GooseChaos/punchy-goose-facts.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/User/GooseChaos/goose-gangster.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/User/GooseChaos/goose-jet.png"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GOOSEROT_RESOURCE_INPUTS})
set(GOOSEROT_RESOURCE_FINGERPRINT_SOURCE "")
foreach(resource_input IN LISTS GOOSEROT_RESOURCE_INPUTS)
  file(SHA256 "${resource_input}" resource_hash)
  string(APPEND GOOSEROT_RESOURCE_FINGERPRINT_SOURCE "${resource_hash}")
endforeach()
string(SHA256 GOOSEROT_RESOURCE_FINGERPRINT "${GOOSEROT_RESOURCE_FINGERPRINT_SOURCE}")
configure_file(src/resources.rc.in "${GOOSEROT_GENERATED_DIR}/resources.rc" @ONLY)
set_property(SOURCE "${GOOSEROT_GENERATED_DIR}/resources.rc" APPEND PROPERTY
  OBJECT_DEPENDS ${GOOSEROT_RESOURCE_INPUTS})

set(GOOSEROT_STANDARD_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/src/gooserot.manifest")
set(GOOSEROT_LAB_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/src/gooserot_lab.manifest")
set(GOOSEROT_MANIFEST_PATH "${GOOSEROT_STANDARD_MANIFEST}")
configure_file(src/application_manifest.rc.in
               "${GOOSEROT_GENERATED_DIR}/standard_manifest.rc" @ONLY)
set(GOOSEROT_MANIFEST_PATH "${GOOSEROT_LAB_MANIFEST}")
configure_file(src/application_manifest.rc.in
               "${GOOSEROT_GENERATED_DIR}/lab_manifest.rc" @ONLY)
set_property(SOURCE "${GOOSEROT_GENERATED_DIR}/standard_manifest.rc" APPEND PROPERTY
             OBJECT_DEPENDS "${GOOSEROT_STANDARD_MANIFEST}")
set_property(SOURCE "${GOOSEROT_GENERATED_DIR}/lab_manifest.rc" APPEND PROPERTY
             OBJECT_DEPENDS "${GOOSEROT_LAB_MANIFEST}")

add_library(gooserot_core STATIC
  src/core.cpp
  src/core.hpp
)
target_include_directories(gooserot_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(gooserot_core PUBLIC
  UNICODE
  _UNICODE
  WINVER=0x0601
  _WIN32_WINNT=0x0601
  NOMINMAX
  WIN32_LEAN_AND_MEAN
)

set(GOOSEROT_APP_SOURCES
  src/main.cpp
  src/app.cpp
  src/app.hpp
  src/desktop_director.cpp
  src/desktop_director.hpp
  src/overlay_window.cpp
  src/overlay_window.hpp
  src/shell_surface.cpp
  src/shell_surface.hpp
  src/recovery_watchdog.cpp
  src/recovery_watchdog.hpp
  src/companion_windows.cpp
  src/companion_windows.hpp
  src/resource.h
  "${GOOSEROT_GENERATED_DIR}/resources.rc"
)

function(gooserot_add_profile target_name output_name profile_id)
  if("${profile_id}" STREQUAL "2")
    set(profile_manifest_resource "${GOOSEROT_GENERATED_DIR}/lab_manifest.rc")
  else()
    set(profile_manifest_resource "${GOOSEROT_GENERATED_DIR}/standard_manifest.rc")
  endif()
  add_executable(${target_name} WIN32 ${GOOSEROT_APP_SOURCES}
                 "${profile_manifest_resource}")
  target_compile_definitions(${target_name} PRIVATE GOOSEROT_BUILD_PROFILE=${profile_id})
  if("${profile_id}" STREQUAL "2")
    target_compile_definitions(${target_name} PRIVATE GOOSEROT_BUILD_DEBUG_CONSOLE=1)
  endif()
  target_link_libraries(${target_name} PRIVATE
    gooserot_core
    gdiplus
    gdi32
    user32
    shell32
    ole32
    comctl32
    winmm
  )
  if(MSVC)
    set_property(TARGET ${target_name} PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET ${target_name} PROPERTY MSVC_MANIFEST_EMBED OFF)
    target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(${target_name} PRIVATE $<$<CONFIG:Release>:/O2>)
    target_link_options(${target_name} PRIVATE /SUBSYSTEM:WINDOWS,6.01)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(${target_name} PRIVATE -municode -static -static-libgcc -static-libstdc++)
  endif()
  set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "${output_name}")
  gooserot_set_runtime_output(${target_name})
endfunction()

gooserot_add_profile(GooseRotSafe "GooseRot-Safe" 0)
gooserot_add_profile(GooseRotNormal "GooseRot-Normal" 1)
gooserot_add_profile(GooseRotLab "GooseRot-Lab" 2)
add_executable(GooseRotLabDebug WIN32
  ${GOOSEROT_APP_SOURCES}
  "${GOOSEROT_GENERATED_DIR}/lab_manifest.rc"
  src/lab_mode.cpp
  src/lab_mode.hpp
)
target_compile_definitions(GooseRotLabDebug PRIVATE GOOSEROT_BUILD_PROFILE=2 GOOSEROT_BUILD_DEBUG_CONSOLE=1)
target_link_libraries(GooseRotLabDebug PRIVATE
  gooserot_core
  gdiplus
  gdi32
  user32
  shell32
  ole32
  comctl32
  winmm
)
if(MSVC)
  set_property(TARGET GooseRotLabDebug PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_property(TARGET GooseRotLabDebug PROPERTY MSVC_MANIFEST_EMBED OFF)
  target_compile_options(GooseRotLabDebug PRIVATE /W4 /permissive- /utf-8)
  target_link_options(GooseRotLabDebug PRIVATE /SUBSYSTEM:WINDOWS,6.01)
else()
  target_compile_options(GooseRotLabDebug PRIVATE -Wall -Wextra -Wpedantic)
  target_link_options(GooseRotLabDebug PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(GooseRotLabDebug PROPERTIES OUTPUT_NAME "GooseRot-Lab-Debug")
gooserot_set_runtime_output(GooseRotLabDebug)
add_dependencies(GooseRotLab GooseRotLabDebug)
target_sources(GooseRotLab PRIVATE
  src/lab_mode.cpp
  src/lab_mode.hpp
)

if(MSVC)
  set_property(TARGET gooserot_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(gooserot_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(gooserot_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING AND GOOSEROT_BUILD_TESTS)
  add_executable(gooserot_tests tests/core_tests.cpp)
  target_link_libraries(gooserot_tests PRIVATE gooserot_core)
  if(MSVC)
    set_property(TARGET gooserot_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(gooserot_tests PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(gooserot_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(gooserot_tests PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME gooserot_core COMMAND gooserot_tests)

  add_executable(gooserot_win32_tests
    tests/win32_integration_tests.cpp
    src/overlay_window.cpp
    src/overlay_window.hpp
    src/companion_windows.cpp
    src/companion_windows.hpp
    src/desktop_director.cpp
    src/desktop_director.hpp
    src/recovery_watchdog.cpp
    src/recovery_watchdog.hpp
    src/shell_surface.cpp
    src/shell_surface.hpp
    "${GOOSEROT_GENERATED_DIR}/resources.rc"
  )
  target_link_libraries(gooserot_win32_tests PRIVATE
    gooserot_core
    gdiplus
    gdi32
    user32
    ole32
  )
  if(MSVC)
    set_property(TARGET gooserot_win32_tests PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET gooserot_win32_tests PROPERTY MSVC_MANIFEST_EMBED OFF)
    target_compile_options(gooserot_win32_tests PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(gooserot_win32_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(gooserot_win32_tests PRIVATE -municode -static -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME gooserot_win32_integration COMMAND gooserot_win32_tests)
  set_tests_properties(gooserot_win32_integration PROPERTIES TIMEOUT 20)
endif()

if(GOOSEROT_BUILD_BOOT_PREVIEW OR GOOSEROT_BUILD_BOOT_FIRMWARE)
  set(GOOSEBOOT_BUILD_PREVIEW ${GOOSEROT_BUILD_BOOT_PREVIEW}
      CACHE BOOL "Build GooseBoot's safe Win32 preview" FORCE)
  set(GOOSEBOOT_BUILD_TESTS ${GOOSEROT_BUILD_TESTS} CACHE BOOL "Build GooseBoot core tests" FORCE)
  set(GOOSEBOOT_BUILD_FIRMWARE ${GOOSEROT_BUILD_BOOT_FIRMWARE}
      CACHE BOOL "Build experimental GooseBoot firmware artifacts" FORCE)
  add_subdirectory(boot)
endif()
if(TARGET gooseboot_firmware)
  add_dependencies(GooseRotLab gooseboot_firmware)
  add_dependencies(GooseRotLabDebug gooseboot_firmware)
  set(GOOSEROT_LAB_UEFI_IMAGE
      "${CMAKE_BINARY_DIR}/boot/firmware/GooseBootX64.efi")
  set(GOOSEROT_LAB_BIOS_STAGE1
      "${CMAKE_BINARY_DIR}/boot/firmware/gooseboot-bios-stage1.bin")
  set(GOOSEROT_LAB_BIOS_STAGE2
      "${CMAKE_BINARY_DIR}/boot/firmware/gooseboot-bios-stage2.bin")
elseif(NOT GOOSEROT_LAB_FIRMWARE_DIR STREQUAL "")
  get_filename_component(GOOSEROT_LAB_FIRMWARE_DIR
                         "${GOOSEROT_LAB_FIRMWARE_DIR}" ABSOLUTE)
  set(GOOSEROT_LAB_UEFI_IMAGE
      "${GOOSEROT_LAB_FIRMWARE_DIR}/GooseBootX64.efi")
  set(GOOSEROT_LAB_BIOS_STAGE1
      "${GOOSEROT_LAB_FIRMWARE_DIR}/gooseboot-bios-stage1.bin")
  set(GOOSEROT_LAB_BIOS_STAGE2
      "${GOOSEROT_LAB_FIRMWARE_DIR}/gooseboot-bios-stage2.bin")
  foreach(firmware_input IN ITEMS
          "${GOOSEROT_LAB_UEFI_IMAGE}"
          "${GOOSEROT_LAB_BIOS_STAGE1}"
          "${GOOSEROT_LAB_BIOS_STAGE2}")
    if(NOT EXISTS "${firmware_input}")
      message(FATAL_ERROR "Missing Lab firmware input: ${firmware_input}")
    endif()
  endforeach()
endif()

if(DEFINED GOOSEROT_LAB_UEFI_IMAGE)
  set(GOOSEROT_LAB_FIRMWARE_RESOURCE
      "${GOOSEROT_GENERATED_DIR}/lab_firmware.rc")
  set(GOOSEROT_LAB_FIRMWARE_INPUTS
      "${GOOSEROT_LAB_UEFI_IMAGE}"
      "${GOOSEROT_LAB_BIOS_STAGE1}"
      "${GOOSEROT_LAB_BIOS_STAGE2}")
  configure_file(src/lab_firmware.rc.in
                 "${GOOSEROT_LAB_FIRMWARE_RESOURCE}" @ONLY)
  set_property(SOURCE "${GOOSEROT_LAB_FIRMWARE_RESOURCE}" APPEND PROPERTY
               OBJECT_DEPENDS ${GOOSEROT_LAB_FIRMWARE_INPUTS})
  target_sources(GooseRotLab PRIVATE "${GOOSEROT_LAB_FIRMWARE_RESOURCE}")
  target_sources(GooseRotLabDebug PRIVATE "${GOOSEROT_LAB_FIRMWARE_RESOURCE}")
endif()

set(GOOSEROT_PACKAGE_DEPENDS GooseRotSafe GooseRotNormal GooseRotLab GooseRotLabDebug)
set(GOOSEROT_PACKAGE_ARGUMENTS
    "-DGOOSEROT_SAFE_EXE=$<TARGET_FILE:GooseRotSafe>"
    "-DGOOSEROT_NORMAL_EXE=$<TARGET_FILE:GooseRotNormal>"
  "-DGOOSEROT_LAB_EXE=$<TARGET_FILE:GooseRotLab>"
  "-DGOOSEROT_LAB_DEBUG_EXE=$<TARGET_FILE:GooseRotLabDebug>")
if(TARGET GooseBootPreview)
  list(APPEND GOOSEROT_PACKAGE_DEPENDS GooseBootPreview)
  list(APPEND GOOSEROT_PACKAGE_ARGUMENTS
       "-DGOOSEBOOT_PREVIEW=$<TARGET_FILE:GooseBootPreview>")
endif()
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(GOOSEROT_PACKAGE_TARGET gooserot_release)
  set(GOOSEROT_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
  set(GOOSEROT_REQUIRE_RELEASE ON)
  set(GOOSEROT_PACKAGE_COMMENT "Staging the Win32 MSVC /MT GooseRot release")
else()
  set(GOOSEROT_PACKAGE_TARGET gooserot_smoke_bundle)
  set(GOOSEROT_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/smoke-dist")
  set(GOOSEROT_REQUIRE_RELEASE OFF)
  set(GOOSEROT_PACKAGE_COMMENT "Staging a non-release GooseRot smoke bundle")
endif()
add_custom_target(${GOOSEROT_PACKAGE_TARGET}
  COMMAND "${CMAKE_COMMAND}"
          ${GOOSEROT_PACKAGE_ARGUMENTS}
          "-DREADME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/README.md"
          "-DOUTPUT_DIR=${GOOSEROT_PACKAGE_DIRECTORY}"
          "-DBUILD_CONFIGURATION=$<CONFIG>"
          "-DREQUIRE_RELEASE=${GOOSEROT_REQUIRE_RELEASE}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_release.cmake"
  DEPENDS ${GOOSEROT_PACKAGE_DEPENDS}
  COMMENT "${GOOSEROT_PACKAGE_COMMENT}"
  COMMAND_EXPAND_LISTS
  VERBATIM
)
