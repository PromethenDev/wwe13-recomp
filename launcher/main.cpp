#include <windows.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"WWE13LauncherWindow";
constexpr wchar_t kWindowTitle[] = L"WWE '13";
constexpr wchar_t kSettingsSection[] = L"Settings";
constexpr wchar_t kLauncherVersion[] = L"1.0";
constexpr UINT_PTR kChildPollTimer = 1;
constexpr COLORREF kBackground = RGB(18, 20, 24);
constexpr COLORREF kPanel = RGB(29, 32, 38);
constexpr COLORREF kControl = RGB(39, 43, 50);
constexpr COLORREF kText = RGB(244, 245, 247);
constexpr COLORREF kMuted = RGB(164, 171, 181);
constexpr COLORREF kAccent = RGB(220, 35, 48);
constexpr COLORREF kWarning = RGB(255, 183, 77);
constexpr COLORREF kError = RGB(255, 105, 105);
constexpr int kComboItemHeight = 34;
constexpr int kComboBorderAllowance = 6;
constexpr UINT kDefaultDpi = 96;
constexpr int kMainClientWidth = 1040;
constexpr int kMainClientHeight = 680;
constexpr int kMappingClientWidth = 1200;
constexpr int kMappingClientHeight = 670;
constexpr uint32_t kVkPhysicalDeviceTypeIntegratedGpu = 1;
constexpr uint32_t kVkPhysicalDeviceTypeDiscreteGpu = 2;
constexpr size_t kVkPhysicalDeviceFeatureCount = 64;

enum : int {
  kResolutionCombo = 101,
  kFrameRateCombo = 102,
  kDisplayCombo = 103,
  kGpuCombo = 104,
  kAntiAliasingCombo = 105,
  kBrowseButton = 201,
  kPlayButton = 202,
  kExitButton = 203,
  kBugReportButton = 204,
  kKeyboardControlsButton = 205,
  kAutoStartCheckbox = 206,
  kMappingResetButton = 301,
  kMappingSaveButton = 302,
  kMappingCancelButton = 303,
};

struct BindingSpec {
  const wchar_t* label;
  const char* cvar;
  const char* default_key;
};

// Keep these 24 rows aligned with the player-facing table in docs/CONTROLS.md
// and the keybind_* cvars in ReXGlue's mnk_input_driver.cpp.
constexpr BindingSpec kPadBindings[] = {
    {L"A", "keybind_a", "Space"},
    {L"B", "keybind_b", "Shift"},
    {L"X", "keybind_x", "R"},
    {L"Y", "keybind_y", "E"},
    {L"Left trigger (LT)", "keybind_left_trigger", "Z"},
    {L"Right trigger (RT)", "keybind_right_trigger", "Control"},
    {L"Left shoulder (LB)", "keybind_left_shoulder", "Q"},
    {L"Right shoulder (RB)", "keybind_right_shoulder", "F"},
    {L"Left stick up", "keybind_lstick_up", "W"},
    {L"Left stick down", "keybind_lstick_down", "S"},
    {L"Left stick left", "keybind_lstick_left", "A"},
    {L"Left stick right", "keybind_lstick_right", "D"},
    {L"Left stick press (L3)", "keybind_lstick_press", "C"},
    {L"Right stick up", "keybind_rstick_up", "I"},
    {L"Right stick down", "keybind_rstick_down", "K"},
    {L"Right stick left", "keybind_rstick_left", "J"},
    {L"Right stick right", "keybind_rstick_right", "L"},
    {L"Right stick press (R3)", "keybind_rstick_press", "M"},
    {L"D-pad up", "keybind_dpad_up", "Up"},
    {L"D-pad down", "keybind_dpad_down", "Down"},
    {L"D-pad left", "keybind_dpad_left", "Left"},
    {L"D-pad right", "keybind_dpad_right", "Right"},
    {L"Back", "keybind_back", "Tab"},
    {L"Start", "keybind_start", "Escape"},
};
constexpr size_t kPadBindingCount = std::size(kPadBindings);

struct KeyName {
  const wchar_t* name;
  UINT virtual_key;
};

// Exact spellings and aliases copied from ReXGlue's kKeyNames table in
// src/ui/keybinds.cpp. Capture only emits a name found in this table.
constexpr KeyName kKeyNames[] = {
    {L"F1", VK_F1}, {L"F2", VK_F2}, {L"F3", VK_F3}, {L"F4", VK_F4},
    {L"F5", VK_F5}, {L"F6", VK_F6}, {L"F7", VK_F7}, {L"F8", VK_F8},
    {L"F9", VK_F9}, {L"F10", VK_F10}, {L"F11", VK_F11}, {L"F12", VK_F12},
    {L"F13", VK_F13}, {L"F14", VK_F14}, {L"F15", VK_F15}, {L"F16", VK_F16},
    {L"F17", VK_F17}, {L"F18", VK_F18}, {L"F19", VK_F19}, {L"F20", VK_F20},
    {L"F21", VK_F21}, {L"F22", VK_F22}, {L"F23", VK_F23}, {L"F24", VK_F24},
    {L"A", 0x41}, {L"B", 0x42}, {L"C", 0x43}, {L"D", 0x44}, {L"E", 0x45},
    {L"F", 0x46}, {L"G", 0x47}, {L"H", 0x48}, {L"I", 0x49}, {L"J", 0x4A},
    {L"K", 0x4B}, {L"L", 0x4C}, {L"M", 0x4D}, {L"N", 0x4E}, {L"O", 0x4F},
    {L"P", 0x50}, {L"Q", 0x51}, {L"R", 0x52}, {L"S", 0x53}, {L"T", 0x54},
    {L"U", 0x55}, {L"V", 0x56}, {L"W", 0x57}, {L"X", 0x58}, {L"Y", 0x59},
    {L"Z", 0x5A},
    {L"0", 0x30}, {L"1", 0x31}, {L"2", 0x32}, {L"3", 0x33}, {L"4", 0x34},
    {L"5", 0x35}, {L"6", 0x36}, {L"7", 0x37}, {L"8", 0x38}, {L"9", 0x39},
    {L"Backtick", VK_OEM_3}, {L"Minus", VK_OEM_MINUS}, {L"Plus", VK_OEM_PLUS},
    {L"Comma", VK_OEM_COMMA}, {L"Period", VK_OEM_PERIOD}, {L"Semicolon", VK_OEM_1},
    {L"Slash", VK_OEM_2}, {L"Backslash", VK_OEM_5}, {L"LBracket", VK_OEM_4},
    {L"RBracket", VK_OEM_6}, {L"Quote", VK_OEM_7},
    {L"Escape", VK_ESCAPE}, {L"Return", VK_RETURN}, {L"Space", VK_SPACE},
    {L"Tab", VK_TAB}, {L"Backspace", VK_BACK}, {L"Delete", VK_DELETE},
    {L"Insert", VK_INSERT}, {L"Home", VK_HOME}, {L"End", VK_END},
    {L"PageUp", VK_PRIOR}, {L"PageDown", VK_NEXT},
    {L"Left", VK_LEFT}, {L"Right", VK_RIGHT}, {L"Up", VK_UP}, {L"Down", VK_DOWN},
    {L"Shift", VK_SHIFT}, {L"Control", VK_CONTROL}, {L"Alt", VK_MENU},
    {L"Numpad0", VK_NUMPAD0}, {L"Numpad1", VK_NUMPAD1}, {L"Numpad2", VK_NUMPAD2},
    {L"Numpad3", VK_NUMPAD3}, {L"Numpad4", VK_NUMPAD4}, {L"Numpad5", VK_NUMPAD5},
    {L"Numpad6", VK_NUMPAD6}, {L"Numpad7", VK_NUMPAD7}, {L"Numpad8", VK_NUMPAD8},
    {L"Numpad9", VK_NUMPAD9}, {L"NumpadEnter", VK_RETURN}, {L"NumpadPlus", VK_ADD},
    {L"NumpadMinus", VK_SUBTRACT}, {L"NumpadStar", VK_MULTIPLY},
    {L"NumpadSlash", VK_DIVIDE}, {L"PrintScreen", VK_SNAPSHOT}, {L"Pause", VK_PAUSE},
    {L"CapsLock", VK_CAPITAL}, {L"NumLock", VK_NUMLOCK}, {L"ScrollLock", VK_SCROLL},
    {L"LMB", VK_LBUTTON}, {L"RMB", VK_RBUTTON}, {L"MMB", VK_MBUTTON},
};

const KeyName* FindKeyName(std::wstring_view name) {
  for (const KeyName& entry : kKeyNames) {
    if (name == entry.name) return &entry;
  }
  return nullptr;
}

const KeyName* FindKeyByVirtualKey(UINT virtual_key, LPARAM key_flags) {
  // Win32 marks the keypad Enter key as an extended key. ReXGlue accepts both
  // names even though they share VK_RETURN.
  if (virtual_key == VK_RETURN && (key_flags & (LPARAM(1) << 24))) {
    return FindKeyName(L"NumpadEnter");
  }
  for (const KeyName& entry : kKeyNames) {
    if (entry.virtual_key == virtual_key) return &entry;
  }
  return nullptr;
}

struct ResolutionOption {
  const wchar_t* id;
  const wchar_t* label;
  unsigned scale;
  const wchar_t* internal_environment_value;
  const wchar_t* output_resolution;
};

constexpr ResolutionOption kResolutionOptions[] = {
    {L"480p", L"480p (fastest - handhelds / integrated GPUs)", 0, L"480", L"720p"},
    // 576p hidden for v1.0 (owner 2026-10-01: little gain over 480p, dark entrance box); saved 576p -> 480p.
    // {L"576p", L"576p (faster)", 0, L"576", L"720p"},
    {L"720p", L"720p (original)", 0, nullptr, L"720p"},
    // 1080p is plumbed (WWE13_INTERNAL_RES=1080 + --resolution=1080p) but hidden until the intermittent
    // scene corruption in live 1080 runs is fixed (docs/HANDOFF.md, 2026-10-01):
    // {L"1080p", L"1080p (sharp - mid-range GPUs)", 0, L"1080", L"1080p"},
    {L"1440p", L"1440p (sharp - strong GPUs)", 2, nullptr, L"720p"},
};

struct FrameRateOption {
  const wchar_t* id;
  const wchar_t* label;
  const wchar_t* environment_name;
  const wchar_t* environment_value;
  bool enabled;
};

constexpr FrameRateOption kFrameRateOptions[] = {
    {L"classic", L"Classic - 60 in matches, 30 in entrances and cutscenes", nullptr, nullptr,
     true},
    {L"keep-60", L"60 everywhere (experimental)", L"WWE13_KEEP_60", L"1", true},
    {L"lock-30", L"30 (locked)", L"WWE13_LOCK_30", L"1", true},
};

struct AntiAliasingOption {
  const wchar_t* id;
  const wchar_t* label;
  const wchar_t* environment_value;
};

constexpr AntiAliasingOption kAntiAliasingOptions[] = {
    {L"original", L"Original (4x MSAA)", nullptr},
    {L"2x", L"Faster (2x MSAA)", L"2x"},
};

struct DisplayOption {
  const wchar_t* id;
  const wchar_t* label;
  bool fullscreen;
};

constexpr DisplayOption kDisplayOptions[] = {
    {L"fullscreen", L"Fullscreen", true},
    {L"windowed", L"Windowed", false},
};

struct GpuOption {
  std::wstring name;
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
  uint32_t device_type = 0;
  unsigned auto_feature_score = 0;
};

struct Settings {
  std::wstring resolution = L"720p";
  std::wstring frame_rate = L"classic";
  std::wstring anti_aliasing = L"original";
  std::wstring display = L"fullscreen";
  std::wstring gpu_id;
  std::wstring gpu_name;
  std::wstring game_folder;
  bool resolution_explicit = false;
  bool frame_rate_explicit = false;
  bool auto_start = false;
};

struct AppState {
  std::wstring launcher_path;
  std::wstring exe_dir;
  std::wstring settings_path;
  std::wstring default_game_folder;
  std::wstring game_exe;
  std::wstring userdata_dir;
  std::wstring logs_dir;
  Settings settings;
  std::vector<GpuOption> gpus;
  std::wstring gpu_error;
  std::wstring auto_gpu_id;
  std::wstring simulated_auto_gpu_id;
  bool legacy_gpu_setting = false;
  bool needs_schema_upgrade = false;
  bool settings_write_failed = false;
  bool initializing_controls = false;
  UINT dpi = kDefaultDpi;
  UINT mapping_dpi = kDefaultDpi;
  HWND window = nullptr;
  HWND resolution_combo = nullptr;
  HWND frame_rate_combo = nullptr;
  HWND anti_aliasing_combo = nullptr;
  HWND display_combo = nullptr;
  HWND gpu_combo = nullptr;
  HWND browse_button = nullptr;
  HWND play_button = nullptr;
  HWND exit_button = nullptr;
  HWND bug_report_button = nullptr;
  HWND keyboard_controls_button = nullptr;
  HWND auto_start_checkbox = nullptr;
  HWND mapping_window = nullptr;
  HWND mapping_reset_button = nullptr;
  HWND mapping_save_button = nullptr;
  HWND mapping_cancel_button = nullptr;
  std::array<HWND, kPadBindingCount> mapping_key_buttons{};
  std::array<HWND, kPadBindingCount> mapping_escape_buttons{};
  std::array<std::wstring, kPadBindingCount> mapping_values{};
  std::array<bool, kPadBindingCount> mapping_present{};
  std::array<bool, kPadBindingCount> mapping_modified{};
  size_t mapping_capture_index = kPadBindingCount;
  std::wstring mapping_status;
  HFONT regular_font = nullptr;
  HFONT bold_font = nullptr;
  HFONT title_font = nullptr;
  HFONT regular_paint_font = nullptr;
  HFONT bold_paint_font = nullptr;
  HFONT title_paint_font = nullptr;
  HFONT mapping_regular_font = nullptr;
  HFONT mapping_bold_font = nullptr;
  HFONT mapping_title_font = nullptr;
  HFONT mapping_regular_paint_font = nullptr;
  HFONT mapping_bold_paint_font = nullptr;
  HFONT mapping_title_paint_font = nullptr;
  HANDLE child_process = nullptr;
  ULONGLONG child_check_deadline = 0;
};

AppState* g_app = nullptr;

int ScaleForDpi(int value, UINT dpi) {
  return MulDiv(value, int(dpi ? dpi : kDefaultDpi), int(kDefaultDpi));
}

UINT GetSystemDpi();

UINT GetWindowDpi(HWND window) {
  using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
  static const auto get_dpi = reinterpret_cast<GetDpiForWindowFn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  const UINT dpi = get_dpi && window ? get_dpi(window) : GetSystemDpi();
  return dpi ? dpi : kDefaultDpi;
}

UINT GetSystemDpi() {
  using GetDpiForSystemFn = UINT(WINAPI*)();
  static const auto get_dpi = reinterpret_cast<GetDpiForSystemFn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
  const UINT dpi = get_dpi ? get_dpi() : kDefaultDpi;
  return dpi ? dpi : kDefaultDpi;
}

void EnablePerMonitorDpiAwareness() {
  using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
  static const auto set_context = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
  if (set_context) {
    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is the documented pseudo-handle -4.
    if (set_context(reinterpret_cast<HANDLE>(INT_PTR(-4)))) return;
  }
  SetProcessDPIAware();
}

BOOL AdjustWindowRectForDpi(RECT* rect, DWORD style, UINT dpi) {
  using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
  static const auto adjust_for_dpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
  if (adjust_for_dpi) return adjust_for_dpi(rect, style, FALSE, 0, dpi);
  return AdjustWindowRect(rect, style, FALSE);
}

int SetPaintDpiTransform(HDC dc, UINT dpi) {
  const int saved = SaveDC(dc);
  if (saved) {
    SetGraphicsMode(dc, GM_ADVANCED);
    const FLOAT scale = FLOAT(dpi ? dpi : kDefaultDpi) / FLOAT(kDefaultDpi);
    const XFORM transform{scale, 0.0f, 0.0f, scale, 0.0f, 0.0f};
    SetWorldTransform(dc, &transform);
  }
  return saved;
}

// The order follows the current Windows release batch invocation. Keep this
// single table synchronized with tools/windows/wwe13.bat after D1 lands.
enum class ArgumentKind { kLiteral, kFullscreen, kLogFile, kUserDataRoot };
struct LaunchArgument {
  ArgumentKind kind;
  const wchar_t* value;
};
constexpr LaunchArgument kLaunchArguments[] = {
    {ArgumentKind::kLiteral, L"--async_shader_compilation=false"},
    {ArgumentKind::kLiteral, L"--vulkan_async_skip_incomplete_frames=false"},
    {ArgumentKind::kFullscreen, nullptr},
    {ArgumentKind::kLiteral, L"--vulkan_allow_present_mode_immediate=false"},
    {ArgumentKind::kLiteral, L"--ignore_offset_for_ranged_allocations=true"},
    {ArgumentKind::kLiteral, L"--log_verbose=false"},
    {ArgumentKind::kLiteral, L"--mnk_mode=true"},
    {ArgumentKind::kLogFile, nullptr},
    {ArgumentKind::kUserDataRoot, nullptr},
    {ArgumentKind::kLiteral, L"--debug_ui=false"},
};

