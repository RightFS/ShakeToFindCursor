#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE

// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include "resource.h"

// clang-format on

#pragma comment(lib, "User32.lib")

// Configuration class to manage all configurable parameters
class CursorConfig {
 public:
  static constexpr double kSpeedThreshold =
      3000.0;  // Movement speed threshold (pixels/second)
  static constexpr int kEnlargeDurationMs =
      1000;  // Cursor enlargement duration (milliseconds)
  static constexpr int kCheckIntervalMs =
      200;  // Shake check frequency (milliseconds)
  static constexpr UINT_PTR kTimerId = 1;      // Timer ID
  static constexpr UINT kTimerInterval = 100;  // Timer interval (milliseconds)
  static constexpr UINT kTrayIconId = 1;       // Tray icon ID
  static constexpr UINT kTrayIconMessage = WM_APP + 1;  // Tray message ID
  static constexpr UINT kMenuExitId = 2000;             // Exit menu item ID
};

class Logger {
 public:
  static Logger& GetInstance() {
    static Logger instance;
    return instance;
  }

  void Log(const std::string& message) {
    std::ofstream log_file("ShakeToFindCursor.log", std::ios_base::app);
    if (log_file.is_open()) {
      log_file << GetTimestamp() << " - " << message << std::endl;
    }
  }

 private:
  Logger() = default;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
    localtime_s(&now_tm, &now_time_t);
    std::stringstream ss;
    ss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
  }
};

class CursorUtils {
 public:
  static HCURSOR ScaleCursor(HCURSOR src_cursor, double scale_factor) {
    if (!src_cursor || scale_factor <= 0) {
      return nullptr;
    }

    // Get cursor information
    ICONINFO icon_info;
    if (!GetIconInfo(src_cursor, &icon_info)) {
      return nullptr;
    }

    // Use RAII to manage bitmap resources
    std::unique_ptr<std::remove_pointer<HBITMAP>::type, decltype(&DeleteObject)>
        color_bitmap(icon_info.hbmColor, DeleteObject);
    std::unique_ptr<std::remove_pointer<HBITMAP>::type, decltype(&DeleteObject)>
        mask_bitmap(icon_info.hbmMask, DeleteObject);

    // Get bitmap information
    BITMAP bm;
    if (!GetObject(icon_info.hbmColor ? icon_info.hbmColor : icon_info.hbmMask,
                   sizeof(BITMAP), &bm)) {
      return nullptr;
    }

    // Calculate new dimensions
    int new_width = static_cast<int>(bm.bmWidth * scale_factor);
    int new_height = static_cast<int>(bm.bmHeight * scale_factor);

    // Create compatible DC
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

    // Create new color bitmap and mask bitmap
    HBITMAP new_color = nullptr;
    HBITMAP new_mask = nullptr;
    HCURSOR new_cursor = nullptr;

    do {
      // Create enlarged color bitmap
      BITMAPINFO bmi = {0};
      bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth = new_width;
      bmi.bmiHeader.biHeight = new_height;
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;

      void* color_bits = nullptr;
      new_color = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &color_bits,
                                   nullptr, 0);
      if (!new_color) break;

      // Create mask bitmap
      new_mask = CreateBitmap(new_width, new_height, 1, 1, nullptr);
      if (!new_mask) break;

      // Select source bitmap
      HBITMAP old_src_color = (HBITMAP)SelectObject(
          src_dc, icon_info.hbmColor ? icon_info.hbmColor : icon_info.hbmMask);
      HBITMAP old_dst_color = (HBITMAP)SelectObject(dst_dc, new_color);

      // Perform scaling
      SetStretchBltMode(dst_dc, HALFTONE);
      SetBrushOrgEx(dst_dc, 0, 0, nullptr);
      StretchBlt(dst_dc, 0, 0, new_width, new_height, src_dc, 0, 0, bm.bmWidth,
                 bm.bmHeight, SRCCOPY);

      // If there is a color bitmap, also process the mask bitmap
      if (icon_info.hbmColor) {
        SelectObject(src_dc, icon_info.hbmMask);
        SelectObject(dst_dc, new_mask);
        StretchBlt(dst_dc, 0, 0, new_width, new_height, src_dc, 0, 0,
                   bm.bmWidth, bm.bmHeight, SRCCOPY);
      }

      // Restore DC
      SelectObject(src_dc, old_src_color);
      SelectObject(dst_dc, old_dst_color);

      // Create new cursor
      ICONINFO new_icon_info = {0};
      new_icon_info.fIcon =
          FALSE;  // Specify creating a cursor instead of an icon
      new_icon_info.xHotspot =
          static_cast<DWORD>(icon_info.xHotspot * scale_factor);
      new_icon_info.yHotspot =
          static_cast<DWORD>(icon_info.yHotspot * scale_factor);
      new_icon_info.hbmMask = new_mask;
      new_icon_info.hbmColor = new_color;

      new_cursor = CreateIconIndirect(&new_icon_info);

    } while (false);

