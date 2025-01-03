#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE

#include <windows.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

#pragma comment(lib, "User32.lib")

// 配置类，集中管理所有可配置参数
class CursorConfig {
public:
    static constexpr double kSpeedThreshold = 3000.0;    // 移动速度阈值 (像素/秒)
    static constexpr int kEnlargeDurationMs = 1000;      // 光标放大持续时间 (毫秒)
    static constexpr int kCheckIntervalMs = 200;         // 检查摇动频率 (毫秒)
    static constexpr UINT_PTR kTimerId = 1;              // 定时器ID
    static constexpr UINT kTimerInterval = 100;          // 定时器间隔(毫秒)
};

class CursorUtils {
public:
    static HCURSOR ScaleCursor(HCURSOR src_cursor, double scale_factor) {
        if (!src_cursor || scale_factor <= 0) {
            return nullptr;
        }

        // 获取光标信息
        ICONINFO icon_info;
        if (!GetIconInfo(src_cursor, &icon_info)) {
            return nullptr;
        }

        // 使用 RAII 管理位图资源
        std::unique_ptr<std::remove_pointer<HBITMAP>::type, decltype(&DeleteObject)> 
            color_bitmap(icon_info.hbmColor, DeleteObject);
        std::unique_ptr<std::remove_pointer<HBITMAP>::type, decltype(&DeleteObject)> 
            mask_bitmap(icon_info.hbmMask, DeleteObject);

        // 获取位图信息
        BITMAP bm;
        if (!GetObject(icon_info.hbmColor ? icon_info.hbmColor : icon_info.hbmMask, 
                      sizeof(BITMAP), &bm)) {
            return nullptr;
        }

        // 计算新的尺寸
        int new_width = static_cast<int>(bm.bmWidth * scale_factor);
        int new_height = static_cast<int>(bm.bmHeight * scale_factor);

        // 创建兼容DC
        HDC screen_dc = GetDC(nullptr);
        if (!screen_dc) {
            return nullptr;
        }
        HDC src_dc = CreateCompatibleDC(screen_dc);
        HDC dst_dc = CreateCompatibleDC(screen_dc);
        if (!src_dc || !dst_dc) {
            if (src_dc) DeleteDC(src_dc);
            if (dst_dc) DeleteDC(dst_dc);
            ReleaseDC(nullptr, screen_dc);
            return nullptr;
        }

        // 创建新的彩色位图和掩码位图
        HBITMAP new_color = nullptr;
        HBITMAP new_mask = nullptr;
        HCURSOR new_cursor = nullptr;

        do {
            // 创建放大后的彩色位图
            BITMAPINFO bmi = {0};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = new_width;
            bmi.bmiHeader.biHeight = new_height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* color_bits = nullptr;
            new_color = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &color_bits, nullptr, 0);
            if (!new_color) break;

            // 创建掩码位图
            new_mask = CreateBitmap(new_width, new_height, 1, 1, nullptr);
            if (!new_mask) break;

            // 选择源位图
            HBITMAP old_src_color = (HBITMAP)SelectObject(src_dc, 
                icon_info.hbmColor ? icon_info.hbmColor : icon_info.hbmMask);
            HBITMAP old_dst_color = (HBITMAP)SelectObject(dst_dc, new_color);

            // 执行缩放
            SetStretchBltMode(dst_dc, HALFTONE);
            SetBrushOrgEx(dst_dc, 0, 0, nullptr);
            StretchBlt(dst_dc, 0, 0, new_width, new_height,
                      src_dc, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);

            // 如果有彩色位图，还需要处理掩码位图
            if (icon_info.hbmColor) {
                SelectObject(src_dc, icon_info.hbmMask);
                SelectObject(dst_dc, new_mask);
                StretchBlt(dst_dc, 0, 0, new_width, new_height,
                          src_dc, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            }

            // 恢复DC
            SelectObject(src_dc, old_src_color);
            SelectObject(dst_dc, old_dst_color);

            // 创建新的光标
            ICONINFO new_icon_info = {0};
            new_icon_info.fIcon = FALSE;  // 指定创建光标而不是图标
            new_icon_info.xHotspot = static_cast<DWORD>(icon_info.xHotspot * scale_factor);
            new_icon_info.yHotspot = static_cast<DWORD>(icon_info.yHotspot * scale_factor);
            new_icon_info.hbmMask = new_mask;
            new_icon_info.hbmColor = new_color;

            new_cursor = CreateIconIndirect(&new_icon_info);

        } while (false);

        // 清理资源
        if (new_color) DeleteObject(new_color);
        if (new_mask) DeleteObject(new_mask);
        DeleteDC(src_dc);
        DeleteDC(dst_dc);
        ReleaseDC(nullptr, screen_dc);

        return new_cursor;
    }
};

