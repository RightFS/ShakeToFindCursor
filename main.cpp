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
#include <deque>
#include <vector>
#include <stdexcept>
#include "resource.h"



// Configuration class to manage all configurable parameters
class CursorConfig {
 public:
  static constexpr double kScaleFactor = 3.0;           // Cursor enlargement factor
  static constexpr size_t kHistorySize = 10;            // Keep last 10 movements
  static constexpr int kMinDirectionChanges = 5;        // Minimum direction changes required
  static constexpr double kMinMovementSpeed = 800.0;    // Minimum speed in pixels/second
  static constexpr int kMaxTimeWindow = 500;            // Time window in milliseconds
  static constexpr int kEnlargeDurationMs = 500;        // Cursor enlargement duration (milliseconds)
  static constexpr UINT_PTR kTimerId = 1;               // Timer ID
  static constexpr UINT kTimerInterval = 100;           // Timer interval (milliseconds)
  static constexpr UINT kTrayIconId = 1;                // Tray icon ID
  static constexpr UINT kTrayIconMessage = WM_APP + 1;  // Tray message ID
  static constexpr UINT kMenuExitId = 2000;             // Exit menu item ID
  static constexpr UINT kMenuAutoStartId = 2001;        // Enable auto-start menu item ID
  static constexpr UINT kMenuDisableAutoStartId = 2002; // Disable auto-start menu item ID

  enum class MouseTrackingMode {
    kHook,    // Use SetWindowsHookEx
    kPolling  // Use GetCursorPos in WM_TIMER
  };
};

// clang-format on

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

#ifdef _DEBUG
#define DEBUG_LOG(msg) Logger::GetInstance().Log(msg)
#else
#define DEBUG_LOG(msg)
#endif

class AutoStartManager {
 public:
  static bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_READ, &hKey) != ERROR_SUCCESS) {
      return false;
    }

    WCHAR path[MAX_PATH];
    DWORD size = sizeof(path);
    bool exists = RegQueryValueExW(hKey, L"ShakeToFindCursor", nullptr, nullptr,
                                   (LPBYTE)path, &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return exists;
  }

  static bool IsWow64() {
    BOOL is_wow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &is_wow64);
    return is_wow64 == TRUE;
  }

  static bool EnableAutoStart() {
    WCHAR exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
      return false;
    }

    // Add quotes and parameters
    std::wstring command = L"\"" + std::wstring(exePath) + L"\"";

#if _WIN64 || __amd64__
    if (!EnableAutoStartWow64()) {
      return false;
    }
