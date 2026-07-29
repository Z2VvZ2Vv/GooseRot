#pragma once

#include <cstddef>
#include <cstdint>

// Minimal UEFI 2.x declarations for the x86-64 GooseBoot adapter.  Keeping the
// declarations local avoids a dependency on gnu-efi or EDK II headers.  Only
// protocols used by the adapter are typed; unused service-table slots remain
// opaque pointers so the ABI offsets still match the specification.

#if defined(_MSC_VER)
#define AURA67_EFIAPI __cdecl
#elif defined(__x86_64__) && !defined(_WIN32)
#define AURA67_EFIAPI __attribute__((ms_abi))
#else
#define AURA67_EFIAPI
#endif

namespace aura67::uefi {

static_assert(sizeof(void*) == 8U, "This adapter targets UEFI x86-64 only");

using Boolean = std::uint8_t;
using Char16 = std::uint16_t;
using UintN = std::uint64_t;
using Status = std::uint64_t;
using Handle = void*;
using Event = void*;
using Tpl = UintN;
using PhysicalAddress = std::uint64_t;

constexpr Status kErrorBit = 0x8000000000000000ULL;
constexpr Status efi_error_code(Status code) noexcept { return kErrorBit | code; }
constexpr bool efi_error(Status status) noexcept { return (status & kErrorBit) != 0U; }

constexpr Status kSuccess = 0U;
constexpr Status kInvalidParameter = efi_error_code(2U);
constexpr Status kUnsupported = efi_error_code(3U);
constexpr Status kNotReady = efi_error_code(6U);
constexpr Status kDeviceError = efi_error_code(7U);
constexpr Status kOutOfResources = efi_error_code(9U);

constexpr std::uint32_t kEventTimer = 0x80000000U;
constexpr Tpl kTplApplication = 4U;
constexpr std::uint64_t kHundredNanosecondsPerSecond = 10000000ULL;

struct Guid {
    std::uint32_t data1;
    std::uint16_t data2;
    std::uint16_t data3;
    std::uint8_t data4[8];
};

struct TableHeader {
    std::uint64_t signature;
    std::uint32_t revision;
    std::uint32_t header_size;
    std::uint32_t crc32;
    std::uint32_t reserved;
};

struct InputKey {
    std::uint16_t scan_code;
    Char16 unicode_char;
};

struct SimpleTextInputProtocol;
using InputReset = Status (AURA67_EFIAPI *)(SimpleTextInputProtocol*, Boolean);
using ReadKeyStroke = Status (AURA67_EFIAPI *)(SimpleTextInputProtocol*, InputKey*);

struct SimpleTextInputProtocol {
    InputReset reset;
    ReadKeyStroke read_key_stroke;
    Event wait_for_key;
};

enum class TimerDelay : std::uint32_t {
    Cancel,
    Periodic,
    Relative,
};

using EventNotify = void (AURA67_EFIAPI *)(Event, void*);
using CreateEvent = Status (AURA67_EFIAPI *)(std::uint32_t, Tpl, EventNotify, void*, Event*);
using SetTimer = Status (AURA67_EFIAPI *)(Event, TimerDelay, std::uint64_t);
using WaitForEvent = Status (AURA67_EFIAPI *)(UintN, Event*, UintN*);
using CloseEvent = Status (AURA67_EFIAPI *)(Event);
using LocateProtocol = Status (AURA67_EFIAPI *)(const Guid*, void*, void**);
using FreePool = Status (AURA67_EFIAPI *)(void*);
using SetWatchdogTimer = Status (AURA67_EFIAPI *)(UintN, std::uint64_t, UintN, Char16*);

// The ordering below is part of the UEFI ABI.  Do not remove opaque fields.
struct BootServices {
    TableHeader header;
    void* raise_tpl;
    void* restore_tpl;
    void* allocate_pages;
    void* free_pages;
    void* get_memory_map;
    void* allocate_pool;
    FreePool free_pool;
    CreateEvent create_event;
    SetTimer set_timer;
    WaitForEvent wait_for_event;
    void* signal_event;
    CloseEvent close_event;
    void* check_event;
    void* install_protocol_interface;
    void* reinstall_protocol_interface;
    void* uninstall_protocol_interface;
    void* handle_protocol;
    void* reserved;
    void* register_protocol_notify;
    void* locate_handle;
    void* locate_device_path;
    void* install_configuration_table;
    void* load_image;
    void* start_image;
    void* exit;
    void* unload_image;
    void* exit_boot_services;
    void* get_next_monotonic_count;
    void* stall;
    SetWatchdogTimer set_watchdog_timer;
    void* connect_controller;
    void* disconnect_controller;
    void* open_protocol;
    void* close_protocol;
    void* open_protocol_information;
    void* protocols_per_handle;
    void* locate_handle_buffer;
    LocateProtocol locate_protocol;
    void* install_multiple_protocol_interfaces;
    void* uninstall_multiple_protocol_interfaces;
    void* calculate_crc32;
    void* copy_mem;
    void* set_mem;
    void* create_event_ex;
};

static_assert(sizeof(TableHeader) == 24U);
static_assert(offsetof(BootServices, create_event) == 80U);
static_assert(offsetof(BootServices, free_pool) == 72U);
static_assert(offsetof(BootServices, set_timer) == 88U);
static_assert(offsetof(BootServices, wait_for_event) == 96U);
static_assert(offsetof(BootServices, close_event) == 112U);
static_assert(offsetof(BootServices, locate_protocol) == 320U);
static_assert(offsetof(BootServices, set_watchdog_timer) == 256U);

enum class ResetType : std::uint32_t {
    Cold,
    Warm,
    Shutdown,
    PlatformSpecific,
};

using ResetSystem = void (AURA67_EFIAPI *)(ResetType, Status, UintN, void*);

struct EfiTime {
    std::uint16_t year;
    std::uint8_t month;
    std::uint8_t day;
    std::uint8_t hour;
    std::uint8_t minute;
    std::uint8_t second;
    std::uint8_t pad1;
    std::uint32_t nanosecond;
    std::int16_t timezone;
    std::uint8_t daylight;
    std::uint8_t pad2;
};

using GetTime = Status (AURA67_EFIAPI *)(EfiTime*, void*);

struct RuntimeServices {
    TableHeader header;
    GetTime get_time;
    void* set_time;
    void* get_wakeup_time;
    void* set_wakeup_time;
    void* set_virtual_address_map;
    void* convert_pointer;
    void* get_variable;
    void* get_next_variable_name;
    void* set_variable;
    void* get_next_high_monotonic_count;
    ResetSystem reset_system;
    void* update_capsule;
    void* query_capsule_capabilities;
    void* query_variable_info;
};

static_assert(sizeof(EfiTime) == 16U);
static_assert(offsetof(RuntimeServices, get_time) == 24U);
static_assert(offsetof(RuntimeServices, reset_system) == 104U);

enum class GraphicsPixelFormat : std::uint32_t {
    RedGreenBlueReserved8BitPerColor,
    BlueGreenRedReserved8BitPerColor,
    BitMask,
    BltOnly,
    Maximum,
};

struct PixelBitmask {
    std::uint32_t red_mask;
    std::uint32_t green_mask;
    std::uint32_t blue_mask;
    std::uint32_t reserved_mask;
};

struct GraphicsOutputModeInformation {
    std::uint32_t version;
    std::uint32_t horizontal_resolution;
    std::uint32_t vertical_resolution;
    GraphicsPixelFormat pixel_format;
    PixelBitmask pixel_information;
    std::uint32_t pixels_per_scan_line;
};

struct GraphicsOutputProtocolMode {
    std::uint32_t max_mode;
    std::uint32_t mode;
    GraphicsOutputModeInformation* info;
    UintN size_of_info;
    PhysicalAddress framebuffer_base;
    UintN framebuffer_size;
};

struct GraphicsOutputBltPixel {
    std::uint8_t blue;
    std::uint8_t green;
    std::uint8_t red;
    std::uint8_t reserved;
};

enum class GraphicsOutputBltOperation : std::uint32_t {
    VideoFill,
    VideoToBltBuffer,
    BufferToVideo,
    VideoToVideo,
    Maximum,
};

struct GraphicsOutputProtocol;
using GraphicsQueryMode = Status (AURA67_EFIAPI *)(GraphicsOutputProtocol*, std::uint32_t,
                                                    UintN*, GraphicsOutputModeInformation**);
using GraphicsSetMode = Status (AURA67_EFIAPI *)(GraphicsOutputProtocol*, std::uint32_t);
using GraphicsBlt = Status (AURA67_EFIAPI *)(GraphicsOutputProtocol*, GraphicsOutputBltPixel*,
                                             GraphicsOutputBltOperation, UintN, UintN, UintN,
                                             UintN, UintN, UintN, UintN);

struct GraphicsOutputProtocol {
    GraphicsQueryMode query_mode;
    GraphicsSetMode set_mode;
    GraphicsBlt blt;
    GraphicsOutputProtocolMode* mode;
};

struct SystemTable {
    TableHeader header;
    Char16* firmware_vendor;
    std::uint32_t firmware_revision;
    Handle console_in_handle;
    SimpleTextInputProtocol* console_in;
    Handle console_out_handle;
    void* console_out;
    Handle standard_error_handle;
    void* standard_error;
    RuntimeServices* runtime_services;
    BootServices* boot_services;
    UintN number_of_table_entries;
    void* configuration_table;
};

static_assert(offsetof(SystemTable, runtime_services) == 88U);
static_assert(offsetof(SystemTable, boot_services) == 96U);

constexpr Guid kGraphicsOutputProtocolGuid{
    0x9042a9deU,
    0x23dcU,
    0x4a38U,
    {0x96U, 0xfbU, 0x7aU, 0xdeU, 0xd0U, 0x80U, 0x51U, 0x6aU},
};

constexpr std::uint16_t kScanUp = 0x0001U;
constexpr std::uint16_t kScanDown = 0x0002U;
constexpr std::uint16_t kScanRight = 0x0003U;
constexpr std::uint16_t kScanLeft = 0x0004U;
constexpr std::uint16_t kScanEscape = 0x0017U;

} // namespace aura67::uefi