    // Clean up resources
    if (new_color) DeleteObject(new_color);
    if (new_mask) DeleteObject(new_mask);
    DeleteDC(src_dc);
    DeleteDC(dst_dc);
    ReleaseDC(nullptr, screen_dc);

    return new_cursor;
  }
};

HCURSOR GetSystemArrowCursor() {
  CURSORINFO ci = {sizeof(CURSORINFO)};
  if (GetCursorInfo(&ci)) {
    return CopyCursor(ci.hCursor);
  }
  return nullptr;
}

// Cursor state management class
class CursorState {
 public:
  CursorState() {
    // Save the system default cursor
    original_cursor_ = CopyCursor(LoadCursor(nullptr, IDC_ARROW));
    if (!original_cursor_) {
      throw std::runtime_error("Failed to backup original cursor");
    }

    // Create enlarged cursor
    large_cursor_ =
        CursorUtils::ScaleCursor(original_cursor_, 2.0);  // Enlarge by 2 times
    if (!large_cursor_) {
      throw std::runtime_error("Failed to create large cursor");
    }
  }

  ~CursorState() {
    Logger::GetInstance().Log("CursorState destroyed");
    // Use SystemParametersInfo to restore all system cursors
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
      // Create a new cursor copy for SetSystemCursor
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
                         now - enlarge_start_time_)
                         .count();

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

// Mouse movement detector class
class MouseMoveDetector {
 public:
  MouseMoveDetector() {
    GetCursorPos(&last_pos_);
    last_time_ = std::chrono::high_resolution_clock::now();
  }

  bool ShouldEnlargeCursor(const POINT& current_pos) {
    auto now = std::chrono::high_resolution_clock::now();
    auto delta_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time_)
            .count();

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
    // Register window class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.lpszClassName = L"ShakeToFindCursorClass";

    if (!RegisterClassExW(&wc)) {
      throw std::runtime_error("Failed to register window class");
    }

    // Create hidden window
    hwnd_ = CreateWindowW(L"ShakeToFindCursorClass", L"ShakeToFindCursor",
                          WS_OVERLAPPED, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
                          nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!hwnd_) {
      throw std::runtime_error("Failed to create window");
    }

    // Set window instance pointer
    SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Create timer
    if (!SetTimer(hwnd_, CursorConfig::kTimerId, CursorConfig::kTimerInterval,
                  nullptr)) {
      DestroyWindow(hwnd_);
      throw std::runtime_error("Failed to create timer");
    }

    // Install mouse hook
    mouse_hook_ =
        SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(nullptr), 0);

    if (!mouse_hook_) {
      KillTimer(hwnd_, CursorConfig::kTimerId);
      DestroyWindow(hwnd_);
      throw std::runtime_error("Failed to install mouse hook");
    }