struct EnvironmentEntry {
  std::wstring name;
  std::wstring value;
};

std::wstring JoinPath(std::wstring_view left, std::wstring_view right) {
  if (left.empty()) return std::wstring(right);
  std::wstring result(left);
  if (result.back() != L'\\' && result.back() != L'/') result.push_back(L'\\');
  result.append(right);
  return result;
}

std::wstring ParentDirectory(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring ModulePath() {
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
  if (!length || length >= buffer.size()) return {};
  return std::wstring(buffer.data(), length);
}

std::wstring Trim(std::wstring value) {
  const size_t first = value.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) return {};
  const size_t last = value.find_last_not_of(L" \t\r\n");
  return value.substr(first, last - first + 1);
}

bool FileExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool GameFilesPresent(const std::wstring& folder) {
  return DirectoryExists(folder) && FileExists(JoinPath(folder, L"default.xex")) &&
         FileExists(JoinPath(folder, L"default.xexp"));
}

bool WriteAtomicBytes(const std::wstring& destination, const void* data, size_t size) {
  const std::wstring temp = destination + L".tmp." + std::to_wstring(GetCurrentProcessId());
  HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const auto* bytes = static_cast<const uint8_t*>(data);
  size_t offset = 0;
  bool okay = true;
  while (offset < size) {
    const DWORD chunk = DWORD(std::min<size_t>(size - offset, 1u << 20));
    DWORD written = 0;
    if (!WriteFile(file, bytes + offset, chunk, &written, nullptr) || !written) {
      okay = false;
      break;
    }
    offset += written;
  }
  if (okay) okay = FlushFileBuffers(file) != FALSE;
  CloseHandle(file);
  if (!okay) {
    DeleteFileW(temp.c_str());
    return false;
  }
  if (!MoveFileExW(temp.c_str(), destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temp.c_str());
    return false;
  }
  return true;
}

std::vector<uint8_t> BuildIniBytes(const AppState& app) {
  std::wstring text = L"[Settings]\r\nschema=7\r\nanti_aliasing=" +
                      app.settings.anti_aliasing + L"\r\nresolution=" +
                      (app.settings.resolution_explicit ? app.settings.resolution
                                                        : std::wstring(L"auto")) +
                      L"\r\nframe_rate=" +
                      (app.settings.frame_rate_explicit ? app.settings.frame_rate
                                                        : std::wstring(L"auto")) +
                      L"\r\ndisplay=" +
                      app.settings.display + L"\r\ngpu=" +
                      (app.settings.gpu_id.empty() ? L"auto" : app.settings.gpu_id) +
                      L"\r\ngpu_name=" + app.settings.gpu_name + L"\r\ngame_folder=" +
                      app.settings.game_folder + L"\r\nauto_start=" +
                      (app.settings.auto_start ? L"1\r\n" : L"0\r\n");
  // A UTF-16LE BOM makes Windows profile APIs and non-ASCII folder names unambiguous.
  std::vector<uint8_t> bytes;
  bytes.reserve(2 + text.size() * sizeof(wchar_t));
  bytes.push_back(0xFF);
  bytes.push_back(0xFE);
  for (wchar_t character : text) {
    const uint16_t unit = uint16_t(character);
    bytes.push_back(uint8_t(unit & 0xFF));
    bytes.push_back(uint8_t(unit >> 8));
  }
  return bytes;
}

bool WriteIni(const AppState& app) {
  const std::vector<uint8_t> bytes = BuildIniBytes(app);
  return WriteAtomicBytes(app.settings_path, bytes.data(), bytes.size());
}

std::wstring ReadIniValue(const std::wstring& path, const wchar_t* key) {
  std::vector<wchar_t> buffer(32768);
  const DWORD size = GetPrivateProfileStringW(kSettingsSection, key, L"", buffer.data(),
                                               DWORD(buffer.size()), path.c_str());
  return std::wstring(buffer.data(), size);
}

template <size_t N>
bool FindOption(const wchar_t* id, const auto (&options)[N], size_t& index) {
  for (size_t i = 0; i < N; ++i) {
    if (_wcsicmp(id, options[i].id) == 0) {
      index = i;
      return true;
    }
  }
  return false;
}

std::wstring FormatGpuId(uint32_t vendor_id, uint32_t device_id);
bool ParseGpuId(std::wstring_view text, uint32_t& vendor_id, uint32_t& device_id);

void LoadSettings(AppState& app) {
  app.settings.game_folder = app.default_game_folder;
  if (GetFileAttributesW(app.settings_path.c_str()) == INVALID_FILE_ATTRIBUTES) return;

  const std::wstring schema = ReadIniValue(app.settings_path, L"schema");
  const bool has_current_schema = schema == L"4" || schema == L"5" || schema == L"6" ||
                                  schema == L"7";
  app.needs_schema_upgrade = schema == L"4" || schema == L"5" || schema == L"6";
  size_t index = 0;
  if (has_current_schema) {
    std::wstring resolution = ReadIniValue(app.settings_path, L"resolution");
    if (_wcsicmp(resolution.c_str(), L"576p") == 0) resolution = L"480p";
    if (FindOption(resolution.c_str(), kResolutionOptions, index)) {
      app.settings.resolution = kResolutionOptions[index].id;
      app.settings.resolution_explicit = true;
    }
    if (schema == L"6" || schema == L"7") {
      const std::wstring anti_aliasing = ReadIniValue(app.settings_path, L"anti_aliasing");
      if (FindOption(anti_aliasing.c_str(), kAntiAliasingOptions, index)) {
        const wchar_t* selected = kAntiAliasingOptions[index].id;
        // Schema 6 wrote 2x only as the automatic default, not as a deliberate
        // player choice. Make the safe original mode the migrated default.
        if (schema == L"6" && _wcsicmp(selected, L"2x") == 0) selected = L"original";
        app.settings.anti_aliasing = selected;
      }
    }
  } else {
    const std::wstring old_internal_resolution =
        ReadIniValue(app.settings_path, L"internal_resolution");
    if (_wcsicmp(old_internal_resolution.c_str(), L"480p") == 0) {
      app.settings.resolution = L"480p";
      app.settings.resolution_explicit = true;
    } else if (_wcsicmp(old_internal_resolution.c_str(), L"576p") == 0) {
      app.settings.resolution = L"480p";
      app.settings.resolution_explicit = true;
    } else {
      const std::wstring old_resolution = ReadIniValue(app.settings_path, L"resolution");
      if (FindOption(old_resolution.c_str(), kResolutionOptions, index) &&
          (_wcsicmp(old_resolution.c_str(), L"720p") == 0 ||
           kResolutionOptions[index].scale > 0)) {
        app.settings.resolution = kResolutionOptions[index].id;
        app.settings.resolution_explicit =
            kResolutionOptions[index].scale > 0 ||
            _wcsicmp(old_internal_resolution.c_str(), L"720p") == 0;
      }
    }
  }

  const std::wstring frame_rate = ReadIniValue(app.settings_path, L"frame_rate");
  const bool has_explicit_frame_schema = schema == L"3" || has_current_schema;
  bool found_frame = false;
  for (const FrameRateOption& option : kFrameRateOptions) {
    if (_wcsicmp(frame_rate.c_str(), option.id) == 0 && option.enabled) {
      app.settings.frame_rate = option.id;
      found_frame = true;
      // Version 2 launchers always wrote Classic when they had not stored a
      // user choice, and then applied a non-editable integrated-GPU override.
      // Treat that legacy default as automatic so the new 480p/30 defaults
      // apply; version 3 distinguishes an explicit Classic selection.
      app.settings.frame_rate_explicit =
          has_explicit_frame_schema || _wcsicmp(option.id, L"classic") != 0;
      break;
    }
  }
  if (!found_frame) app.settings.frame_rate = L"classic";

  const std::wstring display = ReadIniValue(app.settings_path, L"display");
  if (FindOption(display.c_str(), kDisplayOptions, index)) {
    app.settings.display = kDisplayOptions[index].id;
  }

  const std::wstring gpu = ReadIniValue(app.settings_path, L"gpu");
  if (!gpu.empty()) {
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    if (ParseGpuId(gpu, vendor_id, device_id)) {
      app.settings.gpu_id = FormatGpuId(vendor_id, device_id);
      app.settings.gpu_name = ReadIniValue(app.settings_path, L"gpu_name");
    } else {
      wchar_t* end = nullptr;
      const long legacy_index = wcstol(gpu.c_str(), &end, 10);
      if (end && end != gpu.c_str() && *end == L'\0' && legacy_index >= -1 &&
          legacy_index <= 4096) {
        // Old numeric values depended on the launcher's Vulkan enumeration order.
        // Migrate them to Automatic rather than selecting a possibly different GPU.
        app.legacy_gpu_setting = true;
      }
    }
  }

  const std::wstring folder = Trim(ReadIniValue(app.settings_path, L"game_folder"));
  if (!folder.empty()) app.settings.game_folder = folder;

  if (schema == L"5" || schema == L"6" || schema == L"7") {
    const std::wstring auto_start = ReadIniValue(app.settings_path, L"auto_start");
    app.settings.auto_start = _wcsicmp(auto_start.c_str(), L"1") == 0 ||
                              _wcsicmp(auto_start.c_str(), L"true") == 0;
  }
}

// Minimal Vulkan 1.0 ABI declarations. vulkan-1.dll is loaded dynamically;
// the launcher deliberately has no Vulkan SDK or import-library dependency.
using VkResult = int32_t;
using VkInstance = void*;
using VkPhysicalDevice = void*;
struct VkApplicationInfoMinimal {
  uint32_t sType;
  const void* pNext;
  const char* pApplicationName;
  uint32_t applicationVersion;
  const char* pEngineName;
  uint32_t engineVersion;
  uint32_t apiVersion;
};
struct VkInstanceCreateInfoMinimal {
  uint32_t sType;
  const void* pNext;
  uint32_t flags;
  const VkApplicationInfoMinimal* pApplicationInfo;
  uint32_t enabledLayerCount;
  const char* const* ppEnabledLayerNames;
  uint32_t enabledExtensionCount;
  const char* const* ppEnabledExtensionNames;
};
struct VkPhysicalDevicePropertiesPrefix {
  uint32_t apiVersion;
  uint32_t driverVersion;
  uint32_t vendorID;
  uint32_t deviceID;
  uint32_t deviceType;
  char deviceName[256];
  uint8_t pipelineCacheUUID[16];
};
struct VkPhysicalDeviceFeaturesMinimal {
  uint32_t values[kVkPhysicalDeviceFeatureCount];
};
using VkCreateInstanceFn = VkResult(WINAPI*)(const VkInstanceCreateInfoMinimal*, const void*, VkInstance*);
using VkDestroyInstanceFn = void(WINAPI*)(VkInstance, const void*);
using VkEnumeratePhysicalDevicesFn = VkResult(WINAPI*)(VkInstance, uint32_t*, VkPhysicalDevice*);
using VkGetPhysicalDevicePropertiesFn = void(WINAPI*)(VkPhysicalDevice, void*);
using VkGetPhysicalDeviceFeaturesFn = void(WINAPI*)(VkPhysicalDevice, void*);

std::wstring Utf8ToWide(const char* text) {
  if (!text || !*text) return {};
  const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
  if (needed <= 1) return {};
  std::wstring result(size_t(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), needed);
  result.pop_back();
  return result;
}

std::wstring FormatGpuId(uint32_t vendor_id, uint32_t device_id) {
  constexpr wchar_t kHexDigits[] = L"0123456789ABCDEF";
  auto format_component = [&](uint32_t value) {
    wchar_t digits[8]{};
    for (size_t i = 0; i < std::size(digits); ++i) {
      digits[std::size(digits) - i - 1] = kHexDigits[(value >> (i * 4)) & 0xF];
    }
    size_t first = 0;
    while (first < 4 && digits[first] == L'0') ++first;
    return std::wstring(digits + first, std::size(digits) - first);
  };
  return format_component(vendor_id) + L":" + format_component(device_id);
}

bool ParseGpuId(std::wstring_view text, uint32_t& vendor_id, uint32_t& device_id) {
  const size_t separator = text.find(L':');
  if (separator == std::wstring_view::npos ||
      text.find(L':', separator + 1) != std::wstring_view::npos || separator < 4 ||
      separator > 8 || text.size() - separator - 1 < 4 || text.size() - separator - 1 > 8) {
    return false;
  }
  auto parse_component = [](std::wstring_view component, uint32_t& value) {
    value = 0;
    if (component.size() < 4 || component.size() > 8) return false;
    for (wchar_t character : component) {
      value <<= 4;
      if (character >= L'0' && character <= L'9') {
        value |= uint32_t(character - L'0');
      } else if (character >= L'a' && character <= L'f') {
        value |= uint32_t(character - L'a' + 10);
      } else if (character >= L'A' && character <= L'F') {
        value |= uint32_t(character - L'A' + 10);
      } else {
        return false;
      }
    }
    return true;
  };
  return parse_component(text.substr(0, separator), vendor_id) &&
         parse_component(text.substr(separator + 1), device_id);
}

std::wstring GpuId(const GpuOption& gpu) {
  return FormatGpuId(gpu.vendor_id, gpu.device_id);
}

void EnumerateVulkanGpus(AppState& app) {
  HMODULE library = LoadLibraryW(L"vulkan-1.dll");
  if (!library) {
    app.gpu_error = L"Vulkan GPU names are unavailable; Automatic is still available.";
    return;
  }
  auto create_instance = reinterpret_cast<VkCreateInstanceFn>(GetProcAddress(library, "vkCreateInstance"));
  auto destroy_instance = reinterpret_cast<VkDestroyInstanceFn>(GetProcAddress(library, "vkDestroyInstance"));
  auto enumerate_devices = reinterpret_cast<VkEnumeratePhysicalDevicesFn>(
      GetProcAddress(library, "vkEnumeratePhysicalDevices"));
  auto get_properties = reinterpret_cast<VkGetPhysicalDevicePropertiesFn>(
      GetProcAddress(library, "vkGetPhysicalDeviceProperties"));
  auto get_features = reinterpret_cast<VkGetPhysicalDeviceFeaturesFn>(
      GetProcAddress(library, "vkGetPhysicalDeviceFeatures"));
  if (!create_instance || !destroy_instance || !enumerate_devices || !get_properties ||
      !get_features) {
    app.gpu_error = L"Vulkan GPU names are unavailable; Automatic is still available.";
    FreeLibrary(library);
    return;
  }

  constexpr char kApplicationName[] = "WWE '13";
  VkApplicationInfoMinimal application_info = {0, nullptr, kApplicationName, 1,
                                                kApplicationName, 1, 1u << 22};
  VkInstanceCreateInfoMinimal create_info = {1, nullptr, 0, &application_info, 0, nullptr, 0,
                                              nullptr};
  VkInstance instance = nullptr;
  if (create_instance(&create_info, nullptr, &instance) != 0 || !instance) {
    app.gpu_error = L"Vulkan GPU names are unavailable; Automatic is still available.";
    FreeLibrary(library);
    return;
  }

  uint32_t count = 0;
  VkResult result = enumerate_devices(instance, &count, nullptr);
  if (result == 0 && count) {
    std::vector<VkPhysicalDevice> devices(count);
    result = enumerate_devices(instance, &count, devices.data());
    if (result == 0 || result == 5) {
      devices.resize(count);
      for (size_t i = 0; i < devices.size(); ++i) {
        alignas(16) std::array<uint8_t, 4096> property_bytes{};
        get_properties(devices[i], property_bytes.data());
        const auto* properties = reinterpret_cast<const VkPhysicalDevicePropertiesPrefix*>(
            property_bytes.data());
        std::wstring name = Utf8ToWide(properties->deviceName);
        if (name.empty()) name = L"Vulkan GPU";
        VkPhysicalDeviceFeaturesMinimal features{};
        get_features(devices[i], &features);
        // These are the four feature scores VulkanProvider::Create uses by default.
        const unsigned feature_score = unsigned(features.values[4] != 0) +
                                       unsigned(features.values[26] != 0) +
                                       unsigned(features.values[25] != 0) +
                                       unsigned(features.values[13] != 0);
        app.gpus.push_back({std::move(name), properties->vendorID, properties->deviceID,
                            properties->deviceType, feature_score});
      }
    }
  }
  destroy_instance(instance, nullptr);
  FreeLibrary(library);
  if (app.gpus.empty()) {
    app.gpu_error = L"No Vulkan GPUs were found; Automatic is still available.";
    return;
  }

  // ReXGlue's default automatic ordering is stable: discrete devices first,
  // then the four-feature score, with Vulkan enumeration order as the tie-break.
  // Automatic is left to the SDK; this identity is used only to predict whether
  // the SDK's top-scored device is integrated and therefore needs the safety lock.
  std::vector<const GpuOption*> ordered;
  ordered.reserve(app.gpus.size());
  for (const GpuOption& gpu : app.gpus) ordered.push_back(&gpu);
  std::stable_sort(ordered.begin(), ordered.end(), [](const GpuOption* a, const GpuOption* b) {
    const bool a_discrete = a->device_type == kVkPhysicalDeviceTypeDiscreteGpu;
    const bool b_discrete = b->device_type == kVkPhysicalDeviceTypeDiscreteGpu;
    if (a_discrete != b_discrete) return a_discrete;
    return a->auto_feature_score > b->auto_feature_score;
  });
  if (!ordered.empty()) app.auto_gpu_id = GpuId(*ordered.front());
}