#endif

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
      return false;
    }

    LSTATUS status = RegSetValueExW(
        hKey, L"ShakeToFindCursor", 0, REG_SZ, (LPBYTE)command.c_str(),
        static_cast<DWORD>((command.length() + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);
    bool success = (status == ERROR_SUCCESS);
    if (success) {
      success = SetAppCompatFlags();
    }
    return success;
  }

  static bool EnableAutoStartWow64() {
    WCHAR exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
      return false;
    }

    // Add quotes and parameters
    std::wstring command = L"\"" + std::wstring(exePath) + L"\"";

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_SET_VALUE | KEY_WOW64_32KEY,
                      &hKey) != ERROR_SUCCESS) {
      return false;
    }

    LSTATUS status = RegSetValueExW(
        hKey, L"ShakeToFindCursor", 0, REG_SZ, (LPBYTE)command.c_str(),
        static_cast<DWORD>((command.length() + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);
    bool success = (status == ERROR_SUCCESS);
    if (success) {
      success = SetAppCompatFlags();
    }
    return success;
  }

  static bool DisableAutoStartWow64() {
#if _WIN64 || __amd64__
    if (!DisableAutoStartWow64()) {
      return false;
    }
#endif
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_SET_VALUE | KEY_WOW64_32KEY,
                      &hKey) != ERROR_SUCCESS) {
      return false;
    }

    LSTATUS status = RegDeleteValueW(hKey, L"ShakeToFindCursor");
    RegCloseKey(hKey);
    bool success = (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
    if (success) {
      success = ClearAppCompatFlags();
    }
    return status == ERROR_SUCCESS;
  }

  static bool DisableAutoStart() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
      return false;
    }

    LSTATUS status = RegDeleteValueW(hKey, L"ShakeToFindCursor");
    RegCloseKey(hKey);
    bool success = (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
    if (success) {
      success = ClearAppCompatFlags();
    }
    return status == ERROR_SUCCESS;
  }

  static bool SetAppCompatFlags() {
    WCHAR exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
      return false;
    }

    HKEY hKey;
    LSTATUS status =
        RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows "
                        L"NT\\CurrentVersion\\AppCompatFlags\\Layers",
                        0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                        nullptr, &hKey, nullptr);

    if (status != ERROR_SUCCESS) {
      return false;
    }

    // Set RUNASADMIN flag
    const wchar_t* value = L"~ RUNASADMIN";
    status =
        RegSetValueExW(hKey, exePath, 0, REG_SZ, (BYTE*)value,
                       (static_cast<DWORD>(wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
  }

  static bool ClearAppCompatFlags() {
    WCHAR exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
      return false;
    }

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows "
                      L"NT\\CurrentVersion\\AppCompatFlags\\Layers",
                      0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
      return false;
    }

    LSTATUS status = RegDeleteValueW(hKey, exePath);
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
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

// Large cursor class
class LargeCursor {
 public:
  LargeCursor(LPCWSTR cursor_name, DWORD system_cursor_id)
      : system_cursor_id_(system_cursor_id) {
    // Load the system cursor
    original_cursor_ = CopyCursor(LoadCursorW(nullptr, cursor_name));
    if (!original_cursor_) {
      throw std::runtime_error("Failed to load system cursor");
    }

    // Create enlarged cursor
    large_cursor_ = CursorUtils::ScaleCursor(
        original_cursor_, CursorConfig::kScaleFactor);  // Enlarge by 2 times
    if (!large_cursor_) {
      throw std::runtime_error("Failed to create large cursor");
    }
  }

  void Enlarge() {
    if (large_cursor_) {
      HCURSOR cursor_copy = CopyCursor(large_cursor_);
      if (cursor_copy) {
        SetSystemCursor(cursor_copy, system_cursor_id_);
      } else {
        DestroyCursor(cursor_copy);
      }
    }
  }

  void Restore() {
    if (original_cursor_) {
      HCURSOR cursor_copy = CopyCursor(original_cursor_);
      if (cursor_copy) {
        SetSystemCursor(cursor_copy, system_cursor_id_);
      } else {
        DestroyCursor(cursor_copy);
      }
    }
  }

  ~LargeCursor() {
    if (original_cursor_) {
      DestroyCursor(original_cursor_);
    }
    if (large_cursor_) {
      DestroyCursor(large_cursor_);
    }
  }

 private:
  DWORD system_cursor_id_;
  HCURSOR original_cursor_ = nullptr;
  HCURSOR large_cursor_ = nullptr;
};

// Large cursor manager class
class LargeCursorManager {
 public:
  LargeCursorManager() {
    // Create large cursor for each system cursor
    large_cursors_.push_back(
        std::make_unique<LargeCursor>(IDC_ARROW, OCR_NORMAL));
    large_cursors_.push_back(
        std::make_unique<LargeCursor>(IDC_IBEAM, OCR_IBEAM));
    large_cursors_.push_back(std::make_unique<LargeCursor>(IDC_WAIT, OCR_WAIT));
    large_cursors_.push_back(
        std::make_unique<LargeCursor>(IDC_CROSS, OCR_CROSS));
    large_cursors_.push_back(
        std::make_unique<LargeCursor>(IDC_UPARROW, OCR_UP));
    large_cursors_.push_back(
        std::make_unique<LargeCursor>(IDC_SIZENWSE, OCR_SIZENWSE));
    large_cursors_.push_back(
        std::make_unique<LargeCursor>(IDC_SIZENESW, OCR_SIZENESW));
    large_cursors_.push_back(
        std::make_unique<LargeCursor>(IDC_SIZEWE, OCR_SIZEWE));
    large_cursors_.push_back(
        std::make_unique<LargeCursor>(IDC_SIZENS, OCR_SIZENS));
    large_cursors_.push_back(
        std::make_unique<LargeCursor>(IDC_SIZEALL, OCR_SIZEALL));
    large_cursors_.push_back(std::make_unique<LargeCursor>(IDC_NO, OCR_NO));
    large_cursors_.push_back(std::make_unique<LargeCursor>(IDC_HAND, OCR_HAND));
    large_cursors_.push_back(
        std::make_unique<LargeCursor>(IDC_APPSTARTING, OCR_APPSTARTING));
  }

  void EnlargeAll() {
    for (const auto& cursor : large_cursors_) {
      cursor->Enlarge();
    }
  }

  void RestoreAll() {
    for (const auto& cursor : large_cursors_) {
      cursor->Restore();
    }
  }

 private:
  std::vector<std::unique_ptr<LargeCursor>> large_cursors_;
};

// Cursor state management class
class CursorState {
 public:
  CursorState() {}

  ~CursorState() {
    DEBUG_LOG("CursorState destroyed");
    // Use SystemParametersInfo to restore all system cursors
    if (SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE)) {
      is_enlarged_ = false;
    }
  }

  void Enlarge() {
    if (!is_enlarged_) {
      // Enlarge all system cursors
      large_cursor_manager_.EnlargeAll();
      is_enlarged_ = true;
      enlarge_start_time_ = std::chrono::high_resolution_clock::now();
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
      // Restore all system cursors
      large_cursor_manager_.RestoreAll();
      is_enlarged_ = false;
    }
  }

  LargeCursorManager large_cursor_manager_;
  bool is_enlarged_ = false;
  std::chrono::high_resolution_clock::time_point enlarge_start_time_;
};