    // Set Ctrl+C handler
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // Create tray icon
    NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
    nid.hWnd = hwnd_;
    nid.uID = CursorConfig::kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = CursorConfig::kTrayIconMessage;
    nid.hIcon =
        LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APP_ICON));
    wcscpy_s(nid.szTip, L"Shake to Find Cursor");

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
      KillTimer(hwnd_, CursorConfig::kTimerId);
      DestroyWindow(hwnd_);
      throw std::runtime_error("Failed to create tray icon");
    }

    tray_icon_added_ = true;

    return true;
  }

  void Run() {
    MSG msg;
    running_ = true;

    while (running_) {
      // Use PeekMessage instead of GetMessage to handle timers even when there
      // are no messages
      while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          running_ = false;
          break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }

      // Yield CPU time slice
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
    RemoveTrayIcon();
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

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
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

      case CursorConfig::kTrayIconMessage:
        if (LOWORD(lParam) == WM_RBUTTONUP) {
          instance->ShowContextMenu(hwnd);
        }
        return 0;

      case WM_COMMAND:
        if (LOWORD(wParam) == CursorConfig::kMenuExitId) {
          instance->Stop();
        }
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

  void RemoveTrayIcon() {
    if (tray_icon_added_ && hwnd_) {
      NOTIFYICONDATA nid = {sizeof(NOTIFYICONDATA)};
      nid.hWnd = hwnd_;
      nid.uID = CursorConfig::kTrayIconId;
      Shell_NotifyIcon(NIM_DELETE, &nid);
      tray_icon_added_ = false;
    }
  }

  void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, CursorConfig::kMenuExitId, L"Exit");

    // Set as foreground window, otherwise the right-click menu will not show
    SetForegroundWindow(hwnd);

    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
  }

  HHOOK mouse_hook_ = nullptr;
  HWND hwnd_ = nullptr;
  CursorState cursor_state_;
  MouseMoveDetector move_detector_;
  std::atomic<bool> running_{false};
  bool tray_icon_added_ = false;
};

bool IsRunAsAdmin() {
  BOOL is_admin = FALSE;
  PSID admin_group = nullptr;
  SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;

  if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                               &admin_group)) {
    if (!CheckTokenMembership(nullptr, admin_group, &is_admin)) {
      is_admin = FALSE;
    }
    FreeSid(admin_group);
  }
  return is_admin != FALSE;
}

#ifdef CONSOLE_MODE
int main() {
  if (!IsRunAsAdmin()) {
    std::cerr << "This program requires administrator privileges to run."
              << std::endl;
    return 1;
  }

  SetProcessDPIAware();

  try {
    auto& cursor_finder = ShakeToFindCursor::GetInstance();
    if (!cursor_finder.Initialize()) {
      return 1;
    }

    std::cout << "Shake to Find Cursor demo started. Move the mouse quickly to "
                 "trigger zoom."
              << std::endl;
    std::cout << "Press Ctrl + C to exit." << std::endl;

    cursor_finder.Run();
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
    return 1;
  }
  return 0;
}
#else
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                      LPWSTR lpCmdLine, int nCmdShow) {
  UNREFERENCED_PARAMETER(hInstance);
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);
  UNREFERENCED_PARAMETER(nCmdShow);

  if (!IsRunAsAdmin()) {
    MessageBoxW(nullptr,
                L"This program requires administrator privileges to run.",
                L"Error", MB_OK | MB_ICONERROR);
    return 1;
  }

  SetProcessDPIAware();

  try {
    auto& cursor_finder = ShakeToFindCursor::GetInstance();
    if (!cursor_finder.Initialize()) {
      return 1;
    }

    Logger::GetInstance().Log(
        "Shake to Find Cursor demo started. Move the mouse quickly to trigger "
        "zoom.");

    cursor_finder.Run();
  } catch (const std::exception& e) {
    std::wstringstream ws;
    ws << L"Error: " << e.what();
    MessageBoxW(nullptr, ws.str().c_str(), L"Error", MB_OK | MB_ICONERROR);
    SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
    return 1;
  }
  return 0;
}
#endif