const wchar_t* GpuTypeLabel(uint32_t device_type) {
  switch (device_type) {
    case kVkPhysicalDeviceTypeIntegratedGpu:
      return L"integrated";
    case kVkPhysicalDeviceTypeDiscreteGpu:
      return L"discrete";
    case 3:
      return L"virtual";
    case 4:
      return L"CPU";
    default:
      return L"other";
  }
}

GpuOption* FindGpuById(AppState& app, std::wstring_view id) {
  const std::wstring wanted(id);
  for (GpuOption& gpu : app.gpus) {
    const std::wstring gpu_id = GpuId(gpu);
    if (_wcsicmp(gpu_id.c_str(), wanted.c_str()) == 0) return &gpu;
  }
  return nullptr;
}

const GpuOption* FindGpuById(const AppState& app, std::wstring_view id) {
  const std::wstring wanted(id);
  for (const GpuOption& gpu : app.gpus) {
    const std::wstring gpu_id = GpuId(gpu);
    if (_wcsicmp(gpu_id.c_str(), wanted.c_str()) == 0) return &gpu;
  }
  return nullptr;
}

const GpuOption* GetActiveGpu(const AppState& app) {
  if (!app.settings.gpu_id.empty()) return FindGpuById(app, app.settings.gpu_id);
  if (!app.simulated_auto_gpu_id.empty()) return FindGpuById(app, app.simulated_auto_gpu_id);
  return FindGpuById(app, app.auto_gpu_id);
}

bool IsIntegratedGpuMode(const AppState& app) {
  const GpuOption* gpu = GetActiveGpu(app);
  return gpu && gpu->device_type == kVkPhysicalDeviceTypeIntegratedGpu;
}

Settings GetEffectiveSettings(const AppState& app) {
  Settings effective = app.settings;
  if (IsIntegratedGpuMode(app)) {
    if (!app.settings.resolution_explicit) {
      effective.resolution = L"480p";
    }
    if (!app.settings.frame_rate_explicit) {
      effective.frame_rate = L"lock-30";
    }
  }
  return effective;
}

bool SelectedResolutionIs1440(const Settings& settings) {
  return _wcsicmp(settings.resolution.c_str(), L"1440p") == 0;
}

const ResolutionOption* GetSelectedResolution(const Settings& settings) {
  for (const ResolutionOption& option : kResolutionOptions) {
    if (_wcsicmp(settings.resolution.c_str(), option.id) == 0) return &option;
  }
  for (const ResolutionOption& option : kResolutionOptions) {
    if (_wcsicmp(option.id, L"720p") == 0) return &option;
  }
  return &kResolutionOptions[0];
}

const FrameRateOption* GetSelectedFrameRate(const Settings& settings) {
  for (const FrameRateOption& option : kFrameRateOptions) {
    if (option.enabled && _wcsicmp(settings.frame_rate.c_str(), option.id) == 0) return &option;
  }
  return &kFrameRateOptions[0];
}

const AntiAliasingOption* GetSelectedAntiAliasing(const Settings& settings) {
  for (const AntiAliasingOption& option : kAntiAliasingOptions) {
    if (_wcsicmp(settings.anti_aliasing.c_str(), option.id) == 0) return &option;
  }
  return &kAntiAliasingOptions[0];
}

std::wstring Timestamp() {
  SYSTEMTIME time{};
  GetLocalTime(&time);
  wchar_t value[32]{};
  swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u", time.wYear, time.wMonth, time.wDay,
             time.wHour, time.wMinute, time.wSecond);
  return value;
}

std::wstring GetEnvironmentValue(const wchar_t* name) {
  const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
  if (!required) return {};
  std::vector<wchar_t> value(required);
  const DWORD length = GetEnvironmentVariableW(name, value.data(), required);
  if (!length || length >= required) return {};
  return std::wstring(value.data(), length);
}

enum class TextEncoding { kUtf8, kAnsi, kUtf16Le, kUtf16Be };

bool DecodeText(const std::vector<uint8_t>& bytes, std::wstring& text,
                TextEncoding& encoding, bool& has_bom) {
  size_t offset = 0;
  has_bom = false;
  if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
    encoding = TextEncoding::kUtf16Le;
    has_bom = true;
    offset = 2;
    if ((bytes.size() - offset) % 2 != 0) return false;
    text.reserve((bytes.size() - offset) / 2);
    for (size_t i = offset; i < bytes.size(); i += 2) {
      text.push_back(wchar_t(uint16_t(bytes[i]) | (uint16_t(bytes[i + 1]) << 8)));
    }
    return true;
  }
  if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
    encoding = TextEncoding::kUtf16Be;
    has_bom = true;
    offset = 2;
    if ((bytes.size() - offset) % 2 != 0) return false;
    text.reserve((bytes.size() - offset) / 2);
    for (size_t i = offset; i < bytes.size(); i += 2) {
      text.push_back(wchar_t((uint16_t(bytes[i]) << 8) | uint16_t(bytes[i + 1])));
    }
    return true;
  }
  if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
    has_bom = true;
    offset = 3;
  }
  if (bytes.size() - offset > size_t(INT_MAX)) return false;
  const char* input = bytes.size() == offset
                          ? ""
                          : reinterpret_cast<const char*>(bytes.data() + offset);
  const int input_length = int(bytes.size() - offset);
  int needed = input_length ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input,
                                                  input_length, nullptr, 0)
                            : 0;
  encoding = TextEncoding::kUtf8;
  if (input_length && !needed) {
    needed = MultiByteToWideChar(CP_ACP, 0, input, input_length, nullptr, 0);
    encoding = TextEncoding::kAnsi;
    if (!needed) return false;
  }
  text.resize(size_t(needed));
  if (needed) {
    const UINT code_page = encoding == TextEncoding::kUtf8 ? CP_UTF8 : CP_ACP;
    const DWORD flags = encoding == TextEncoding::kUtf8 ? MB_ERR_INVALID_CHARS : 0;
    if (!MultiByteToWideChar(code_page, flags, input, input_length, text.data(), needed)) {
      return false;
    }
  }
  return true;
}

bool EncodeText(const std::wstring& text, TextEncoding encoding, bool has_bom,
                std::vector<uint8_t>& bytes) {
  bytes.clear();
  if (encoding == TextEncoding::kUtf16Le || encoding == TextEncoding::kUtf16Be) {
    if (has_bom) {
      bytes.push_back(encoding == TextEncoding::kUtf16Le ? 0xFF : 0xFE);
      bytes.push_back(encoding == TextEncoding::kUtf16Le ? 0xFE : 0xFF);
    }
    bytes.reserve(bytes.size() + text.size() * 2);
    for (wchar_t character : text) {
      const uint16_t unit = uint16_t(character);
      if (encoding == TextEncoding::kUtf16Le) {
        bytes.push_back(uint8_t(unit & 0xFF));
        bytes.push_back(uint8_t(unit >> 8));
      } else {
        bytes.push_back(uint8_t(unit >> 8));
        bytes.push_back(uint8_t(unit & 0xFF));
      }
    }
    return true;
  }
  if (text.size() > size_t(INT_MAX)) return false;
  if (has_bom && encoding == TextEncoding::kUtf8) {
    bytes.insert(bytes.end(), {0xEF, 0xBB, 0xBF});
  }
  if (text.empty()) return true;
  const UINT code_page = encoding == TextEncoding::kUtf8 ? CP_UTF8 : CP_ACP;
  const int needed = WideCharToMultiByte(code_page, 0, text.data(), int(text.size()), nullptr, 0,
                                         nullptr, nullptr);
  if (!needed) return false;
  const size_t old_size = bytes.size();
  bytes.resize(old_size + size_t(needed));
  if (!WideCharToMultiByte(code_page, 0, text.data(), int(text.size()),
                           reinterpret_cast<char*>(bytes.data() + old_size), needed, nullptr,
                           nullptr)) {
    return false;
  }
  return true;
}

void ReplaceAllCaseInsensitive(std::wstring& text, const std::wstring& value,
                               const wchar_t* marker) {
  if (value.empty()) return;
  const size_t marker_length = wcslen(marker);
  size_t position = 0;
  while (position + value.size() <= text.size()) {
    if (CompareStringOrdinal(text.data() + position, int(value.size()), value.data(),
                             int(value.size()), TRUE) == CSTR_EQUAL) {
      text.replace(position, value.size(), marker, marker_length);
      position += marker_length;
    } else {
      ++position;
    }
  }
}

bool ScrubTextFile(std::vector<uint8_t>& bytes, const std::wstring& profile_path,
                   const std::wstring& user_name, const std::wstring& computer_name) {
  std::wstring text;
  TextEncoding encoding = TextEncoding::kUtf8;
  bool has_bom = false;
  if (!DecodeText(bytes, text, encoding, has_bom)) return false;
  if (!profile_path.empty()) {
    ReplaceAllCaseInsensitive(text, profile_path, L"\xE000");
    std::wstring slash_path = profile_path;
    std::replace(slash_path.begin(), slash_path.end(), L'\\', L'/');
    if (slash_path != profile_path) {
      ReplaceAllCaseInsensitive(text, slash_path, L"\xE000");
    }
  }
  ReplaceAllCaseInsensitive(text, user_name, L"\xE001");
  ReplaceAllCaseInsensitive(text, computer_name, L"\xE002");
  ReplaceAllCaseInsensitive(text, L"\xE000", L"<user>");
  ReplaceAllCaseInsensitive(text, L"\xE001", L"<user>");
  ReplaceAllCaseInsensitive(text, L"\xE002", L"<computer>");
  return EncodeText(text, encoding, has_bom, bytes);
}

std::wstring Sha256File(const std::wstring& path) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
    return L"unavailable";
  }
  DWORD object_length = 0;
  DWORD result_length = 0;
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length),
                        &result_length, 0) < 0 || !object_length) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return L"unavailable";
  }
  std::vector<UCHAR> object_buffer(object_length);
  if (BCryptCreateHash(algorithm, &hash, object_buffer.data(), object_length, nullptr, 0, 0) < 0) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return L"unavailable";
  }

  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  bool okay = file != INVALID_HANDLE_VALUE;
  std::array<UCHAR, 1 << 16> chunk{};
  while (okay) {
    DWORD read = 0;
    if (!ReadFile(file, chunk.data(), DWORD(chunk.size()), &read, nullptr)) {
      okay = false;
      break;
    }
    if (!read) break;
    if (BCryptHashData(hash, chunk.data(), read, 0) < 0) okay = false;
  }
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  std::array<UCHAR, 32> digest{};
  if (okay && BCryptFinishHash(hash, digest.data(), DWORD(digest.size()), 0) < 0) okay = false;
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!okay) return L"unavailable";
  constexpr wchar_t hex[] = L"0123456789abcdef";
  std::wstring result;
  result.reserve(digest.size() * 2);
  for (UCHAR byte : digest) {
    result.push_back(hex[byte >> 4]);
    result.push_back(hex[byte & 0xF]);
  }
  return result;
}

std::wstring WindowsVersion() {
  using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto get_version = ntdll ? reinterpret_cast<RtlGetVersionFunction>(
                                       GetProcAddress(ntdll, "RtlGetVersion"))
                                 : nullptr;
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  if (!get_version || get_version(&version) != 0) return L"unavailable";
  return L"Windows " + std::to_wstring(version.dwMajorVersion) + L"." +
         std::to_wstring(version.dwMinorVersion) + L" (build " +
         std::to_wstring(version.dwBuildNumber) + L")";
}

uint32_t Crc32(const std::vector<uint8_t>& data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint8_t byte : data) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

void AppendU16(std::vector<uint8_t>& bytes, uint16_t value) {
  bytes.push_back(uint8_t(value & 0xFF));
  bytes.push_back(uint8_t(value >> 8));
}

void AppendU32(std::vector<uint8_t>& bytes, uint32_t value) {
  AppendU16(bytes, uint16_t(value & 0xFFFF));
  AppendU16(bytes, uint16_t(value >> 16));
}

struct ZipEntry {
  std::string name;
  std::vector<uint8_t> data;
  uint32_t crc = 0;
  uint32_t local_offset = 0;
};

bool AddZipEntry(std::vector<ZipEntry>& entries, const std::wstring& name,
                 std::vector<uint8_t> data) {
  std::vector<uint8_t> encoded_name;
  if (!EncodeText(name, TextEncoding::kUtf8, false, encoded_name) ||
      encoded_name.size() > UINT16_MAX || data.size() > UINT32_MAX) {
    return false;
  }
  ZipEntry entry;
  entry.name.assign(encoded_name.begin(), encoded_name.end());
  entry.data = std::move(data);
  entry.crc = Crc32(entry.data);
  entries.push_back(std::move(entry));
  return true;
}

bool BuildZipArchive(std::vector<ZipEntry>& entries, std::vector<uint8_t>& archive) {
  if (entries.empty() || entries.size() > UINT16_MAX) return false;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  const uint16_t dos_time = uint16_t((now.wHour << 11) | (now.wMinute << 5) | (now.wSecond / 2));
  const uint16_t dos_date = uint16_t(((now.wYear < 1980 ? 0 : now.wYear - 1980) << 9) |
                                     (now.wMonth << 5) | now.wDay);
  archive.clear();
  for (ZipEntry& entry : entries) {
    if (archive.size() > UINT32_MAX || entry.data.size() > UINT32_MAX ||
        entry.name.size() > UINT16_MAX) {
      return false;
    }
    entry.local_offset = uint32_t(archive.size());
    AppendU32(archive, 0x04034B50u);
    AppendU16(archive, 20);
    AppendU16(archive, 0x0800);  // UTF-8 file names.
    AppendU16(archive, 0);       // Store without compression.
    AppendU16(archive, dos_time);
    AppendU16(archive, dos_date);
    AppendU32(archive, entry.crc);
    AppendU32(archive, uint32_t(entry.data.size()));
    AppendU32(archive, uint32_t(entry.data.size()));
    AppendU16(archive, uint16_t(entry.name.size()));
    AppendU16(archive, 0);
    archive.insert(archive.end(), entry.name.begin(), entry.name.end());
    archive.insert(archive.end(), entry.data.begin(), entry.data.end());
  }
  if (archive.size() > UINT32_MAX) return false;
  const uint32_t central_offset = uint32_t(archive.size());
  for (const ZipEntry& entry : entries) {
    AppendU32(archive, 0x02014B50u);
    AppendU16(archive, 20);
    AppendU16(archive, 20);
    AppendU16(archive, 0x0800);
    AppendU16(archive, 0);
    AppendU16(archive, dos_time);
    AppendU16(archive, dos_date);
    AppendU32(archive, entry.crc);
    AppendU32(archive, uint32_t(entry.data.size()));
    AppendU32(archive, uint32_t(entry.data.size()));
    AppendU16(archive, uint16_t(entry.name.size()));
    AppendU16(archive, 0);
    AppendU16(archive, 0);
    AppendU16(archive, 0);
    AppendU16(archive, 0);
    AppendU32(archive, 0);
    AppendU32(archive, entry.local_offset);
    archive.insert(archive.end(), entry.name.begin(), entry.name.end());
    if (archive.size() > UINT32_MAX) return false;
  }
  const uint32_t central_size = uint32_t(archive.size()) - central_offset;
  AppendU32(archive, 0x06054B50u);
  AppendU16(archive, 0);
  AppendU16(archive, 0);
  AppendU16(archive, uint16_t(entries.size()));
  AppendU16(archive, uint16_t(entries.size()));
  AppendU32(archive, central_size);
  AppendU32(archive, central_offset);
  AppendU16(archive, 0);
  return archive.size() <= size_t(UINT32_MAX) + 22;
}

struct GameLogFile {
  std::wstring name;
  FILETIME modified{};
  uint64_t size = 0;
};

bool FindGameLogs(const AppState& app, std::vector<GameLogFile>& logs) {
  const std::wstring pattern = JoinPath(app.logs_dir, L"wwe13-*.log");
  WIN32_FIND_DATAW data{};
  HANDLE search = FindFirstFileW(pattern.c_str(), &data);
  if (search == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
  }
  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    logs.push_back({data.cFileName, data.ftLastWriteTime,
                    (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow});
  } while (FindNextFileW(search, &data));
  const DWORD error = GetLastError();
  FindClose(search);
  if (error != ERROR_NO_MORE_FILES) return false;
  std::sort(logs.begin(), logs.end(), [](const GameLogFile& a, const GameLogFile& b) {
    const int time_order = CompareFileTime(&a.modified, &b.modified);
    if (time_order != 0) return time_order > 0;
    return _wcsicmp(a.name.c_str(), b.name.c_str()) > 0;
  });
  return true;
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& bytes) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
      uint64_t(size.QuadPart) > UINT32_MAX) {
    CloseHandle(file);
    return false;
  }
  bytes.resize(size_t(size.QuadPart));
  size_t offset = 0;
  bool okay = true;
  while (offset < bytes.size()) {
    const DWORD wanted = DWORD(std::min<size_t>(bytes.size() - offset, 1u << 20));
    DWORD read = 0;
    if (!ReadFile(file, bytes.data() + offset, wanted, &read, nullptr) || !read) {
      okay = false;
      break;
    }
    offset += read;
  }
  CloseHandle(file);
  return okay;
}

