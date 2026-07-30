cmake_minimum_required(VERSION 3.20)

project(GooseRot VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GOOSEROT_BUILD_TESTS "Build the deterministic core tests" ON)
option(GOOSEROT_BUILD_BOOT_PREVIEW "Build the safe AURA 67 portable core and Win32 preview" ON)
option(GOOSEROT_BUILD_BOOT_FIRMWARE
  "Build the experimental, non-installing GooseBoot UEFI/BIOS artifacts" OFF)

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
configure_file(src/resources.rc.in "${GOOSEROT_GENERATED_DIR}/resources.rc" @ONLY)

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

add_executable(GooseRot WIN32
  src/main.cpp
  src/app.cpp
  src/app.hpp
  src/desktop_director.cpp
  src/desktop_director.hpp
  src/overlay_window.cpp
  src/overlay_window.hpp
  src/recovery_watchdog.cpp
  src/recovery_watchdog.hpp
  src/companion_windows.cpp
  src/companion_windows.hpp
  src/resource.h
  "${GOOSEROT_GENERATED_DIR}/resources.rc"
)
target_link_libraries(GooseRot PRIVATE
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
  set_property(TARGET GooseRot PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_property(TARGET gooserot_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(gooserot_core PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(GooseRot PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(GooseRot PRIVATE $<$<CONFIG:Release>:/O2>)
  target_link_options(GooseRot PRIVATE /SUBSYSTEM:WINDOWS,6.01)
else()
  target_compile_options(gooserot_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(GooseRot PRIVATE -Wall -Wextra -Wpedantic)
  target_link_options(GooseRot PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

set_target_properties(GooseRot PROPERTIES OUTPUT_NAME "GooseRot")
gooserot_set_runtime_output(GooseRot)

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
    src/companion_windows.cpp
    src/companion_windows.hpp
    src/desktop_director.cpp
    src/desktop_director.hpp
    src/recovery_watchdog.cpp
    src/recovery_watchdog.hpp
  )
  target_link_libraries(gooserot_win32_tests PRIVATE gooserot_core user32 gdi32)
  if(MSVC)
    set_property(TARGET gooserot_win32_tests PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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

set(GOOSEROT_PACKAGE_DEPENDS GooseRot)
set(GOOSEROT_PACKAGE_ARGUMENTS "-DGOOSEROT_EXE=$<TARGET_FILE:GooseRot>")
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