HCURSOR GetSystemArrowCursor() {
    CURSORINFO ci = { sizeof(CURSORINFO) };
    if (GetCursorInfo(&ci)) {
        return CopyCursor(ci.hCursor);
    }
    return nullptr;
}

// 光标状态管理类
class CursorState {
public:
    CursorState() {
        // 保存系统默认光标
        original_cursor_ = CopyCursor(LoadCursor(nullptr, IDC_ARROW));
        if (!original_cursor_) {
            throw std::runtime_error("Failed to backup original cursor");
        }

        // 创建放大的光标
        large_cursor_ = CursorUtils::ScaleCursor(original_cursor_, 2.0);  // 放大2倍
        if (!large_cursor_) {
            throw std::runtime_error("Failed to create large cursor");
        }
    }

    ~CursorState() {
        std::cout << "CursorState destroyed" << std::endl;
        // 使用 SystemParametersInfo 恢复所有系统光标
        if (SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE)) {
            is_enlarged_ = false;
        }
        if (original_cursor_) {
            DestroyCursor(original_cursor_);
        }
        if (large_cursor_) {
            DestroyCursor(large_cursor_);
        }
    }

    void Enlarge() {
        if (!is_enlarged_) {
            // 为 SetSystemCursor 创建一个新的光标副本
            HCURSOR cursor_copy = CopyCursor(large_cursor_);
            if (cursor_copy) {
                if (SetSystemCursor(cursor_copy, OCR_NORMAL)) {
                    is_enlarged_ = true;
                    enlarge_start_time_ = std::chrono::high_resolution_clock::now();
                } else {
                    DestroyCursor(cursor_copy);
                }
            }
        }
    }

    void RestoreIfNeeded() {
        if (is_enlarged_) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - enlarge_start_time_).count();
            
            if (elapsed > CursorConfig::kEnlargeDurationMs) {
                RestoreOriginalCursor();
            }
        }
    }

private:
    void RestoreOriginalCursor() {
        if (is_enlarged_) {
            HCURSOR cursor_copy = CopyCursor(original_cursor_);
            if (cursor_copy) {
                if (SetSystemCursor(cursor_copy, OCR_NORMAL)) {
                    is_enlarged_ = false;
                } else {
                    DestroyCursor(cursor_copy);
                }
            }
        }
    }

    HCURSOR original_cursor_ = nullptr;
    HCURSOR large_cursor_ = nullptr;
    bool is_enlarged_ = false;
    std::chrono::high_resolution_clock::time_point enlarge_start_time_;
};

// 鼠标移动检测器类
class MouseMoveDetector {
public:
    MouseMoveDetector() {
        GetCursorPos(&last_pos_);
        last_time_ = std::chrono::high_resolution_clock::now();
    }

    bool ShouldEnlargeCursor(const POINT& current_pos) {
        auto now = std::chrono::high_resolution_clock::now();
        auto delta_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_time_).count();

        if (delta_time > 0) {
            double dx = static_cast<double>(current_pos.x - last_pos_.x);
            double dy = static_cast<double>(current_pos.y - last_pos_.y);
            double distance = std::sqrt(dx * dx + dy * dy);
            double speed = (distance / delta_time) * 1000.0;

            last_pos_ = current_pos;
            last_time_ = now;

            return speed > CursorConfig::kSpeedThreshold;
        }
        return false;
    }

private:
    POINT last_pos_;
    std::chrono::high_resolution_clock::time_point last_time_;
};

class ShakeToFindCursor {
public:
    static ShakeToFindCursor& GetInstance() {
        static ShakeToFindCursor instance;
        return instance;
    }