struct BindingLine {
  size_t index = kPadBindingCount;
  size_t value_begin = 0;
  size_t value_end = 0;
  std::string value;
  bool quoted = false;
};

size_t FindPadBinding(std::string_view name) {
  for (size_t i = 0; i < kPadBindingCount; ++i) {
    if (name == kPadBindings[i].cvar) return i;
  }
  return kPadBindingCount;
}

bool IsTomlSpace(char c) { return c == ' ' || c == '\t'; }

bool IsTomlTableHeader(std::string_view line) {
  size_t cursor = 0;
  if (line.size() >= 3 && uint8_t(line[0]) == 0xEF && uint8_t(line[1]) == 0xBB &&
      uint8_t(line[2]) == 0xBF) {
    cursor = 3;
  }
  while (cursor < line.size() && IsTomlSpace(line[cursor])) ++cursor;
  return cursor < line.size() && line[cursor] == '[';
}

BindingLine ParseBindingLine(std::string_view line) {
  BindingLine result;
  size_t cursor = 0;
  if (line.size() >= 3 && uint8_t(line[0]) == 0xEF && uint8_t(line[1]) == 0xBB &&
      uint8_t(line[2]) == 0xBF) {
    cursor = 3;
  }
  while (cursor < line.size() && IsTomlSpace(line[cursor])) ++cursor;
  const size_t key_begin = cursor;
  while (cursor < line.size() &&
         ((line[cursor] >= 'a' && line[cursor] <= 'z') ||
          (line[cursor] >= 'A' && line[cursor] <= 'Z') ||
          (line[cursor] >= '0' && line[cursor] <= '9') || line[cursor] == '_')) {
    ++cursor;
  }
  result.index = FindPadBinding(line.substr(key_begin, cursor - key_begin));
  if (result.index == kPadBindingCount) return result;
  while (cursor < line.size() && IsTomlSpace(line[cursor])) ++cursor;
  if (cursor == line.size() || line[cursor++] != '=') {
    result.index = kPadBindingCount;
    return result;
  }
  while (cursor < line.size() && IsTomlSpace(line[cursor])) ++cursor;
  result.value_begin = cursor;
  size_t fallback_end = line.find('#', cursor);
  if (fallback_end == std::string_view::npos) fallback_end = line.size();
  while (fallback_end > cursor && IsTomlSpace(line[fallback_end - 1])) --fallback_end;
  result.value_end = fallback_end;
  if (cursor < line.size() && (line[cursor] == '"' || line[cursor] == '\'')) {
    const char quote = line[cursor++];
    const size_t text_begin = cursor;
    bool escaped = false;
    while (cursor < line.size()) {
      const char character = line[cursor];
      if (escaped) {
        if (character == quote || character == '\\') result.value.push_back(character);
        else {
          result.value.clear();
          return result;
        }
        escaped = false;
        ++cursor;
      } else if (character == '\\' && quote == '"') {
        escaped = true;
        ++cursor;
      } else if (character == quote) {
        result.value_end = cursor + 1;
        result.quoted = true;
        if (result.value.empty() && cursor > text_begin) {
          result.value.assign(line.substr(text_begin, cursor - text_begin));
        }
        return result;
      } else {
        result.value.push_back(character);
        ++cursor;
      }
    }
    result.value.clear();
    return result;
  }

  size_t end = line.find('#', cursor);
  if (end == std::string_view::npos) end = line.size();
  while (end > cursor && IsTomlSpace(line[end - 1])) --end;
  result.value_end = end;
  result.value.assign(line.substr(cursor, end - cursor));
  return result;
}

bool WideAscii(const std::wstring& value, std::string& output) {
  output.clear();
  output.reserve(value.size());
  for (wchar_t character : value) {
    if (uint32_t(character) > 0x7F) return false;
    output.push_back(char(character));
  }
  return true;
}

bool LoadMappingValues(AppState& app, std::wstring& error) {
  for (size_t i = 0; i < kPadBindingCount; ++i) {
    app.mapping_values[i] = Utf8ToWide(kPadBindings[i].default_key);
    app.mapping_present[i] = false;
    app.mapping_modified[i] = false;
  }
  const std::wstring path = JoinPath(app.exe_dir, L"wwe13.toml");
  if (!FileExists(path)) return true;
  std::vector<uint8_t> bytes;
  if (!ReadFileBytes(path, bytes)) {
    error = L"The launcher could not read wwe13.toml.";
    return false;
  }
  const std::string text(bytes.begin(), bytes.end());
  size_t line_begin = 0;
  bool root_table = true;
  while (line_begin < text.size()) {
    size_t line_end = text.find('\n', line_begin);
    if (line_end == std::string::npos) line_end = text.size();
    size_t content_end = line_end;
    if (content_end > line_begin && text[content_end - 1] == '\r') --content_end;
    const std::string_view line(text.data() + line_begin, content_end - line_begin);
    if (IsTomlTableHeader(line)) root_table = false;
    const BindingLine parsed = root_table ? ParseBindingLine(line) : BindingLine{};
    if (parsed.index < kPadBindingCount && parsed.quoted) {
      app.mapping_values[parsed.index] = Utf8ToWide(parsed.value.c_str());
      app.mapping_present[parsed.index] = true;
    }
    line_begin = line_end == text.size() ? text.size() : line_end + 1;
  }
  return true;
}

bool SaveMappingValues(const AppState& app, std::wstring& error) {
  const std::wstring path = JoinPath(app.exe_dir, L"wwe13.toml");
  std::vector<uint8_t> old_bytes;
  const bool existed = FileExists(path);
  if (existed && !ReadFileBytes(path, old_bytes)) {
    error = L"The launcher could not read the current wwe13.toml.";
    return false;
  }
  std::string original(old_bytes.begin(), old_bytes.end());
  std::array<std::string, kPadBindingCount> values{};
  for (size_t i = 0; i < kPadBindingCount; ++i) {
    const bool will_write = app.mapping_modified[i] ||
                            (!app.mapping_present[i] &&
                             app.mapping_values[i] != Utf8ToWide(kPadBindings[i].default_key));
    if (will_write &&
        (!WideAscii(app.mapping_values[i], values[i]) ||
         (!app.mapping_values[i].empty() && !FindKeyName(app.mapping_values[i])))) {
      error = L"A key name is not supported by the game and was not saved.";
      return false;
    }
    if (!will_write && !WideAscii(app.mapping_values[i], values[i])) {
      values[i] = kPadBindings[i].default_key;
    }
  }

  std::array<bool, kPadBindingCount> found{};
  std::string merged;
  merged.reserve(original.size() + 512);
  size_t line_begin = 0;
  size_t insertion_position = std::string::npos;
  bool root_table = true;
  while (line_begin < original.size()) {
    size_t line_end = original.find('\n', line_begin);
    if (line_end == std::string::npos) line_end = original.size();
    size_t content_end = line_end;
    if (content_end > line_begin && original[content_end - 1] == '\r') --content_end;
    const std::string_view line(original.data() + line_begin, content_end - line_begin);
    if (root_table && IsTomlTableHeader(line)) {
      root_table = false;
      insertion_position = merged.size();
    }
    const BindingLine parsed = root_table ? ParseBindingLine(line) : BindingLine{};
    if (parsed.index < kPadBindingCount) {
      found[parsed.index] = true;
      if (app.mapping_modified[parsed.index]) {
        merged.append(line.substr(0, parsed.value_begin));
        merged.push_back('"');
        merged.append(values[parsed.index]);
        merged.push_back('"');
        if (parsed.value_end >= parsed.value_begin && parsed.value_end <= line.size()) {
          merged.append(line.substr(parsed.value_end));
        }
      } else {
        merged.append(line);
      }
    } else {
      merged.append(line);
    }
    if (line_end < original.size()) {
      merged.append(original.data() + content_end, line_end + 1 - content_end);
    }
    line_begin = line_end == original.size() ? original.size() : line_end + 1;
  }

  std::string newline = "\n";
  const size_t first_newline = original.find('\n');
  if (first_newline != std::string::npos && first_newline > 0 &&
      original[first_newline - 1] == '\r') {
    newline = "\r\n";
  }
  if (insertion_position == std::string::npos) insertion_position = merged.size();
  std::string additions;
  for (size_t i = 0; i < kPadBindingCount; ++i) {
    const bool non_default = values[i] != kPadBindings[i].default_key;
    if (found[i] || (!app.mapping_present[i] && !non_default)) continue;
    if (additions.empty() && insertion_position > 0 && merged[insertion_position - 1] != '\n') {
      additions += newline;
    }
    additions += kPadBindings[i].cvar;
    additions += " = \"";
    additions += values[i];
    additions += "\"";
    additions += newline;
  }
  merged.insert(insertion_position, additions);

  if (merged == original) return true;
  if (existed) {
    const std::wstring backup = path + L".bak";
    if (!WriteAtomicBytes(backup, old_bytes.data(), old_bytes.size())) {
      error = L"The previous wwe13.toml could not be backed up; nothing was saved.";
      return false;
    }
  }
  if (!WriteAtomicBytes(path, merged.data(), merged.size())) {
    error = L"The launcher could not atomically save wwe13.toml.";
    return false;
  }
  return true;
}

void KeepNewestGameLogs(const AppState& app) {
  std::vector<GameLogFile> logs;
  if (!FindGameLogs(app, logs)) return;
  for (size_t i = 10; i < logs.size(); ++i) {
    DeleteFileW(JoinPath(app.logs_dir, logs[i].name).c_str());
  }
}

bool WriteNewFile(const std::wstring& path, const std::vector<uint8_t>& bytes) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  size_t offset = 0;
  bool okay = true;
  while (offset < bytes.size()) {
    const DWORD wanted = DWORD(std::min<size_t>(bytes.size() - offset, 1u << 20));
    DWORD written = 0;
    if (!WriteFile(file, bytes.data() + offset, wanted, &written, nullptr) || !written) {
      okay = false;
      break;
    }
    offset += written;
  }
  if (okay) okay = FlushFileBuffers(file) != FALSE;
  const DWORD error = okay ? ERROR_SUCCESS : GetLastError();
  CloseHandle(file);
  if (!okay) {
    DeleteFileW(path.c_str());
    SetLastError(error);
    return false;
  }
  return true;
}

bool WriteArchiveToFolder(const std::wstring& folder, const std::vector<uint8_t>& archive,
                          std::wstring& result_path) {
  if (!DirectoryExists(folder)) return false;
  const std::wstring base = L"wwe13-bug-report-" + Timestamp();
  for (unsigned suffix = 0; suffix < 100; ++suffix) {
    std::wstring filename = base;
    if (suffix) filename += L"-" + std::to_wstring(suffix + 1);
    filename += L".zip";
    const std::wstring path = JoinPath(folder, filename);
    if (WriteNewFile(path, archive)) {
      result_path = path;
      return true;
    }
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) return false;
  }
  return false;
}

std::wstring DesktopDirectory() {
  PWSTR desktop = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktop)) ||
      !desktop) {
    return {};
  }
  std::wstring path(desktop);
  CoTaskMemFree(desktop);
  return path;
}

std::wstring BuildReportInfo(const AppState& app, const std::vector<GameLogFile>& logs,
                             size_t included_log_count) {
  const Settings effective = GetEffectiveSettings(app);
  std::wstring report = L"WWE '13 Enhanced Launcher bug report\r\n";
  report += L"Launcher version: " + std::wstring(kLauncherVersion) + L"\r\n";
  report += L"Launcher SHA-256: " + Sha256File(app.launcher_path) + L"\r\n";
  report += L"wwe13.exe SHA-256: " + Sha256File(app.game_exe) + L"\r\n";
  report += L"Windows version: " + WindowsVersion() + L"\r\n\r\n";

  report += L"GPU list from the launcher's Vulkan enumeration:\r\n";
  if (app.gpus.empty()) {
    report += L"  (unavailable)\r\n";
  } else {
    for (const GpuOption& gpu : app.gpus) {
      report += L"  - " + gpu.name + L"; type=" + GpuTypeLabel(gpu.device_type) +
                L"; vendor:device=" + GpuId(gpu) + L"\r\n";
    }
  }
  if (!app.gpu_error.empty()) report += L"GPU enumeration note: " + app.gpu_error + L"\r\n";

  report += L"\r\nChosen settings:\r\n";
  report += L"  Resolution: " +
            (app.settings.resolution_explicit ? app.settings.resolution
                                              : std::wstring(L"auto")) +
            L"\r\n";
  report += L"  Frame rate: " + app.settings.frame_rate + L"\r\n";
  report += L"  Anti-aliasing: " + app.settings.anti_aliasing + L"\r\n";
  report += L"  Display: " + app.settings.display + L"\r\n";
  if (app.settings.gpu_id.empty()) {
    report += L"  GPU selection: Automatic\r\n";
  } else {
    const GpuOption* selected = FindGpuById(app, app.settings.gpu_id);
    report += L"  GPU selection: " + app.settings.gpu_id;
    if (selected) report += L" (" + selected->name + L", " + GpuTypeLabel(selected->device_type) + L")";
    report += L"\r\n";
  }
  report += L"  Game folder: " + app.settings.game_folder + L"\r\n";
  const bool game_files_present = GameFilesPresent(app.settings.game_folder);
  report += L"  Game folder check: ";
  report += game_files_present ? L"PASS (default.xex and default.xexp present)\r\n"
                               : L"FAIL (default.xex and/or default.xexp missing)\r\n";
  report += L"  Effective resolution: " + effective.resolution + L"\r\n";
  report += L"  Output/window resolution: " +
            std::wstring(GetSelectedResolution(effective)->output_resolution) + L"\r\n";
  report += L"  Effective frame rate: " + effective.frame_rate + L"\r\n";

  report += L"\r\nIncluded log files (newest first):\r\n";
  if (!included_log_count) {
    report += L"  (no matching logs\\wwe13-*.log files found)\r\n";
  } else {
    for (size_t i = 0; i < included_log_count; ++i) {
      report += L"  logs\\" + logs[i].name + L"\r\n";
    }
  }
  report += L"Log rule: include the newest matching log; include the previous log too when the newest is under 4096 bytes.\r\n";
  report += L"Privacy scrub: case-insensitive occurrences of USERPROFILE (slash or backslash form) and USERNAME are replaced with <user>; COMPUTERNAME is replaced with <computer> in every text file in this archive.\r\n";
  report += L"The launcher does not upload this report or make network requests.\r\n";
  return report;
}

bool CreateBugReportArchive(const AppState& app, std::wstring& archive_path,
                            std::wstring& error_message) {
  std::vector<GameLogFile> logs;
  if (!FindGameLogs(app, logs)) {
    error_message = L"The launcher could not list its logs folder.";
    return false;
  }
  size_t included_log_count = logs.empty() ? 0 : 1;
  if (!logs.empty() && logs[0].size < 4096 && logs.size() > 1) included_log_count = 2;

  const std::wstring profile_path = GetEnvironmentValue(L"USERPROFILE");
  const std::wstring user_name = GetEnvironmentValue(L"USERNAME");
  const std::wstring computer_name = GetEnvironmentValue(L"COMPUTERNAME");
  std::vector<ZipEntry> entries;
  for (size_t i = 0; i < included_log_count; ++i) {
    std::vector<uint8_t> content;
    if (!ReadFileBytes(JoinPath(app.logs_dir, logs[i].name), content)) {
      error_message = L"The launcher could not read log file " + logs[i].name + L".";
      return false;
    }
    if (!ScrubTextFile(content, profile_path, user_name, computer_name)) {
      error_message = L"The launcher could not scrub log file " + logs[i].name + L".";
      return false;
    }
    std::wstring archive_name = L"logs/";
    archive_name += logs[i].name;
    if (!AddZipEntry(entries, archive_name, std::move(content))) {
      error_message = L"The selected log is too large or has an invalid archive name.";
      return false;
    }
  }

  std::vector<uint8_t> settings = BuildIniBytes(app);
  if (!ScrubTextFile(settings, profile_path, user_name, computer_name) ||
      !AddZipEntry(entries, L"wwe13-enhanced.ini", std::move(settings))) {
    error_message = L"The launcher could not prepare the settings file for the report.";
    return false;
  }

  std::vector<uint8_t> report_info;
  if (!EncodeText(BuildReportInfo(app, logs, included_log_count), TextEncoding::kUtf8, false,
                  report_info) ||
      !ScrubTextFile(report_info, profile_path, user_name, computer_name) ||
      !AddZipEntry(entries, L"report-info.txt", std::move(report_info))) {
    error_message = L"The launcher could not prepare the report information.";
    return false;
  }

  std::vector<uint8_t> archive;
  if (!BuildZipArchive(entries, archive)) {
    error_message = L"The report is too large for a standard ZIP archive.";
    return false;
  }
  const std::wstring desktop = DesktopDirectory();
  if (!desktop.empty() && WriteArchiveToFolder(desktop, archive, archive_path)) return true;
  if (app.exe_dir != desktop && WriteArchiveToFolder(app.exe_dir, archive, archive_path)) return true;
  error_message = L"The launcher could not write the bug report to the Desktop or launcher folder.";
  return false;
}

