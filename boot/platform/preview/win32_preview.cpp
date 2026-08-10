#include "game/game.h"
#include "common/render_policy.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace {

constexpr wchar_t kWindowClass[] = L"GooseBootAura67Preview";
constexpr wchar_t kWindowTitle[] = L"AURA 67 - safe Win32 preview (no reboot, no disk access)";
constexpr std::size_t kPixelCount =
    static_cast<std::size_t>(aura67::kFrameWidth) * aura67::kFrameHeight;

std::uint32_t g_pixels[kPixelCount]{};
aura67::GameState g_game{};
aura67::FrameBuffer g_framebuffer{
    reinterpret_cast<std::uint8_t*>(g_pixels),
    aura67::kFrameWidth,
    aura67::kFrameHeight,
    aura67::kFrameWidth * aura67::kBytesPerPixel,
    aura67::PixelFormat::Bgra8888,
    aura67::RenderDetail::Detailed,
};
std::uint32_t g_held = 0U;
std::uint32_t g_pressed = 0U;
std::uint32_t g_released = 0U;
bool g_running = true;
std::uint32_t g_logical_processors = 0U;
std::uint64_t g_physical_memory_bytes = 0U;

void detect_machine_capabilities() noexcept {
    SYSTEM_INFO system_info{};
    GetNativeSystemInfo(&system_info);
    g_logical_processors =
        static_cast<std::uint32_t>(system_info.dwNumberOfProcessors);

    MEMORYSTATUSEX memory{};
    memory.dwLength = static_cast<DWORD>(sizeof(memory));
    if (GlobalMemoryStatusEx(&memory) != FALSE) {
        g_physical_memory_bytes =
            static_cast<std::uint64_t>(memory.ullTotalPhys);
    }
}

void update_render_detail_for_client(HWND window) noexcept {
    RECT client{};
    if (GetClientRect(window, &client) == FALSE) {
        g_framebuffer.detail = aura67::RenderDetail::Reduced;
        return;
    }
    const int client_width = client.right - client.left;
    const int client_height = client.bottom - client.top;
    const aura67::PresentationSize fitted = aura67::fit_frame_to_bounds(
        client_width > 0 ? static_cast<std::uint32_t>(client_width) : 0U,
        client_height > 0 ? static_cast<std::uint32_t>(client_height) : 0U);
    g_framebuffer.detail = aura67::render_detail_for_machine_and_surface(
        g_logical_processors,
        g_physical_memory_bytes,
        fitted.width,
        fitted.height);
}

std::uint32_t button_for_key(WPARAM key) noexcept {
    switch (key) {
    case VK_UP:
    case 'W': return aura67::input_mask(aura67::InputButton::Up);
    case VK_DOWN:
    case 'S': return aura67::input_mask(aura67::InputButton::Down);
    case VK_LEFT:
    case 'A': return aura67::input_mask(aura67::InputButton::Left);
    case VK_RIGHT:
    case 'D': return aura67::input_mask(aura67::InputButton::Right);
    case VK_SPACE: return aura67::input_mask(aura67::InputButton::Dash);
    case VK_RETURN: return aura67::input_mask(aura67::InputButton::Confirm);
    case 'R': return aura67::input_mask(aura67::InputButton::Reset);
    case VK_ESCAPE: return aura67::input_mask(aura67::InputButton::Escape);
    default: return 0U;
    }
}