// Mouse movement detector class with shake pattern recognition
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

    if (delta_time <= 0) return false;

    // Calculate movement vector
    int dx = current_pos.x - last_pos_.x;
    int dy = current_pos.y - last_pos_.y;

    // Update position history
    movement_history_.push_back({dx, dy, delta_time});
    if (movement_history_.size() > CursorConfig::kHistorySize) {
      movement_history_.pop_front();
    }

    last_pos_ = current_pos;
    last_time_ = now;

    return DetectShakePattern();
  }

 private:
  struct Movement {
    int dx;
    int dy;
    long long dt;
  };

  bool DetectShakePattern() {
    if (movement_history_.size() < CursorConfig::kHistorySize) return false;

    int direction_changes = 0;
    double total_speed = 0.0;
    long long total_time = 0;

    // Previous movement direction (-1: negative, 1: positive, 0: neutral)
    int last_x_dir = 0;
    int last_y_dir = 0;

    for (const auto& mov : movement_history_) {
      // Calculate current direction
      int curr_x_dir = (mov.dx > 0) ? 1 : (mov.dx < 0) ? -1 : 0;
      int curr_y_dir = (mov.dy > 0) ? 1 : (mov.dy < 0) ? -1 : 0;

      // Count direction changes
      if (last_x_dir != 0 && curr_x_dir != 0 && last_x_dir != curr_x_dir) {
        direction_changes++;
      }
      if (last_y_dir != 0 && curr_y_dir != 0 && last_y_dir != curr_y_dir) {
        direction_changes++;
      }

      // Update last direction
      last_x_dir = curr_x_dir;
      last_y_dir = curr_y_dir;

      // Calculate speed
      double distance = std::sqrt(mov.dx * mov.dx + mov.dy * mov.dy);
      double speed = (mov.dt > 0) ? (distance / mov.dt) * 1000.0 : 0;
      total_speed += speed;
      total_time += mov.dt;
    }

    // Check if we're within the time window
    if (total_time > CursorConfig::kMaxTimeWindow) return false;

    // Calculate average speed
    double avg_speed = total_speed / movement_history_.size();

    // Return true if we have enough direction changes and sufficient speed
    return direction_changes >= CursorConfig::kMinDirectionChanges &&
           avg_speed >= CursorConfig::kMinMovementSpeed;
  }

  POINT last_pos_;
  std::chrono::high_resolution_clock::time_point last_time_;
  std::deque<Movement> movement_history_;
};