std::wstring QuoteWindowsArgument(std::wstring_view argument) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t character : argument) {
    if (character == L'\\') {
      ++slashes;
      continue;
    }
    if (character == L'"') {
      result.append(slashes * 2 + 1, L'\\');
      result.push_back(L'"');
      slashes = 0;
      continue;
    }
    result.append(slashes, L'\\');
    slashes = 0;
    result.push_back(character);
  }
  result.append(slashes * 2, L'\\');
  result.push_back(L'"');
  return result;
}

std::vector<std::wstring> BuildArguments(const AppState& app) {
  std::vector<std::wstring> arguments;
  const Settings effective = GetEffectiveSettings(app);
  arguments.push_back(effective.game_folder);
  const bool fullscreen = _wcsicmp(effective.display.c_str(), L"windowed") != 0;
  const std::wstring log_path = JoinPath(app.logs_dir, L"wwe13-" + Timestamp() + L".log");
  for (const LaunchArgument& entry : kLaunchArguments) {
    switch (entry.kind) {
      case ArgumentKind::kLiteral:
        arguments.emplace_back(entry.value);
        break;
      case ArgumentKind::kFullscreen:
        arguments.emplace_back(fullscreen ? L"--fullscreen=true" : L"--fullscreen=false");
        break;
      case ArgumentKind::kLogFile:
        arguments.emplace_back(L"--log_file=" + log_path);
        break;
      case ArgumentKind::kUserDataRoot:
        arguments.emplace_back(L"--user_data_root=" + app.userdata_dir);
        break;
    }
  }
  const ResolutionOption* resolution = GetSelectedResolution(effective);
  if (resolution->output_resolution) {
    arguments.emplace_back(L"--resolution=" + std::wstring(resolution->output_resolution));
  }
  if (resolution->scale > 1) {
    arguments.emplace_back(L"--resolution_scale=" + std::to_wstring(resolution->scale));
  }
  if (!app.settings.gpu_id.empty()) {
    arguments.emplace_back(L"--vulkan_device_id=" + app.settings.gpu_id);
  }
  return arguments;
}

std::vector<EnvironmentEntry> BuildEnvironmentOverrides(const AppState& app) {
  std::vector<EnvironmentEntry> entries = {
      {L"REX_DEBUG_UI", L"false"},
      {L"WWE13_RATE_CENSUS", L""},
      {L"WWE13_FRAME_TIME", L""},
      {L"WWE13_PERF_OVERLAY", L""},
      {L"WWE13_THREAD_PROFILE", L""},
      {L"WWE13_THREAD_PROFILE_HZ", L""},
      {L"WWE13_THREAD_PROFILE_OUT", L""},
      {L"WWE13_LOCK_OWNER_SAMPLE", L""},
      {L"WWE13_F24_PIXEL_RATE", L"1"},
  };
  const Settings effective = GetEffectiveSettings(app);
  const ResolutionOption* resolution = GetSelectedResolution(effective);
  entries.push_back({L"WWE13_INTERNAL_RES",
                     resolution->internal_environment_value
                         ? resolution->internal_environment_value
                         : L""});
  const FrameRateOption* frame_rate = GetSelectedFrameRate(effective);
  if (frame_rate->environment_name) {
    entries.push_back({frame_rate->environment_name, frame_rate->environment_value});
  }
  const AntiAliasingOption* anti_aliasing = GetSelectedAntiAliasing(app.settings);
  entries.push_back({L"WWE13_SCENE_AA",
                     anti_aliasing->environment_value ? anti_aliasing->environment_value : L""});
  return entries;
}

bool EnvironmentNameEquals(std::wstring_view item, std::wstring_view name) {
  const size_t equals = item.find(L'=');
  if (equals == std::wstring_view::npos) return false;
  const std::wstring_view item_name = item.substr(0, equals);
  return item_name.size() == name.size() && _wcsnicmp(item_name.data(), name.data(), name.size()) == 0;
}

std::vector<wchar_t> BuildChildEnvironment(const AppState& app) {
  const std::vector<EnvironmentEntry> overrides = BuildEnvironmentOverrides(app);
  std::vector<std::wstring> values;
  LPWCH inherited = GetEnvironmentStringsW();
  if (inherited) {
    for (const wchar_t* item = inherited; *item; item += wcslen(item) + 1) {
      bool replaced = false;
      for (const EnvironmentEntry& entry : overrides) {
        if (EnvironmentNameEquals(item, entry.name)) {
          replaced = true;
          break;
        }
      }
      for (const FrameRateOption& option : kFrameRateOptions) {
        if (option.environment_name && EnvironmentNameEquals(item, option.environment_name)) {
          replaced = true;
          break;
        }
      }
      if (!replaced) values.emplace_back(item);
    }
    FreeEnvironmentStringsW(inherited);
  }
  for (const EnvironmentEntry& entry : overrides) {
    if ((_wcsicmp(entry.name.c_str(), L"WWE13_INTERNAL_RES") == 0 ||
         _wcsicmp(entry.name.c_str(), L"WWE13_SCENE_AA") == 0) &&
        entry.value.empty()) {
      // An empty setting means the inherited variable must be absent so the
      // game uses its own unmodified choice.
      continue;
    }
    values.push_back(entry.name + L"=" + entry.value);
  }
  std::sort(values.begin(), values.end(), [](const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
  });
  size_t count = 1;
  for (const std::wstring& value : values) count += value.size() + 1;
  std::vector<wchar_t> block;
  block.reserve(count + 1);
  for (const std::wstring& value : values) {
    block.insert(block.end(), value.begin(), value.end());
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  if (values.empty()) block.push_back(L'\0');
  return block;
}

std::wstring BuildCommandLine(const std::wstring& game_exe, const std::vector<std::wstring>& arguments) {
  std::wstring command = QuoteWindowsArgument(game_exe);
  for (const std::wstring& argument : arguments) {
    command.push_back(L' ');
    command.append(QuoteWindowsArgument(argument));
  }
  return command;
}

std::string WideToUtf8(std::wstring_view text) {
  if (text.empty()) return {};
  const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0,
                                        nullptr, nullptr);
  if (needed <= 0) return {};
  std::string output(size_t(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), output.data(), needed, nullptr,
                      nullptr);
  return output;
}

std::wstring BuildPrintReport(const AppState& app) {
  const std::wstring game_exe = JoinPath(app.exe_dir, L"wwe13.exe");
  const std::wstring command = BuildCommandLine(game_exe, BuildArguments(app));
  std::wstring report = L"COMMAND_LINE=" + command + L"\r\nCHILD_ENVIRONMENT_OVERRIDES:\r\n";
  for (const EnvironmentEntry& entry : BuildEnvironmentOverrides(app)) {
    report += L"  " + entry.name + L"=";
    if ((_wcsicmp(entry.name.c_str(), L"WWE13_INTERNAL_RES") == 0 ||
         _wcsicmp(entry.name.c_str(), L"WWE13_SCENE_AA") == 0) &&
        entry.value.empty()) {
      report += L"<not set>";
    } else {
      report += entry.value;
    }
    report += L"\r\n";
  }
  for (const wchar_t* name : {L"WWE13_INTERNAL_RES", L"WWE13_SCENE_AA", L"WWE13_KEEP_60",
                              L"WWE13_LOCK_30"}) {
    bool listed = false;
    for (const EnvironmentEntry& entry : BuildEnvironmentOverrides(app)) {
      if (_wcsicmp(entry.name.c_str(), name) == 0) listed = true;
    }
    if (!listed) report += L"  " + std::wstring(name) + L"=<not set>\r\n";
  }
  report += L"(Other inherited environment entries are passed through unchanged.)\r\n";
  return report;
}

void WriteReportToParentConsole(const std::wstring& report) {
  if (GetStdHandle(STD_OUTPUT_HANDLE) == nullptr || GetStdHandle(STD_OUTPUT_HANDLE) == INVALID_HANDLE_VALUE) {
    AttachConsole(ATTACH_PARENT_PROCESS);
  }
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (output == nullptr || output == INVALID_HANDLE_VALUE) return;
  DWORD mode = 0;
  if (GetConsoleMode(output, &mode)) {
    DWORD written = 0;
    WriteConsoleW(output, report.data(), DWORD(report.size()), &written, nullptr);
  } else {
    const std::string utf8 = WideToUtf8(report);
    DWORD written = 0;
    WriteFile(output, utf8.data(), DWORD(utf8.size()), &written, nullptr);
  }
}

std::wstring GetOptionArgument(const std::vector<std::wstring>& arguments, const wchar_t* prefix) {
  const size_t prefix_length = wcslen(prefix);
  for (const std::wstring& argument : arguments) {
    if (argument.size() >= prefix_length && _wcsnicmp(argument.c_str(), prefix, prefix_length) == 0) {
      return argument.substr(prefix_length);
    }
  }
  return {};
}

bool HasOption(const std::vector<std::wstring>& arguments, const wchar_t* option) {
  return std::any_of(arguments.begin(), arguments.end(), [option](const std::wstring& argument) {
    return _wcsicmp(argument.c_str(), option) == 0;
  });
}

bool HasOptionPrefix(const std::vector<std::wstring>& arguments, const wchar_t* prefix) {
  const size_t prefix_length = wcslen(prefix);
  return std::any_of(arguments.begin(), arguments.end(), [prefix, prefix_length](const std::wstring& argument) {
    return argument.size() >= prefix_length &&
           _wcsnicmp(argument.c_str(), prefix, prefix_length) == 0;
  });
}

bool ApplyPrintOverrides(AppState& app, const std::vector<std::wstring>& arguments,
                         std::wstring& error) {
  if (HasOptionPrefix(arguments, L"--assume-integrated=")) {
    const std::wstring assumed_id = GetOptionArgument(arguments, L"--assume-integrated=");
    GpuOption* gpu = FindGpuById(app, assumed_id);
    if (!gpu) {
      error = L"--assume-integrated must name a vendor:device ID shown by the GPU list.";
      return false;
    }
    gpu->device_type = kVkPhysicalDeviceTypeIntegratedGpu;
    app.simulated_auto_gpu_id = GpuId(*gpu);
  }

  std::wstring value = GetOptionArgument(arguments, L"--resolution=");
  if (!value.empty()) {
    size_t index = 0;
    if (!FindOption(value.c_str(), kResolutionOptions, index)) {
      error = L"--resolution must be 480p, 720p, or 1440p.";
      return false;
    }
    app.settings.resolution = kResolutionOptions[index].id;
    app.settings.resolution_explicit = true;
  }

  value = GetOptionArgument(arguments, L"--frame-rate=");
  if (!value.empty()) {
    bool matched = false;
    for (const FrameRateOption& option : kFrameRateOptions) {
      if (option.enabled && _wcsicmp(option.id, value.c_str()) == 0) {
        app.settings.frame_rate = option.id;
        app.settings.frame_rate_explicit = true;
        matched = true;
      }
    }
    if (!matched) {
      error = L"--frame-rate must be classic, keep-60, or lock-30.";
      return false;
    }
  }

  if (HasOptionPrefix(arguments, L"--anti-aliasing=")) {
    value = GetOptionArgument(arguments, L"--anti-aliasing=");
    size_t index = 0;
    if (!FindOption(value.c_str(), kAntiAliasingOptions, index)) {
      error = L"--anti-aliasing must be 2x or original.";
      return false;
    }
    app.settings.anti_aliasing = kAntiAliasingOptions[index].id;
  }

  value = GetOptionArgument(arguments, L"--internal-resolution=");
  if (!value.empty()) {
    size_t index = 0;
    if (!FindOption(value.c_str(), kResolutionOptions, index) ||
        kResolutionOptions[index].scale > 0) {
      error = L"Legacy --internal-resolution must be 480p or 720p.";
      return false;
    }
    app.settings.resolution = kResolutionOptions[index].id;
    app.settings.resolution_explicit = true;
  }

  value = GetOptionArgument(arguments, L"--display=");
  if (!value.empty()) {
    size_t index = 0;
    if (!FindOption(value.c_str(), kDisplayOptions, index)) {
      error = L"--display must be fullscreen or windowed.";
      return false;
    }
    app.settings.display = kDisplayOptions[index].id;
  }

  value = GetOptionArgument(arguments, L"--gpu=");
  if (!value.empty()) {
    if (_wcsicmp(value.c_str(), L"auto") == 0) {
      app.settings.gpu_id.clear();
      app.settings.gpu_name.clear();
    } else {
      uint32_t vendor_id = 0;
      uint32_t device_id = 0;
      if (!ParseGpuId(value, vendor_id, device_id)) {
        error = L"--gpu must be auto or a Vulkan vendor:device ID shown by the GPU list.";
        return false;
      }
      app.settings.gpu_id = FormatGpuId(vendor_id, device_id);
      const GpuOption* gpu = FindGpuById(app, app.settings.gpu_id);
      if (!gpu) {
        error = L"--gpu ID must match a Vulkan device shown by the GPU list.";
        return false;
      }
      app.settings.gpu_name = gpu->name;
    }
  }

  value = GetOptionArgument(arguments, L"--game-folder=");
  if (!value.empty()) app.settings.game_folder = value;
  return true;
}

int RunPrintCommand(AppState& app, const std::vector<std::wstring>& arguments) {
  std::wstring error;
  if (!ApplyPrintOverrides(app, arguments, error)) {
    const std::wstring report = L"ERROR: " + error + L"\r\n";
    WriteReportToParentConsole(report);
    return 2;
  }
  const std::wstring report = BuildPrintReport(app);
  const std::string utf8 = WideToUtf8(report);
  const std::wstring output_path = JoinPath(app.exe_dir, L"wwe13-enhanced-command.txt");
  if (!WriteAtomicBytes(output_path, utf8.data(), utf8.size())) {
    WriteReportToParentConsole(L"ERROR: Could not write wwe13-enhanced-command.txt.\r\n");
    return 3;
  }
  WriteReportToParentConsole(report);
  return 0;
}

void SetControlFont(HWND control, HFONT font) {
  if (control && font) SendMessageW(control, WM_SETFONT, WPARAM(font), TRUE);
}