    bool Initialize() {
        // 注册窗口类
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"ShakeToFindCursorClass";
        
        if (!RegisterClassExW(&wc)) {
            throw std::runtime_error("Failed to register window class");
        }

        // 创建隐藏窗口
        hwnd_ = CreateWindowW(
            L"ShakeToFindCursorClass",
            L"ShakeToFindCursor",
            WS_OVERLAPPED,
            CW_USEDEFAULT, CW_USEDEFAULT,
            0, 0,
            nullptr, nullptr,
            GetModuleHandle(nullptr),
            nullptr
        );

        if (!hwnd_) {
            throw std::runtime_error("Failed to create window");
        }

        // 设置窗口实例指针
        SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        // 创建定时器
        if (!SetTimer(hwnd_, CursorConfig::kTimerId, CursorConfig::kTimerInterval, nullptr)) {
            DestroyWindow(hwnd_);
            throw std::runtime_error("Failed to create timer");
        }

        // 安装鼠标钩子
        mouse_hook_ = SetWindowsHookEx(
            WH_MOUSE_LL, 
            MouseProc, 
            GetModuleHandle(nullptr), 
            0
        );

        if (!mouse_hook_) {
            KillTimer(hwnd_, CursorConfig::kTimerId);
            DestroyWindow(hwnd_);
            throw std::runtime_error("Failed to install mouse hook");
        }

        // 设置 Ctrl+C 处理
        SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

        return true;
    }

    void Run() {
        MSG msg;
        running_ = true;

        while (running_) {
            // 使用PeekMessage而不是GetMessage，这样可以在没有消息时也能处理定时器
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running_ = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            // 让出CPU时间片
            Sleep(1);
        }
    }

    void Stop() {
        running_ = false;
        if (hwnd_) {
            PostMessage(hwnd_, WM_QUIT, 0, 0);
        }
    }

    ~ShakeToFindCursor() {
        if (mouse_hook_) {
            UnhookWindowsHookEx(mouse_hook_);
        }
        if (hwnd_) {
            KillTimer(hwnd_, CursorConfig::kTimerId);
            DestroyWindow(hwnd_);
        }
        SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
    }

    void ProcessMouseMove(const MSLLHOOKSTRUCT* mouse_info) {
        if (move_detector_.ShouldEnlargeCursor(mouse_info->pt)) {
            cursor_state_.Enlarge();
        }
    }

private:
    ShakeToFindCursor() = default;
    ShakeToFindCursor(const ShakeToFindCursor&) = delete;
    ShakeToFindCursor& operator=(const ShakeToFindCursor&) = delete;

    static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION && wParam == WM_MOUSEMOVE) {
            auto& instance = GetInstance();
            instance.ProcessMouseMove(reinterpret_cast<MSLLHOOKSTRUCT*>(lParam));
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* instance = reinterpret_cast<ShakeToFindCursor*>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA));

        switch (msg) {
            case WM_TIMER:
                if (wParam == CursorConfig::kTimerId && instance) {
                    instance->cursor_state_.RestoreIfNeeded();
                }
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
        if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
            GetInstance().Stop();
            return TRUE;
        }
        return FALSE;
    }

    HHOOK mouse_hook_ = nullptr;
    HWND hwnd_ = nullptr;
    CursorState cursor_state_;
    MouseMoveDetector move_detector_;
    std::atomic<bool> running_{false};
};

bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(
        &ntAuthority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &adminGroup)) {
        if (!CheckTokenMembership(nullptr, adminGroup, &isAdmin)) {
            isAdmin = FALSE;
        }
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

int main() {
    if (!IsRunAsAdmin()) {
        std::cerr << "This program requires administrator privileges to run." << std::endl;
        return 1;
    }

    SetProcessDPIAware();

    try {
        auto& cursor_finder = ShakeToFindCursor::GetInstance();
        if (!cursor_finder.Initialize()) {
            return 1;
        }

        std::cout << "Shake to Find Cursor demo started. Move the mouse quickly to trigger zoom." << std::endl;
        std::cout << "Press Ctrl + C to exit." << std::endl;

        cursor_finder.Run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
        return 1;
    }
    return 0;
}