class ShakeToFindCursor {
 public:
  static ShakeToFindCursor& GetInstance() {
    static ShakeToFindCursor instance;
    return instance;
  }

  bool Initialize(CursorConfig::MouseTrackingMode mode) {
    tracking_mode_ = mode;

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

    // Create timer with different interval based on mode
    UINT timer_interval =
        (tracking_mode_ == CursorConfig::MouseTrackingMode::kPolling)
            ? 10  // Poll more frequently when using timer
            : CursorConfig::kTimerInterval;

    if (!SetTimer(hwnd_, CursorConfig::kTimerId, timer_interval, nullptr)) {
      DestroyWindow(hwnd_);
      throw std::runtime_error("Failed to create timer");
    }

    // Only install hook if using hook mode
    if (tracking_mode_ == CursorConfig::MouseTrackingMode::kHook) {
      mouse_hook_ =
          SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(nullptr), 0);

      if (!mouse_hook_) {
        KillTimer(hwnd_, CursorConfig::kTimerId);
        DestroyWindow(hwnd_);
        throw std::runtime_error("Failed to install mouse hook");
      }
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
          if (instance->tracking_mode_ ==
              CursorConfig::MouseTrackingMode::kPolling) {
            POINT pt;
            GetCursorPos(&pt);
            instance->ProcessMouseMove(reinterpret_cast<MSLLHOOKSTRUCT*>(&pt));
          }
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
        } else if (LOWORD(wParam) == CursorConfig::kMenuAutoStartId) {
          if (AutoStartManager::EnableAutoStart()) {
            MessageBoxW(hwnd, L"Auto-start enabled successfully.", L"Success",
                        MB_OK | MB_ICONINFORMATION);
          } else {
            MessageBoxW(hwnd, L"Failed to enable auto-start.", L"Error",
                        MB_OK | MB_ICONERROR);
          }
        } else if (LOWORD(wParam) == CursorConfig::kMenuDisableAutoStartId) {
          if (AutoStartManager::DisableAutoStart()) {
            MessageBoxW(hwnd, L"Auto-start disabled successfully.", L"Success",
                        MB_OK | MB_ICONINFORMATION);
          } else {
            MessageBoxW(hwnd, L"Failed to disable auto-start.", L"Error",
                        MB_OK | MB_ICONERROR);
          }
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

    bool is_auto_start = AutoStartManager::IsAutoStartEnabled();
    if (is_auto_start) {
      AppendMenuW(menu, MF_STRING, CursorConfig::kMenuDisableAutoStartId,
                  L"Disable Auto-start");
    } else {
      AppendMenuW(menu, MF_STRING, CursorConfig::kMenuAutoStartId,
                  L"Enable Auto-start");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CursorConfig::kMenuExitId, L"Exit");

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
  CursorConfig::MouseTrackingMode tracking_mode_;
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
int main(int argc, char* argv[]) {
  if (!IsRunAsAdmin()) {
    std::cerr << "This program requires administrator privileges to run."
              << std::endl;
    return 1;
  }

  SetProcessDPIAware();

  CursorConfig::MouseTrackingMode mode =
      CursorConfig::MouseTrackingMode::kPolling;
  if (argc > 1 && std::string(argv[1]) == "--hook") {
    mode = CursorConfig::MouseTrackingMode::kHook;
  }

  try {
    auto& cursor_finder = ShakeToFindCursor::GetInstance();
    if (!cursor_finder.Initialize(mode)) {
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

  CursorConfig::MouseTrackingMode mode =
      CursorConfig::MouseTrackingMode::kPolling;
  if (wcsstr(lpCmdLine, L"--hook")) {
    mode = CursorConfig::MouseTrackingMode::kHook;
  }

  try {
    auto& cursor_finder = ShakeToFindCursor::GetInstance();
    if (!cursor_finder.Initialize(mode)) {
      return 1;
    }

    DEBUG_LOG(
        "Shake to Find Cursor started. Move the mouse quickly to trigger "
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