HFONT CreateLauncherFont(int height, int weight) {
  return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void CreateMainFonts(AppState& app) {
  const HFONT old_regular = app.regular_font;
  const HFONT old_bold = app.bold_font;
  const HFONT old_title = app.title_font;
  const HFONT old_regular_paint = app.regular_paint_font;
  const HFONT old_bold_paint = app.bold_paint_font;
  const HFONT old_title_paint = app.title_paint_font;
  app.regular_font = CreateLauncherFont(ScaleForDpi(17, app.dpi), FW_NORMAL);
  app.bold_font = CreateLauncherFont(ScaleForDpi(16, app.dpi), FW_SEMIBOLD);
  app.title_font = CreateLauncherFont(ScaleForDpi(34, app.dpi), FW_BOLD);
  app.regular_paint_font = CreateLauncherFont(17, FW_NORMAL);
  app.bold_paint_font = CreateLauncherFont(16, FW_SEMIBOLD);
  app.title_paint_font = CreateLauncherFont(34, FW_BOLD);

  for (HWND control : {app.resolution_combo, app.frame_rate_combo, app.anti_aliasing_combo,
                       app.display_combo, app.gpu_combo}) {
    SetControlFont(control, app.regular_font);
  }
  for (HWND control : {app.browse_button, app.bug_report_button, app.keyboard_controls_button,
                       app.play_button, app.exit_button}) {
    SetControlFont(control, app.bold_font);
  }
  SetControlFont(app.auto_start_checkbox, app.regular_font);
  if (old_regular) DeleteObject(old_regular);
  if (old_bold) DeleteObject(old_bold);
  if (old_title) DeleteObject(old_title);
  if (old_regular_paint) DeleteObject(old_regular_paint);
  if (old_bold_paint) DeleteObject(old_bold_paint);
  if (old_title_paint) DeleteObject(old_title_paint);
}

void CreateMappingFonts(AppState& app) {
  const HFONT old_regular = app.mapping_regular_font;
  const HFONT old_bold = app.mapping_bold_font;
  const HFONT old_title = app.mapping_title_font;
  const HFONT old_regular_paint = app.mapping_regular_paint_font;
  const HFONT old_bold_paint = app.mapping_bold_paint_font;
  const HFONT old_title_paint = app.mapping_title_paint_font;
  app.mapping_regular_font = CreateLauncherFont(ScaleForDpi(17, app.mapping_dpi), FW_NORMAL);
  app.mapping_bold_font = CreateLauncherFont(ScaleForDpi(16, app.mapping_dpi), FW_SEMIBOLD);
  app.mapping_title_font = CreateLauncherFont(ScaleForDpi(30, app.mapping_dpi), FW_BOLD);
  app.mapping_regular_paint_font = CreateLauncherFont(17, FW_NORMAL);
  app.mapping_bold_paint_font = CreateLauncherFont(16, FW_SEMIBOLD);
  app.mapping_title_paint_font = CreateLauncherFont(30, FW_BOLD);

  for (HWND control : app.mapping_key_buttons) SetControlFont(control, app.mapping_regular_font);
  for (HWND control : app.mapping_escape_buttons) SetControlFont(control, app.mapping_regular_font);
  SetControlFont(app.mapping_reset_button, app.mapping_bold_font);
  SetControlFont(app.mapping_save_button, app.mapping_bold_font);
  SetControlFont(app.mapping_cancel_button, app.mapping_bold_font);
  if (old_regular) DeleteObject(old_regular);
  if (old_bold) DeleteObject(old_bold);
  if (old_title) DeleteObject(old_title);
  if (old_regular_paint) DeleteObject(old_regular_paint);
  if (old_bold_paint) DeleteObject(old_bold_paint);
  if (old_title_paint) DeleteObject(old_title_paint);
}

void AddComboValue(HWND combo, const wchar_t* text) {
  SendMessageW(combo, CB_ADDSTRING, 0, LPARAM(text));
}

int OwnerDrawComboHeight(size_t item_count, UINT dpi) {
  // CBS_DROPDOWNLIST uses the window height for both its selection field and
  // the dropped list. Reserve one extra row for the native field/list-frame
  // allocation: the exact row-count height left one item hidden in Wine.
  const int item_height = ScaleForDpi(kComboItemHeight, dpi);
  return item_height + (int(item_count) + 1) * item_height +
         ScaleForDpi(kComboBorderAllowance, dpi);
}

void SetComboItemHeights(HWND combo, UINT dpi) {
  const int item_height = ScaleForDpi(kComboItemHeight, dpi);
  SendMessageW(combo, CB_SETITEMHEIGHT, WPARAM(-1), item_height);
  SendMessageW(combo, CB_SETITEMHEIGHT, 0, item_height);
}

void ResizeOwnerDrawComboToItems(HWND combo) {
  if (!combo) return;
  LRESULT item_count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
  if (item_count == CB_ERR) item_count = 0;
  RECT bounds{};
  if (!GetWindowRect(combo, &bounds)) return;

  const UINT dpi = GetWindowDpi(combo);
  int dropped_width = bounds.right - bounds.left;
  HDC dc = GetDC(combo);
  if (dc) {
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(combo, WM_GETFONT, 0, 0));
    HGDIOBJ old_font = font ? SelectObject(dc, font) : nullptr;
    for (LRESULT index = 0; index < item_count; ++index) {
      const LRESULT text_length = SendMessageW(combo, CB_GETLBTEXTLEN, WPARAM(index), 0);
      if (text_length == CB_ERR) continue;
      std::vector<wchar_t> text(size_t(text_length) + 1, L'\0');
      if (SendMessageW(combo, CB_GETLBTEXT, WPARAM(index), LPARAM(text.data())) == CB_ERR) {
        continue;
      }
      SIZE extent{};
      if (GetTextExtentPoint32W(dc, text.data(), int(text_length), &extent)) {
        dropped_width = std::max(dropped_width, int(extent.cx) + ScaleForDpi(32, dpi));
      }
    }
    if (old_font) SelectObject(dc, old_font);
    ReleaseDC(combo, dc);
  }
  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  const HMONITOR monitor = MonitorFromWindow(combo, MONITOR_DEFAULTTONEAREST);
  if (monitor && GetMonitorInfoW(monitor, &monitor_info)) {
    const int available_width = monitor_info.rcWork.right - bounds.left - 8;
    if (available_width >= bounds.right - bounds.left) {
      dropped_width = std::min(dropped_width, available_width);
    }
  }
  SendMessageW(combo, CB_SETDROPPEDWIDTH, WPARAM(dropped_width), 0);
  SetWindowPos(combo, nullptr, 0, 0, bounds.right - bounds.left,
               OwnerDrawComboHeight(size_t(item_count), dpi),
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

int FindComboEntry(HWND combo, const wchar_t* wanted) {
  const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
  for (LRESULT i = 0; i < count; ++i) {
    wchar_t text[512]{};
    SendMessageW(combo, CB_GETLBTEXT, WPARAM(i), LPARAM(text));
    if (_wcsicmp(text, wanted) == 0) return int(i);
  }
  return -1;
}

void AddOwnerDrawCombo(HWND parent, int id, HWND& result, int x, int y, int width,
                       const std::vector<std::wstring>& items, HFONT font) {
  result = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                                CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
                           x, y, width, OwnerDrawComboHeight(items.size(), GetWindowDpi(parent)), parent,
                           HMENU(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
  if (!result) return;
  SetControlFont(result, font);
  SetComboItemHeights(result, GetWindowDpi(result));
  for (const std::wstring& item : items) AddComboValue(result, item.c_str());
  ResizeOwnerDrawComboToItems(result);
}

void AddOwnerDrawButton(HWND parent, int id, const wchar_t* text, HWND& result, int x, int y,
                        int width, int height, HFONT font) {
  result = CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                           x, y, width, height, parent, HMENU(INT_PTR(id)), GetModuleHandleW(nullptr),
                           nullptr);
  if (result) SetControlFont(result, font);
}

void SelectComboValue(HWND combo, const wchar_t* value) {
  const int index = FindComboEntry(combo, value);
  SendMessageW(combo, CB_SETCURSEL, WPARAM(index < 0 ? 0 : index), 0);
}

void SetComboSelections(AppState& app) {
  app.initializing_controls = true;
  const Settings effective = GetEffectiveSettings(app);
  SelectComboValue(app.resolution_combo, GetSelectedResolution(effective)->label);
  SelectComboValue(app.frame_rate_combo, GetSelectedFrameRate(effective)->label);
  SelectComboValue(app.anti_aliasing_combo, GetSelectedAntiAliasing(app.settings)->label);
  SelectComboValue(app.display_combo,
                   _wcsicmp(effective.display.c_str(), L"windowed") == 0 ? L"Windowed" : L"Fullscreen");
  int gpu_selection = 0;
  for (size_t i = 0; i < app.gpus.size(); ++i) {
    if (!app.settings.gpu_id.empty() &&
        _wcsicmp(GpuId(app.gpus[i]).c_str(), app.settings.gpu_id.c_str()) == 0) {
      gpu_selection = int(i + 1);
      break;
    }
  }
  SendMessageW(app.gpu_combo, CB_SETCURSEL, WPARAM(gpu_selection), 0);
  app.initializing_controls = false;
}

void UpdateControlState(AppState& app) {
  if (app.play_button) EnableWindow(app.play_button, GameFilesPresent(app.settings.game_folder));
  if (app.window) InvalidateRect(app.window, nullptr, FALSE);
}

void DrawTextLine(HDC dc, HFONT font, COLORREF color, const wchar_t* text, RECT rect,
                  UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX) {
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  HFONT old = static_cast<HFONT>(SelectObject(dc, font));
  DrawTextW(dc, text, -1, &rect, format);
  SelectObject(dc, old);
}

void DrawBackground(AppState& app, HDC dc, const RECT& client) {
  HBRUSH background = CreateSolidBrush(kBackground);
  FillRect(dc, &client, background);
  DeleteObject(background);

  RECT header{28, 22, client.right - 28, 105};
  HBRUSH panel_brush = CreateSolidBrush(kPanel);
  FillRect(dc, &header, panel_brush);
  DeleteObject(panel_brush);

  RECT accent{28, 22, 35, 105};
  HBRUSH accent_brush = CreateSolidBrush(kAccent);
  FillRect(dc, &accent, accent_brush);
  DeleteObject(accent_brush);

  RECT title{54, 31, client.right - 48, 72};
  DrawTextLine(dc, app.title_paint_font, kText, L"WWE '13", title);
  RECT subtitle{56, 73, client.right - 48, 96};
  DrawTextLine(dc, app.regular_paint_font, kMuted, L"ENHANCED SETTINGS", subtitle);

  const int left = 40;
  const int right = client.right - 40;
  auto label = [&](const wchar_t* value, int x, int y, int width) {
    RECT bounds{x, y, x + width, y + 22};
    DrawTextLine(dc, app.bold_paint_font, kMuted, value, bounds);
  };
  label(L"RESOLUTION", left, 125, right - left);
  label(L"ANTI-ALIASING", 520, 125, right - 520);
  label(L"FRAME RATE", left, 207, 460);
  label(L"DISPLAY", 520, 207, right - 520);
  label(L"GRAPHICS CARD", left, 273, 330);

  if (IsIntegratedGpuMode(app)) {
    RECT notice{520, 273, right, 295};
    DrawTextLine(dc, app.bold_paint_font, kWarning,
                 L"iGPU defaults: 480p / 30 fps (changeable)", notice);
  } else if (SelectedResolutionIs1440(app.settings) &&
             _wcsicmp(app.settings.frame_rate.c_str(), L"keep-60") == 0) {
    RECT warning{520, 273, right, 295};
    DrawTextLine(dc, app.bold_paint_font, kWarning,
                 L"1440p + 60 may be unstable on some systems.", warning);
  } else if (!app.gpu_error.empty()) {
    RECT warning{520, 273, right, 295};
    DrawTextLine(dc, app.regular_paint_font, kWarning, app.gpu_error.c_str(), warning,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  }

  RECT files{28, 337, client.right - 28, 535};
  panel_brush = CreateSolidBrush(kPanel);
  FillRect(dc, &files, panel_brush);
  DeleteObject(panel_brush);
  RECT section{46, 348, right, 376};
  DrawTextLine(dc, app.bold_paint_font, kText, L"GAME FILES", section);
  RECT folder{46, 378, right - 8, 404};
  DrawTextLine(dc, app.regular_paint_font, kMuted, app.settings.game_folder.c_str(), folder,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

  RECT status{46, 408, right - 8, 433};
  const bool valid = GameFilesPresent(app.settings.game_folder);
  DrawTextLine(dc, app.bold_paint_font, valid ? RGB(129, 218, 157) : kError,
               valid ? L"Game files found — ready to play." :
                       L"Game files not ready: default.xex and default.xexp are both required.",
               status);
  RECT tip{46, 434, right - 8, 457};
  DrawTextLine(dc, app.regular_paint_font, kMuted,
               valid ? L"The launcher checks file presence; it does not verify file contents or version."
                     : L"Choose a folder containing the game and title update 2.0.1.0.",
               tip);

  if (app.settings_write_failed) {
    RECT error{46, 462, right - 8, 484};
    DrawTextLine(dc, app.regular_paint_font, kError, L"Could not save settings; check folder permissions.", error);
  }

  HPEN line = CreatePen(PS_SOLID, 1, RGB(55, 59, 67));
  HPEN old_pen = static_cast<HPEN>(SelectObject(dc, line));
  MoveToEx(dc, 30, 555, nullptr);
  LineTo(dc, client.right - 30, 555);
  SelectObject(dc, old_pen);
  DeleteObject(line);
}

bool IsDuplicateMapping(const AppState& app, size_t index) {
  if (index >= kPadBindingCount) return false;
  const KeyName* key = FindKeyName(app.mapping_values[index]);
  if (!key) return false;
  for (size_t other = 0; other < kPadBindingCount; ++other) {
    if (other == index) continue;
    const KeyName* other_key = FindKeyName(app.mapping_values[other]);
    if (other_key && other_key->virtual_key == key->virtual_key) return true;
  }
  return false;
}

bool HasDuplicateMappings(const AppState& app) {
  for (size_t i = 0; i < kPadBindingCount; ++i) {
    if (IsDuplicateMapping(app, i)) return true;
  }
  return false;
}

void UpdateMappingButtons(AppState& app) {
  for (size_t i = 0; i < kPadBindingCount; ++i) {
    if (!app.mapping_key_buttons[i]) continue;
    const std::wstring label = app.mapping_capture_index == i
                                   ? L"Press a key..."
                                   : (app.mapping_values[i].empty() ? L"(Unbound)"
                                                                    : app.mapping_values[i]);
    SetWindowTextW(app.mapping_key_buttons[i], label.c_str());
    InvalidateRect(app.mapping_key_buttons[i], nullptr, FALSE);
    if (app.mapping_escape_buttons[i]) {
      InvalidateRect(app.mapping_escape_buttons[i], nullptr, FALSE);
    }
  }
  if (app.mapping_capture_index < kPadBindingCount) {
    app.mapping_status = L"Press a key. Escape cancels; use Bind Esc to assign Escape.";
  } else if (HasDuplicateMappings(app)) {
    app.mapping_status = L"Duplicate keys are highlighted. Resolve them or confirm before saving.";
  } else {
    app.mapping_status = L"Changes are saved beside wwe13.exe in wwe13.toml.";
  }
  if (app.mapping_window) InvalidateRect(app.mapping_window, nullptr, FALSE);
}

void BeginMappingCapture(AppState& app, size_t index) {
  if (index >= kPadBindingCount) return;
  app.mapping_capture_index = index;
  UpdateMappingButtons(app);
  SetFocus(app.mapping_window);
}

void FinishMappingCapture(AppState& app, const KeyName* key) {
  const size_t index = app.mapping_capture_index;
  if (index >= kPadBindingCount || !key) return;
  app.mapping_values[index] = key->name;
  app.mapping_modified[index] = true;
  app.mapping_capture_index = kPadBindingCount;
  UpdateMappingButtons(app);
}

void DrawButton(const DRAWITEMSTRUCT* item, const AppState* app = nullptr) {
  const bool disabled = (item->itemState & ODS_DISABLED) != 0;
  const bool pressed = (item->itemState & ODS_SELECTED) != 0;
  const int id = int(item->CtlID);
  if (id == kAutoStartCheckbox) {
    HBRUSH background = CreateSolidBrush(kBackground);
    FillRect(item->hDC, &item->rcItem, background);
    DeleteObject(background);

    const UINT dpi = GetWindowDpi(item->hwndItem);
    const int side = ScaleForDpi(20, dpi);
    RECT box{item->rcItem.left + ScaleForDpi(2, dpi),
             item->rcItem.top + (item->rcItem.bottom - item->rcItem.top - side) / 2,
             item->rcItem.left + ScaleForDpi(2, dpi) + side,
             item->rcItem.top + (item->rcItem.bottom - item->rcItem.top + side) / 2};
    HBRUSH box_fill = CreateSolidBrush(kControl);
    FillRect(item->hDC, &box, box_fill);
    DeleteObject(box_fill);
    HBRUSH border = CreateSolidBrush(RGB(115, 122, 133));
    FrameRect(item->hDC, &box, border);
    DeleteObject(border);
    if (app && app->settings.auto_start) {
      HPEN check = CreatePen(PS_SOLID, std::max(ScaleForDpi(2, dpi), 1), kAccent);
      HPEN old_pen = static_cast<HPEN>(SelectObject(item->hDC, check));
      MoveToEx(item->hDC, box.left + ScaleForDpi(4, dpi), box.top + side / 2, nullptr);
      LineTo(item->hDC, box.left + side / 2 - ScaleForDpi(1, dpi),
             box.bottom - ScaleForDpi(4, dpi));
      LineTo(item->hDC, box.right - ScaleForDpi(3, dpi), box.top + ScaleForDpi(4, dpi));
      SelectObject(item->hDC, old_pen);
      DeleteObject(check);
    }

    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, disabled ? RGB(139, 141, 146) : kText);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    HFONT old = static_cast<HFONT>(SelectObject(item->hDC, font));
    RECT label = item->rcItem;
    label.left = box.right + ScaleForDpi(10, dpi);
    wchar_t text[128]{};
    GetWindowTextW(item->hwndItem, text, int(std::size(text)));
    DrawTextW(item->hDC, text, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (item->itemState & ODS_FOCUS) {
      RECT focus = item->rcItem;
      InflateRect(&focus, -ScaleForDpi(2, dpi), -ScaleForDpi(2, dpi));
      DrawFocusRect(item->hDC, &focus);
    }
    SelectObject(item->hDC, old);
    return;
  }
  COLORREF fill = (id == kPlayButton || id == kMappingSaveButton) ? kAccent : kControl;
  if (app && id >= 1000 && id < 1000 + int(kPadBindingCount) &&
      IsDuplicateMapping(*app, size_t(id - 1000))) {
    fill = RGB(116, 42, 48);
  }
  if (disabled) fill = RGB(60, 62, 67);
  else if (pressed && fill != RGB(116, 42, 48)) {
    fill = (id == kPlayButton || id == kMappingSaveButton) ? RGB(171, 24, 36)
                                                            : RGB(53, 58, 67);
  }
  HBRUSH brush = CreateSolidBrush(fill);
  FillRect(item->hDC, &item->rcItem, brush);
  DeleteObject(brush);
  SetBkMode(item->hDC, TRANSPARENT);
  SetTextColor(item->hDC, disabled ? RGB(139, 141, 146) : kText);
  HFONT font = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
  HFONT old = static_cast<HFONT>(SelectObject(item->hDC, font));
  wchar_t text[128]{};
  GetWindowTextW(item->hwndItem, text, int(std::size(text)));
  DrawTextW(item->hDC, text, -1, const_cast<RECT*>(&item->rcItem),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (item->itemState & ODS_FOCUS) {
    RECT focus = item->rcItem;
    InflateRect(&focus, -4, -4);
    DrawFocusRect(item->hDC, &focus);
  }
  SelectObject(item->hDC, old);
}

void CreateMappingControls(AppState& app, HWND window) {
  for (size_t column = 0; column < 2; ++column) {
    for (size_t row = 0; row < kPadBindingCount / 2; ++row) {
      const size_t index = column * (kPadBindingCount / 2) + row;
      const int y = ScaleForDpi(108 + int(row) * 37, app.mapping_dpi);
      const int key_x = ScaleForDpi(column == 0 ? 230 : 822, app.mapping_dpi);
      const int escape_x = ScaleForDpi(column == 0 ? 414 : 1006, app.mapping_dpi);
      AddOwnerDrawButton(window, 1000 + int(index), L"", app.mapping_key_buttons[index],
                         key_x, y, ScaleForDpi(174, app.mapping_dpi),
                         ScaleForDpi(30, app.mapping_dpi), app.mapping_regular_font);
      AddOwnerDrawButton(window, 1100 + int(index), L"Bind Esc",
                         app.mapping_escape_buttons[index], escape_x, y,
                         ScaleForDpi(92, app.mapping_dpi), ScaleForDpi(30, app.mapping_dpi),
                         app.mapping_regular_font);
    }
  }
}

void LayoutMappingControls(AppState& app) {
  const UINT dpi = app.mapping_dpi;
  for (size_t column = 0; column < 2; ++column) {
    for (size_t row = 0; row < kPadBindingCount / 2; ++row) {
      const size_t index = column * (kPadBindingCount / 2) + row;
      const int y = ScaleForDpi(108 + int(row) * 37, dpi);
      const int key_x = ScaleForDpi(column == 0 ? 230 : 822, dpi);
      const int escape_x = ScaleForDpi(column == 0 ? 414 : 1006, dpi);
      MoveWindow(app.mapping_key_buttons[index], key_x, y, ScaleForDpi(174, dpi),
                 ScaleForDpi(30, dpi), TRUE);
      MoveWindow(app.mapping_escape_buttons[index], escape_x, y, ScaleForDpi(92, dpi),
                 ScaleForDpi(30, dpi), TRUE);
    }
  }
  MoveWindow(app.mapping_reset_button, ScaleForDpi(28, dpi), ScaleForDpi(600, dpi),
             ScaleForDpi(188, dpi), ScaleForDpi(38, dpi), TRUE);
  MoveWindow(app.mapping_save_button, ScaleForDpi(930, dpi), ScaleForDpi(600, dpi),
             ScaleForDpi(92, dpi), ScaleForDpi(38, dpi), TRUE);
  MoveWindow(app.mapping_cancel_button, ScaleForDpi(1040, dpi), ScaleForDpi(600, dpi),
             ScaleForDpi(125, dpi), ScaleForDpi(38, dpi), TRUE);
}

LRESULT CALLBACK MappingWindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  AppState* app = g_app;
  if (!app) return DefWindowProcW(window, message, wparam, lparam);
  switch (message) {
    case WM_CREATE: {
      app->mapping_window = window;
      app->mapping_dpi = GetWindowDpi(window);
      CreateMappingFonts(*app);
      app->mapping_capture_index = kPadBindingCount;
      CreateMappingControls(*app, window);
      AddOwnerDrawButton(window, kMappingResetButton, L"Reset to defaults", app->mapping_reset_button,
                         ScaleForDpi(28, app->mapping_dpi), ScaleForDpi(600, app->mapping_dpi),
                         ScaleForDpi(188, app->mapping_dpi), ScaleForDpi(38, app->mapping_dpi),
                         app->mapping_bold_font);
      AddOwnerDrawButton(window, kMappingSaveButton, L"Save", app->mapping_save_button,
                         ScaleForDpi(930, app->mapping_dpi), ScaleForDpi(600, app->mapping_dpi),
                         ScaleForDpi(92, app->mapping_dpi), ScaleForDpi(38, app->mapping_dpi),
                         app->mapping_bold_font);
      AddOwnerDrawButton(window, kMappingCancelButton, L"Cancel", app->mapping_cancel_button,
                         ScaleForDpi(1040, app->mapping_dpi), ScaleForDpi(600, app->mapping_dpi),
                         ScaleForDpi(125, app->mapping_dpi), ScaleForDpi(38, app->mapping_dpi),
                         app->mapping_bold_font);
      LayoutMappingControls(*app);
      UpdateMappingButtons(*app);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      const int saved = SetPaintDpiTransform(dc, app->mapping_dpi);
      RECT client{};
      GetClientRect(window, &client);
      if (app->mapping_dpi != kDefaultDpi) {
        client.right = MulDiv(client.right, int(kDefaultDpi), int(app->mapping_dpi));
        client.bottom = MulDiv(client.bottom, int(kDefaultDpi), int(app->mapping_dpi));
      }
      HBRUSH background = CreateSolidBrush(kBackground);
      FillRect(dc, &client, background);
      DeleteObject(background);
      RECT heading{20, 16, client.right - 20, 76};
      HBRUSH panel = CreateSolidBrush(kPanel);
      FillRect(dc, &heading, panel);
      DeleteObject(panel);
      RECT title{34, 18, client.right - 34, 53};
      DrawTextLine(dc, app->mapping_title_paint_font, kText, L"KEYBOARD CONTROLS", title);
      RECT subtitle{36, 54, client.right - 34, 74};
      DrawTextLine(dc, app->mapping_regular_paint_font, kMuted,
                   L"Click a key to remap it. Escape cancels capture; use Bind Esc to assign Escape.",
                   subtitle);
      RECT headers{20, 78, client.right - 20, 100};
      panel = CreateSolidBrush(kPanel);
      FillRect(dc, &headers, panel);
      DeleteObject(panel);
      DrawTextLine(dc, app->mapping_bold_paint_font, kMuted, L"PAD INPUT", RECT{28, 78, 218, 100});
      DrawTextLine(dc, app->mapping_bold_paint_font, kMuted, L"KEY", RECT{230, 78, 404, 100});
      DrawTextLine(dc, app->mapping_bold_paint_font, kMuted, L"PAD INPUT", RECT{620, 78, 810, 100});
      DrawTextLine(dc, app->mapping_bold_paint_font, kMuted, L"KEY", RECT{822, 78, 996, 100});
      for (size_t column = 0; column < 2; ++column) {
        for (size_t row = 0; row < kPadBindingCount / 2; ++row) {
          const size_t index = column * (kPadBindingCount / 2) + row;
          const int x = column == 0 ? 28 : 620;
          const int y = 108 + int(row) * 37;
          RECT label{x, y, x + 190, y + 30};
          DrawTextLine(dc, app->mapping_regular_paint_font,
                       IsDuplicateMapping(*app, index) ? kError : kText,
                       kPadBindings[index].label, label,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
      }
      RECT status{28, 554, client.right - 28, 580};
      const COLORREF status_color = HasDuplicateMappings(*app) ? kWarning : kMuted;
      DrawTextLine(dc, app->mapping_regular_paint_font, status_color, app->mapping_status.c_str(), status);
      if (saved) RestoreDC(dc, saved);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_DRAWITEM: {
      const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
      if (item->CtlType == ODT_BUTTON) DrawButton(item, app);
      return TRUE;
    }
    case WM_COMMAND:
      if (HIWORD(wparam) == BN_CLICKED) {
        const int id = LOWORD(wparam);
        if (id >= 1000 && id < 1000 + int(kPadBindingCount)) {
          BeginMappingCapture(*app, size_t(id - 1000));
          return 0;
        }
        if (id >= 1100 && id < 1100 + int(kPadBindingCount)) {
          const size_t index = size_t(id - 1100);
          app->mapping_values[index] = L"Escape";
          app->mapping_modified[index] = true;
          app->mapping_capture_index = kPadBindingCount;
          UpdateMappingButtons(*app);
          return 0;
        }
        if (id == kMappingResetButton) {
          for (size_t i = 0; i < kPadBindingCount; ++i) {
            app->mapping_modified[i] = app->mapping_present[i] ||
                                       app->mapping_values[i] !=
                                           Utf8ToWide(kPadBindings[i].default_key);
            app->mapping_values[i] = Utf8ToWide(kPadBindings[i].default_key);
          }
          app->mapping_capture_index = kPadBindingCount;
          UpdateMappingButtons(*app);
          return 0;
        }
        if (id == kMappingSaveButton) {
          if (HasDuplicateMappings(*app) &&
              MessageBoxW(window,
                          L"Two or more pad inputs share a key. Save these duplicates anyway?",
                          kWindowTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            return 0;
          }
          std::wstring error;
          if (!SaveMappingValues(*app, error)) {
            MessageBoxW(window, error.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
            return 0;
          }
          DestroyWindow(window);
          return 0;
        }
        if (id == kMappingCancelButton) {
          DestroyWindow(window);
          return 0;
        }
      }
      break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      if (app->mapping_capture_index < kPadBindingCount) {
        if (wparam == VK_ESCAPE) {
          app->mapping_capture_index = kPadBindingCount;
          UpdateMappingButtons(*app);
          return 0;
        }
        const KeyName* key = FindKeyByVirtualKey(UINT(wparam), lparam);
        if (key) {
          FinishMappingCapture(*app, key);
        } else {
          app->mapping_status = L"That key is not supported by the game; try another key.";
          InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
      }
      break;
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_DPICHANGED: {
      app->mapping_dpi = HIWORD(wparam) ? HIWORD(wparam) : kDefaultDpi;
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      CreateMappingFonts(*app);
      LayoutMappingControls(*app);
      InvalidateRect(window, nullptr, TRUE);
      return 0;
    }
    case WM_NCDESTROY:
      if (app->mapping_window == window) app->mapping_window = nullptr;
      app->mapping_capture_index = kPadBindingCount;
      for (HFONT* font : {&app->mapping_regular_font, &app->mapping_bold_font,
                          &app->mapping_title_font, &app->mapping_regular_paint_font,
                          &app->mapping_bold_paint_font, &app->mapping_title_paint_font}) {
        if (*font) DeleteObject(*font);
        *font = nullptr;
      }
      app->mapping_key_buttons.fill(nullptr);
      app->mapping_escape_buttons.fill(nullptr);
      app->mapping_reset_button = nullptr;
      app->mapping_save_button = nullptr;
      app->mapping_cancel_button = nullptr;
      if (app->window && IsWindow(app->window)) {
        EnableWindow(app->window, TRUE);
        SetForegroundWindow(app->window);
      }
      return DefWindowProcW(window, message, wparam, lparam);
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void OpenKeyMapping(AppState& app) {
  if (app.mapping_window) {
    SetForegroundWindow(app.mapping_window);
    return;
  }
  std::wstring error;
  if (!LoadMappingValues(app, error)) {
    MessageBoxW(app.window, error.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
    return;
  }
  RECT bounds{0, 0, ScaleForDpi(kMappingClientWidth, app.dpi),
              ScaleForDpi(kMappingClientHeight, app.dpi)};
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  AdjustWindowRectForDpi(&bounds, style, app.dpi);
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  RECT owner_bounds{};
  GetWindowRect(app.window, &owner_bounds);
  int x = owner_bounds.left + ((owner_bounds.right - owner_bounds.left) - width) / 2;
  int y = owner_bounds.top + ((owner_bounds.bottom - owner_bounds.top) - height) / 2;
  // A window manager may not exist (and Wine/Xvfb have none), so centre on the
  // monitor's work area and keep the whole dialog on screen: the launcher can be
  // smaller than the page, which otherwise pushes the pad-input labels and the
  // Reset button off the left edge.
  RECT work{};
  if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
    const int work_left = int(work.left);
    const int work_top = int(work.top);
    const int work_width = int(work.right) - work_left;
    const int work_height = int(work.bottom) - work_top;
    x = work_left + (work_width - width) / 2;
    y = work_top + (work_height - height) / 2;
    if (work_width > width) {
      x = std::clamp(x, work_left, work_left + work_width - width);
    }
    if (work_height > height) {
      y = std::clamp(y, work_top, work_top + work_height - height);
    }
  }
  HWND mapping = CreateWindowExW(WS_EX_DLGMODALFRAME, L"WWE13KeyboardMappingWindow",
                                  L"WWE '13 — Keyboard controls", style, x, y, width, height,
                                  app.window, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!mapping) {
    MessageBoxW(app.window, L"The keyboard controls page could not be opened.", kWindowTitle,
                MB_OK | MB_ICONERROR);
    return;
  }
  EnableWindow(app.window, FALSE);
  BOOL dark_mode = TRUE;
  DwmSetWindowAttribute(mapping, 20, &dark_mode, sizeof(dark_mode));
  ShowWindow(mapping, SW_SHOWNORMAL);
  UpdateWindow(mapping);
  SetForegroundWindow(mapping);
}

void DrawComboItem(const DRAWITEMSTRUCT* item) {
  const bool edit_face = (item->itemState & ODS_COMBOBOXEDIT) != 0;
  const int item_index = edit_face
                             ? int(SendMessageW(item->hwndItem, CB_GETCURSEL, 0, 0))
                             : (item->itemID == UINT(-1) ? -1 : int(item->itemID));
  const bool selected = !edit_face && (item->itemState & ODS_SELECTED) != 0;
  HBRUSH brush = CreateSolidBrush(selected ? RGB(68, 83, 103) : kControl);
  FillRect(item->hDC, &item->rcItem, brush);
  DeleteObject(brush);
  if (item_index < 0) return;
  const LRESULT text_length = SendMessageW(item->hwndItem, CB_GETLBTEXTLEN,
                                            WPARAM(item_index), 0);
  if (text_length == CB_ERR) return;
  std::vector<wchar_t> text(size_t(text_length) + 1, L'\0');
  if (SendMessageW(item->hwndItem, CB_GETLBTEXT, WPARAM(item_index), LPARAM(text.data())) ==
      CB_ERR) {
    return;
  }
  RECT bounds = item->rcItem;
  bounds.left += 11;
  DrawTextLine(item->hDC, reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0)),
               IsWindowEnabled(item->hwndItem) ? kText : RGB(139, 141, 146), text.data(),
               bounds);
  if (item->itemState & ODS_FOCUS) {
    RECT focus = item->rcItem;
    InflateRect(&focus, -3, -3);
    DrawFocusRect(item->hDC, &focus);
  }
}

void RefreshGpuCombo(AppState& app) {
  SendMessageW(app.gpu_combo, CB_RESETCONTENT, 0, 0);
  AddComboValue(app.gpu_combo, L"Automatic");
  for (const GpuOption& gpu : app.gpus) {
    const std::wstring label = gpu.name + L" (" + GpuId(gpu) + L", " +
                               GpuTypeLabel(gpu.device_type) + L")";
    AddComboValue(app.gpu_combo, label.c_str());
  }
  SetComboItemHeights(app.gpu_combo, app.dpi);
  ResizeOwnerDrawComboToItems(app.gpu_combo);
}

void UpdateGpuSettingFromCombo(AppState& app) {
  const int selected = int(SendMessageW(app.gpu_combo, CB_GETCURSEL, 0, 0));
  if (selected <= 0) {
    app.settings.gpu_id.clear();
    app.settings.gpu_name.clear();
    return;
  }
  const GpuOption& gpu = app.gpus[size_t(selected - 1)];
  app.settings.gpu_id = GpuId(gpu);
  app.settings.gpu_name = gpu.name;
}

void SaveSettingsAndRefresh(AppState& app) {
  app.settings_write_failed = !WriteIni(app);
  SetComboSelections(app);
  UpdateControlState(app);
}

int CALLBACK BrowseCallback(HWND dialog, UINT message, LPARAM, LPARAM data) {
  if (message == BFFM_INITIALIZED && data) {
    SendMessageW(dialog, BFFM_SETSELECTIONW, TRUE, data);
  }
  return 0;
}

bool ChooseFolder(HWND owner, const std::wstring& initial, std::wstring& selected) {
  BROWSEINFOW info{};
  info.hwndOwner = owner;
  info.lpszTitle = L"Choose the WWE '13 game folder";
  info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
  info.lpfn = BrowseCallback;
  info.lParam = LPARAM(initial.c_str());
  PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
  if (!item) return false;
  wchar_t path[MAX_PATH]{};
  const bool okay = SHGetPathFromIDListW(item, path) != FALSE;
  CoTaskMemFree(item);
  if (okay) selected = path;
  return okay;
}

bool EnsureDirectory(const std::wstring& path) {
  if (DirectoryExists(path)) return true;
  if (CreateDirectoryW(path.c_str(), nullptr)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS && DirectoryExists(path);
}

void CreateBugReport(AppState& app) {
  std::wstring archive_path;
  std::wstring error_message;
  if (!CreateBugReportArchive(app, archive_path, error_message)) {
    MessageBoxW(app.window, error_message.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
    return;
  }
  const std::wstring message = L"Bug report created:\r\n" + archive_path +
                               L"\r\n\r\nAttach this file to a GitHub issue.\r\n" +
                               L"The report stays on this PC; it is not uploaded.";
  MessageBoxW(app.window, message.c_str(), kWindowTitle, MB_OK | MB_ICONINFORMATION);
  const std::wstring arguments = L"/select,\"" + archive_path + L"\"";
  const HINSTANCE explorer = ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(),
                                           nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(explorer) <= 32) {
    MessageBoxW(app.window, L"The report was created, but Explorer could not select it.",
                kWindowTitle, MB_OK | MB_ICONWARNING);
  }
}

bool StartGame(AppState& app, bool headless = false) {
  KeepNewestGameLogs(app);
  if (!GameFilesPresent(app.settings.game_folder)) {
    MessageBoxW(app.window,
                L"The selected folder must contain both default.xex and default.xexp (title update 2.0.1.0).\n\nChoose a folder containing both files, then press Play.",
                kWindowTitle, MB_OK | MB_ICONWARNING);
    return false;
  }
  if (!FileExists(app.game_exe)) {
    MessageBoxW(app.window, L"wwe13.exe was not found next to the launcher. Reinstall or rebuild the game files.",
                kWindowTitle, MB_OK | MB_ICONERROR);
    return false;
  }
  if (!EnsureDirectory(app.logs_dir) || !EnsureDirectory(app.userdata_dir)) {
    MessageBoxW(app.window, L"The launcher could not create the logs or userdata folder next to itself.",
                kWindowTitle, MB_OK | MB_ICONERROR);
    return false;
  }

  std::vector<std::wstring> arguments = BuildArguments(app);
  std::wstring command = BuildCommandLine(app.game_exe, arguments);
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  std::vector<wchar_t> environment = BuildChildEnvironment(app);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(app.game_exe.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                      CREATE_UNICODE_ENVIRONMENT, environment.data(), nullptr, &startup, &process)) {
    wchar_t message[512]{};
    swprintf_s(message, L"Could not start wwe13.exe (Windows error %lu).", GetLastError());
    MessageBoxW(app.window, message, kWindowTitle, MB_OK | MB_ICONERROR);
    return false;
  }
  if (headless) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
  }
  ShowWindow(app.window, SW_MINIMIZE);
  CloseHandle(process.hThread);
  if (app.child_process) CloseHandle(app.child_process);
  app.child_process = process.hProcess;
  app.child_check_deadline = GetTickCount64() + 5000;
  SetTimer(app.window, kChildPollTimer, 250, nullptr);
  return true;
}

void PollChildProcess(AppState& app) {
  if (!app.child_process) {
    KillTimer(app.window, kChildPollTimer);
    return;
  }
  DWORD exit_code = STILL_ACTIVE;
  if (GetExitCodeProcess(app.child_process, &exit_code) && exit_code != STILL_ACTIVE) {
    KillTimer(app.window, kChildPollTimer);
    CloseHandle(app.child_process);
    app.child_process = nullptr;
    if (exit_code != 0) {
      ShowWindow(app.window, SW_RESTORE);
      wchar_t message[512]{};
      swprintf_s(message, L"wwe13.exe stopped shortly after launch with error code %lu.", exit_code);
      MessageBoxW(app.window, message, kWindowTitle, MB_OK | MB_ICONERROR);
    }
    return;
  }
  if (GetTickCount64() >= app.child_check_deadline) {
    KillTimer(app.window, kChildPollTimer);
    CloseHandle(app.child_process);
    app.child_process = nullptr;
  }
}

void LayoutMainControls(AppState& app) {
  const UINT dpi = app.dpi;
  auto move = [&](HWND control, int x, int y, int width, int height) {
    if (control) {
      MoveWindow(control, ScaleForDpi(x, dpi), ScaleForDpi(y, dpi), ScaleForDpi(width, dpi),
                 ScaleForDpi(height, dpi), TRUE);
    }
  };
  auto layout_combo = [&](HWND combo, int x, int y, int width) {
    if (!combo) return;
    SetComboItemHeights(combo, dpi);
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    const size_t item_count = count == CB_ERR ? 0 : size_t(count);
    MoveWindow(combo, ScaleForDpi(x, dpi), ScaleForDpi(y, dpi), ScaleForDpi(width, dpi),
               OwnerDrawComboHeight(item_count, dpi), TRUE);
    ResizeOwnerDrawComboToItems(combo);
  };
  layout_combo(app.resolution_combo, 40, 150, 460);
  layout_combo(app.anti_aliasing_combo, 520, 150, 480);
  layout_combo(app.frame_rate_combo, 40, 232, 460);
  layout_combo(app.display_combo, 520, 232, 480);
  layout_combo(app.gpu_combo, 40, 296, 960);
  move(app.browse_button, 46, 476, 190, 42);
  move(app.bug_report_button, 250, 476, 220, 42);
  move(app.keyboard_controls_button, 490, 476, 250, 42);
  move(app.auto_start_checkbox, 40, 586, 550, 40);
  move(app.play_button, 808, 586, 120, 48);
  move(app.exit_button, 940, 586, 72, 48);
}

void CreateControls(AppState& app, HWND window) {
  app.window = window;
  app.dpi = GetWindowDpi(window);
  CreateMainFonts(app);

  std::vector<std::wstring> resolutions;
  for (const ResolutionOption& option : kResolutionOptions) resolutions.emplace_back(option.label);
  AddOwnerDrawCombo(window, kResolutionCombo, app.resolution_combo, 0, 0, 1, resolutions,
                    app.regular_font);

  std::vector<std::wstring> anti_aliasing_options;
  for (const AntiAliasingOption& option : kAntiAliasingOptions) {
    anti_aliasing_options.emplace_back(option.label);
  }
  AddOwnerDrawCombo(window, kAntiAliasingCombo, app.anti_aliasing_combo, 0, 0, 1,
                    anti_aliasing_options, app.regular_font);

  std::vector<std::wstring> frame_rates;
  for (const FrameRateOption& option : kFrameRateOptions) {
    if (option.enabled) frame_rates.emplace_back(option.label);
  }
  AddOwnerDrawCombo(window, kFrameRateCombo, app.frame_rate_combo, 0, 0, 1, frame_rates,
                    app.regular_font);

  std::vector<std::wstring> displays;
  for (const DisplayOption& option : kDisplayOptions) displays.emplace_back(option.label);
  AddOwnerDrawCombo(window, kDisplayCombo, app.display_combo, 0, 0, 1, displays,
                    app.regular_font);

  AddOwnerDrawCombo(window, kGpuCombo, app.gpu_combo, 0, 0, 1, {}, app.regular_font);
  RefreshGpuCombo(app);
  AddOwnerDrawButton(window, kBrowseButton, L"Choose folder...", app.browse_button, 0, 0, 1, 1,
                     app.bold_font);
  AddOwnerDrawButton(window, kBugReportButton, L"CREATE BUG REPORT", app.bug_report_button,
                     0, 0, 1, 1, app.bold_font);
  AddOwnerDrawButton(window, kKeyboardControlsButton, L"Keyboard controls...",
                     app.keyboard_controls_button, 0, 0, 1, 1, app.bold_font);
  app.auto_start_checkbox = CreateWindowExW(
      0, L"BUTTON", L"Use these settings every time",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, window,
      HMENU(INT_PTR(kAutoStartCheckbox)), GetModuleHandleW(nullptr), nullptr);
  SetControlFont(app.auto_start_checkbox, app.regular_font);
  AddOwnerDrawButton(window, kPlayButton, L"PLAY", app.play_button, 0, 0, 1, 1,
                     app.bold_font);
  AddOwnerDrawButton(window, kExitButton, L"EXIT", app.exit_button, 0, 0, 1, 1,
                     app.bold_font);
  LayoutMainControls(app);
  SetComboSelections(app);
  UpdateControlState(app);
}

void HandleSelectionChange(AppState& app, int control_id) {
  if (app.initializing_controls) return;
  if (control_id == kResolutionCombo) {
    const int index = int(SendMessageW(app.resolution_combo, CB_GETCURSEL, 0, 0));
    if (index >= 0 && size_t(index) < std::size(kResolutionOptions)) {
      app.settings.resolution = kResolutionOptions[index].id;
      app.settings.resolution_explicit = true;
    }
  } else if (control_id == kAntiAliasingCombo) {
    const int index = int(SendMessageW(app.anti_aliasing_combo, CB_GETCURSEL, 0, 0));
    if (index >= 0 && size_t(index) < std::size(kAntiAliasingOptions)) {
      app.settings.anti_aliasing = kAntiAliasingOptions[index].id;
    }
  } else if (control_id == kFrameRateCombo) {
    const int index = int(SendMessageW(app.frame_rate_combo, CB_GETCURSEL, 0, 0));
    int selected = 0;
    for (const FrameRateOption& option : kFrameRateOptions) {
      if (!option.enabled) continue;
      if (selected++ == index) {
        app.settings.frame_rate = option.id;
        app.settings.frame_rate_explicit = true;
        break;
      }
    }
  } else if (control_id == kDisplayCombo) {
    const int index = int(SendMessageW(app.display_combo, CB_GETCURSEL, 0, 0));
    app.settings.display = kDisplayOptions[index == 1 ? 1 : 0].id;
  } else if (control_id == kGpuCombo) {
    UpdateGpuSettingFromCombo(app);
  }
  SaveSettingsAndRefresh(app);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  AppState* app = g_app;
  switch (message) {
    case WM_CREATE:
      if (app) CreateControls(*app, window);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      const int saved = app ? SetPaintDpiTransform(dc, app->dpi) : 0;
      RECT client{};
      GetClientRect(window, &client);
      if (app && app->dpi != kDefaultDpi) {
        client.right = MulDiv(client.right, int(kDefaultDpi), int(app->dpi));
        client.bottom = MulDiv(client.bottom, int(kDefaultDpi), int(app->dpi));
      }
      if (app) DrawBackground(*app, dc, client);
      if (saved) RestoreDC(dc, saved);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_DPICHANGED: {
      if (!app) return 0;
      app->dpi = HIWORD(wparam) ? HIWORD(wparam) : kDefaultDpi;
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      CreateMainFonts(*app);
      LayoutMainControls(*app);
      InvalidateRect(window, nullptr, TRUE);
      return 0;
    }
    case WM_DRAWITEM: {
      const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
      if (item->CtlType == ODT_BUTTON) DrawButton(item, app);
      else if (item->CtlType == ODT_COMBOBOX) DrawComboItem(item);
      return TRUE;
    }
    case WM_COMMAND:
      if (!app) break;
      if (HIWORD(wparam) == CBN_SELCHANGE) {
        HandleSelectionChange(*app, LOWORD(wparam));
        return 0;
      }
      if (HIWORD(wparam) == BN_CLICKED) {
        if (LOWORD(wparam) == kBrowseButton) {
          std::wstring folder;
          if (ChooseFolder(window, app->settings.game_folder, folder)) {
            app->settings.game_folder = folder;
            SaveSettingsAndRefresh(*app);
          }
          return 0;
        }
        if (LOWORD(wparam) == kPlayButton) {
          StartGame(*app);
          return 0;
        }
        if (LOWORD(wparam) == kBugReportButton) {
          CreateBugReport(*app);
          return 0;
        }
        if (LOWORD(wparam) == kKeyboardControlsButton) {
          OpenKeyMapping(*app);
          return 0;
        }
        if (LOWORD(wparam) == kAutoStartCheckbox) {
          app->settings.auto_start = !app->settings.auto_start;
          SaveSettingsAndRefresh(*app);
          InvalidateRect(app->auto_start_checkbox, nullptr, FALSE);
          return 0;
        }
        if (LOWORD(wparam) == kExitButton) {
          DestroyWindow(window);
          return 0;
        }
      }
      break;
    case WM_TIMER:
      if (app && wparam == kChildPollTimer) PollChildProcess(*app);
      return 0;
    case WM_DESTROY:
      if (app) {
        if (app->mapping_window && IsWindow(app->mapping_window)) {
          DestroyWindow(app->mapping_window);
        }
        if (app->child_process) CloseHandle(app->child_process);
        app->child_process = nullptr;
        if (app->regular_font) DeleteObject(app->regular_font);
        if (app->bold_font) DeleteObject(app->bold_font);
        if (app->title_font) DeleteObject(app->title_font);
        if (app->regular_paint_font) DeleteObject(app->regular_paint_font);
        if (app->bold_paint_font) DeleteObject(app->bold_paint_font);
        if (app->title_paint_font) DeleteObject(app->title_paint_font);
      }
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void NormalizeSettings(AppState& app) {
  if (app.settings.gpu_id.empty()) {
    app.settings.gpu_name.clear();
    return;
  }
  const GpuOption* gpu = FindGpuById(app, app.settings.gpu_id);
  if (!gpu) {
    app.settings.gpu_id.clear();
    app.settings.gpu_name.clear();
    return;
  }
  app.settings.gpu_id = GpuId(*gpu);
  app.settings.gpu_name = gpu->name;
}

void ApplyUiTestSimulation(AppState& app) {
  wchar_t value[64]{};
  const DWORD length = GetEnvironmentVariableW(L"WWE13_LAUNCHER_TEST_ASSUME_INTEGRATED", value,
                                                 DWORD(std::size(value)));
  if (!length || length >= std::size(value)) return;
  GpuOption* gpu = FindGpuById(app, value);
  if (!gpu) {
    app.gpu_error = L"Test simulation ignored: integrated-device ID is not in this Vulkan list.";
    return;
  }
  gpu->device_type = kVkPhysicalDeviceTypeIntegratedGpu;
  app.simulated_auto_gpu_id = GpuId(*gpu);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  const bool shift_pressed_at_start = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  EnablePerMonitorDpiAwareness();
  const std::wstring module_path = ModulePath();
  if (module_path.empty()) {
    MessageBoxW(nullptr, L"Could not determine the launcher folder.", kWindowTitle,
                MB_OK | MB_ICONERROR);
    return 1;
  }
  AppState app;
  app.dpi = GetSystemDpi();
  app.launcher_path = module_path;
  app.exe_dir = ParentDirectory(module_path);
  app.settings_path = JoinPath(app.exe_dir, L"wwe13-enhanced.ini");
  app.default_game_folder = JoinPath(app.exe_dir, L"WWE 13");
  app.game_exe = JoinPath(app.exe_dir, L"wwe13.exe");
  app.userdata_dir = JoinPath(app.exe_dir, L"userdata");
  app.logs_dir = JoinPath(app.exe_dir, L"logs");
  LoadSettings(app);
  EnumerateVulkanGpus(app);
  NormalizeSettings(app);
  if (app.legacy_gpu_setting || app.needs_schema_upgrade) {
    app.settings_write_failed = !WriteIni(app);
  }

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::wstring> arguments;
  for (int i = 1; argv && i < argc; ++i) arguments.emplace_back(argv[i]);
  const bool print_command = HasOption(arguments, L"--print-command");
  const bool assume_integrated = HasOptionPrefix(arguments, L"--assume-integrated=");
  const bool settings_override = HasOption(arguments, L"--settings") || shift_pressed_at_start;
  if (argv) LocalFree(argv);
  if (print_command) return RunPrintCommand(app, arguments);
  if (assume_integrated) {
    WriteReportToParentConsole(
        L"ERROR: --assume-integrated is test-only and requires --print-command.\r\n");
    return 2;
  }

  ApplyUiTestSimulation(app);

  if (app.settings.auto_start && !settings_override && StartGame(app, true)) return 0;

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = WindowProcedure;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = nullptr;
  window_class.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&window_class)) return 2;
  WNDCLASSEXW mapping_class{};
  mapping_class.cbSize = sizeof(mapping_class);
  mapping_class.style = CS_HREDRAW | CS_VREDRAW;
  mapping_class.lpfnWndProc = MappingWindowProcedure;
  mapping_class.hInstance = instance;
  mapping_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  mapping_class.hbrBackground = nullptr;
  mapping_class.lpszClassName = L"WWE13KeyboardMappingWindow";
  if (!RegisterClassExW(&mapping_class)) return 2;
  g_app = &app;

  RECT bounds{0, 0, ScaleForDpi(kMainClientWidth, app.dpi),
              ScaleForDpi(kMainClientHeight, app.dpi)};
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRectForDpi(&bounds, style, app.dpi);
  HWND window = CreateWindowExW(0, kWindowClass, kWindowTitle, style,
                                CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                                bounds.bottom - bounds.top, nullptr, nullptr, instance, nullptr);
  if (!window) return 3;
  BOOL dark_mode = TRUE;
  DwmSetWindowAttribute(window, 20, &dark_mode, sizeof(dark_mode));
  ShowWindow(window, show_command == SW_HIDE ? SW_SHOWNORMAL : show_command);
  UpdateWindow(window);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return int(message.wParam);
}