void paint_frame(HWND window) noexcept {
    PAINTSTRUCT paint{};
    HDC device = BeginPaint(window, &paint);
    if (device == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(window, &client);
    const int client_width = client.right - client.left;
    const int client_height = client.bottom - client.top;
    FillRect(device, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    const aura67::PresentationSize fitted = aura67::fit_frame_to_bounds(
        client_width > 0 ? static_cast<std::uint32_t>(client_width) : 0U,
        client_height > 0 ? static_cast<std::uint32_t>(client_height) : 0U);
    if (fitted.width == 0U || fitted.height == 0U) {
        EndPaint(window, &paint);
        return;
    }
    const int destination_width = static_cast<int>(fitted.width);
    const int destination_height = static_cast<int>(fitted.height);

    const int destination_x = (client_width - destination_width) / 2;
    const int destination_y = (client_height - destination_height) / 2;
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = static_cast<LONG>(aura67::kFrameWidth);
    bitmap.bmiHeader.biHeight = -static_cast<LONG>(aura67::kFrameHeight);
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(device, COLORONCOLOR);
    StretchDIBits(
        device,
        destination_x,
        destination_y,
        destination_width,
        destination_height,
        0,
        0,
        static_cast<int>(aura67::kFrameWidth),
        static_cast<int>(aura67::kFrameHeight),
        g_pixels,
        &bitmap,
        DIB_RGB_COLORS,
        SRCCOPY);
    EndPaint(window, &paint);
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint_frame(window);
        return 0;
    case WM_SIZE:
        update_render_detail_for_client(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const std::uint32_t button = button_for_key(w_param);
        if (button != 0U && (g_held & button) == 0U) {
            g_pressed |= button;
        }
        g_held |= button;
        return button == 0U ? DefWindowProcW(window, message, w_param, l_param) : 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        const std::uint32_t button = button_for_key(w_param);
        if (button != 0U) {
            g_held &= ~button;
            g_released |= button;
            return 0;
        }
        return DefWindowProcW(window, message, w_param, l_param);
    }
    case WM_KILLFOCUS:
        g_released |= g_held;
        g_held = 0U;
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, w_param, l_param);
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show_command) {
    SetProcessDPIAware();

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    window_class.lpszClassName = kWindowClass;
    if (RegisterClassExW(&window_class) == 0U) {
        return 1;
    }

    detect_machine_capabilities();

    RECT work_area{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0U, &work_area, 0U) == FALSE) {
        work_area.right = GetSystemMetrics(SM_CXSCREEN);
        work_area.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    const int work_width = work_area.right - work_area.left;
    const int work_height = work_area.bottom - work_area.top;
    const int outer_limit_width = work_width > 0 ? work_width * 4 / 5 : 1;
    const int outer_limit_height = work_height > 0 ? work_height * 4 / 5 : 1;

    RECT reference{0, 0, static_cast<LONG>(aura67::kFrameWidth),
                   static_cast<LONG>(aura67::kFrameHeight)};
    AdjustWindowRect(&reference, WS_OVERLAPPEDWINDOW, FALSE);
    const int chrome_width = (reference.right - reference.left) -
                             static_cast<int>(aura67::kFrameWidth);
    const int chrome_height = (reference.bottom - reference.top) -
                              static_cast<int>(aura67::kFrameHeight);
    const int client_limit_width = outer_limit_width > chrome_width
                                       ? outer_limit_width - chrome_width
                                       : 1;
    const int client_limit_height = outer_limit_height > chrome_height
                                        ? outer_limit_height - chrome_height
                                        : 1;
    const aura67::PresentationSize initial_client = aura67::fit_frame_to_bounds(
        static_cast<std::uint32_t>(client_limit_width),
        static_cast<std::uint32_t>(client_limit_height));
    RECT desired{0, 0, static_cast<LONG>(initial_client.width),
                 static_cast<LONG>(initial_client.height)};
    AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);
    const int window_width = desired.right - desired.left;
    const int window_height = desired.bottom - desired.top;
    const int window_x = work_area.left + ((work_area.right - work_area.left) - window_width) / 2;
    const int window_y = work_area.top + ((work_area.bottom - work_area.top) - window_height) / 2;

    HWND window = CreateWindowExW(
        0U,
        kWindowClass,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        window_x,
        window_y,
        window_width,
        window_height,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr) {
        return 2;
    }

    update_render_detail_for_client(window);
    aura67::game_initialize(&g_game, 67U);
    aura67::game_render(g_game, g_framebuffer);
    ShowWindow(window, show_command);
    UpdateWindow(window);

    LARGE_INTEGER frequency{};
    LARGE_INTEGER previous{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);
    LONGLONG accumulator = 0;

    while (g_running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) {
                g_running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!g_running) {
            break;
        }

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        LONGLONG elapsed = now.QuadPart - previous.QuadPart;
        previous = now;
        const LONGLONG maximum_elapsed = frequency.QuadPart / 4;
        if (elapsed > maximum_elapsed) {
            elapsed = maximum_elapsed;
        }
        accumulator += elapsed * static_cast<LONGLONG>(aura67::kTickRate);

        bool dirty = false;
        int catch_up_ticks = 0;
        while (accumulator >= frequency.QuadPart && catch_up_ticks < 5) {
            const aura67::InputState input{g_held, g_pressed, g_released};
            const aura67::GameSignal signal = aura67::game_tick(&g_game, input);
            g_pressed = 0U;
            g_released = 0U;
            accumulator -= frequency.QuadPart;
            ++catch_up_ticks;
            dirty = true;

            if (signal == aura67::GameSignal::QuitRequested) {
                DestroyWindow(window);
                break;
            }
            if (signal == aura67::GameSignal::ResetRequested) {
                // Safety invariant: the preview never calls ExitWindowsEx,
                // InitiateSystemShutdown, or any firmware reset service.
                SetWindowTextW(window, L"AURA 67 Preview - reset request ignored (safe preview)");
            }
        }
        if (catch_up_ticks == 5 && accumulator >= frequency.QuadPart) {
            accumulator = 0;
        }
        if (dirty && g_running) {
            aura67::game_render(g_game, g_framebuffer);
            InvalidateRect(window, nullptr, FALSE);
        }
        if (g_running) {
            Sleep(1U);
        }
    }

    return 0;
}
