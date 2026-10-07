#include "release_diagnostics.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "wwe13_build_info.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <processthreadsapi.h>
#include <psapi.h>
#include <winreg.h>
#include <winternl.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <ucontext.h>
#endif

REXCVAR_DECLARE(std::int32_t, resolution_scale);
REXCVAR_DECLARE(bool, fullscreen);
REXCVAR_DECLARE(bool, debug_ui);
REXCVAR_DECLARE(std::int32_t, vulkan_device);
REXCVAR_DECLARE(std::string, vulkan_device_id);
REXCVAR_DECLARE(std::string, log_file);

namespace wwe13 {
namespace {

constexpr char kTestCrashEnv[] = "WWE13_TEST_CRASH";
constexpr auto kTestCrashDelay = std::chrono::seconds(8);

const char* EnvValue(const char* name) {
  const char* value = std::getenv(name);
  return value && *value ? value : "unset";
}

std::string ReadFirstMatchingLine(const char* path, std::string_view key) {
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    // The key is followed by ':', '=' or whitespace before the separator
    // (/proc/cpuinfo uses "model name<TAB>: ..."). A plain compare(0, n, key)
    // with n found from ':' would compare one character too many and never
    // match, so check the prefix and the next character explicitly.
    if (line.compare(0, key.size(), key) != 0) {
      continue;
    }
    const char next = line.size() > key.size() ? line[key.size()] : '\0';
    if (next != ':' && next != '=' && next != ' ' && next != '\t') {
      continue;
    }
    const std::size_t separator = line.find(':', key.size());
    const std::size_t equals = line.find('=', key.size());
    const std::size_t split = separator == std::string::npos ? equals : separator;
    if (split == std::string::npos) {
      continue;
    }
    std::string value = line.substr(split + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                              value.front() == '"')) {
      value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                              value.back() == '"' || value.back() == '\r')) {
      value.pop_back();
    }
    return value;
  }
  return "unknown";
}

std::string CpuName() {
#ifdef _WIN32
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                    KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
    return "unknown";
  }
  std::array<wchar_t, 256> value{};
  DWORD type = 0;
  DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
  const LONG result = RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type,
                                      reinterpret_cast<BYTE*>(value.data()), &bytes);
  RegCloseKey(key);
  if (result != ERROR_SUCCESS || type != REG_SZ) {
    return "unknown";
  }
  const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, value.data(), -1, nullptr, 0, nullptr,
                                            nullptr);
  if (utf8_size <= 1) {
    return "unknown";
  }
  std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), -1, utf8.data(), utf8_size, nullptr, nullptr);
  utf8.resize(static_cast<std::size_t>(utf8_size - 1));
  return utf8;
#else
  return ReadFirstMatchingLine("/proc/cpuinfo", "model name");
#endif
}

std::string OsVersion() {
#ifdef _WIN32
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto get_version = ntdll ? reinterpret_cast<RtlGetVersionFn>(
                                      GetProcAddress(ntdll, "RtlGetVersion"))
                                : nullptr;
  RTL_OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  if (!get_version || get_version(&version) != 0) {
    return "Windows version unknown";
  }
  return "Windows " + std::to_string(version.dwMajorVersion) + "." +
         std::to_string(version.dwMinorVersion) + " build " +
         std::to_string(version.dwBuildNumber);
#else
  const std::string pretty = ReadFirstMatchingLine("/etc/os-release", "PRETTY_NAME");
  struct utsname info {};
  if (uname(&info) == 0) {
    return pretty + " (" + info.release + ")";
  }
  return pretty;
#endif
}

std::string CoreCountAndRam() {
#ifdef _WIN32
  SYSTEM_INFO system_info{};
  GetNativeSystemInfo(&system_info);
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  const std::uint64_t total_ram = GlobalMemoryStatusEx(&memory) ? memory.ullTotalPhys : 0;
  return std::to_string(system_info.dwNumberOfProcessors) + " cores, " +
         std::to_string(total_ram / (1024 * 1024)) + " MiB RAM";
#else
  const long cores = sysconf(_SC_NPROCESSORS_ONLN);
  struct sysinfo memory {};
  const std::uint64_t total_ram = sysinfo(&memory) == 0
                                      ? std::uint64_t(memory.totalram) * memory.mem_unit
                                      : 0;
  return std::to_string(cores > 0 ? cores : 0) + " cores, " +
         std::to_string(total_ram / (1024 * 1024)) + " MiB RAM";
#endif
}

// Reads a sysfs attribute whose entire content is the value (no key): returns
// the first line with surrounding whitespace removed, or an empty string when
// the file cannot be read. Never throws.
std::string ReadTrimmedFirstLine(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::string line;
  std::getline(input, line);
  const std::size_t first = line.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const std::size_t last = line.find_last_not_of(" \t\r\n");
  return line.substr(first, last - first + 1);
}

std::string PowerState() {
#ifdef _WIN32
  SYSTEM_POWER_STATUS status{};
  if (!GetSystemPowerStatus(&status) || status.ACLineStatus == 255) {
    return "unknown";
  }
  if (status.ACLineStatus == 0) {
    return status.BatteryLifePercent == 255
               ? "battery"
               : "battery " + std::to_string(status.BatteryLifePercent) + "%";
  }
  return "AC";
#else
  try {  // directory iteration can throw on increment; a log line must never stop the game
  std::vector<std::string> battery_capacities;
  std::error_code error;
  std::filesystem::directory_iterator entries("/sys/class/power_supply", error);
  if (error) {
    return "unknown";
  }
  for (const auto& entry : entries) {
    const std::string type = ReadTrimmedFirstLine(entry.path() / "type");
    if (type == "Mains") {
      if (ReadTrimmedFirstLine(entry.path() / "online") == "1") {
        return "AC";
      }
    } else if (type == "Battery") {
      battery_capacities.push_back(ReadTrimmedFirstLine(entry.path() / "capacity"));
    }
  }
  if (battery_capacities.empty()) {
    return "no battery (desktop)";
  }
  for (const std::string& capacity : battery_capacities) {
    if (!capacity.empty()) {
      return "battery " + capacity + "%";
    }
  }
  return "battery";
  } catch (...) {
    return "unknown";
  }
#endif
}

std::string LogFilePath() {
  const std::string configured = REXCVAR_GET(log_file);
  if (!configured.empty()) {
    return configured;
  }
  return (rex::filesystem::GetExecutableFolder() / "wwe13.log").string();
}

bool HasRegularFile(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error) && !error;
}

void FlushAllLoggerCategories() {
  for (const auto& category : rex::GetAllCategories()) {
    if (category.logger) {
      category.logger->flush();
    }
  }
}

#ifdef _WIN32

HANDLE g_crash_log = INVALID_HANDLE_VALUE;
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_exception_filter = nullptr;
HANDLE g_flush_request_event = nullptr;
HANDLE g_flush_complete_event = nullptr;

DWORD WINAPI CrashFlushWorker(void*) {
  while (WaitForSingleObject(g_flush_request_event, INFINITE) == WAIT_OBJECT_0) {
    FlushAllLoggerCategories();
    if (!SetEvent(g_flush_complete_event)) {
      return 1;
    }
  }
  return 0;
}

std::string WindowsThreadName(DWORD thread_id) {
  HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread_id);
  if (!thread) {
    return "unknown";
  }
  using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
  const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
  const auto get_description = kernel ? reinterpret_cast<GetThreadDescriptionFn>(
                                            GetProcAddress(kernel, "GetThreadDescription"))
                                      : nullptr;
  PWSTR description = nullptr;
  std::string result = "unknown";
  if (get_description && SUCCEEDED(get_description(thread, &description)) && description) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, description, -1, nullptr, 0, nullptr, nullptr);
    if (size > 1) {
      result.resize(static_cast<std::size_t>(size));
      WideCharToMultiByte(CP_UTF8, 0, description, -1, result.data(), size, nullptr, nullptr);
      result.resize(static_cast<std::size_t>(size - 1));
    }
    LocalFree(description);
  }
  CloseHandle(thread);
  return result;
}

void AppendToCrashLog(const char* text) {
  if (g_crash_log == INVALID_HANDLE_VALUE || !text) {
    return;
  }
  DWORD written = 0;
  WriteFile(g_crash_log, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
  FlushFileBuffers(g_crash_log);
}

// Unwinds from the faulting context (x64 unwind data) so the report names the whole call chain:
// exe frames as offsets for llvm-symbolizer/addr2line on the .pdb build, other modules by name.
void AppendStackToCrashLog(const CONTEXT* fault_context) {
#if defined(_M_X64) || defined(__x86_64__)
  if (!fault_context) {
    return;
  }
  CONTEXT context = *fault_context;
  const auto exe_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
  AppendToCrashLog("backtrace (exe offsets):\n");
  for (int frame = 0; frame < 48 && context.Rip != 0; ++frame) {
    std::array<char, 384> line{};
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(context.Rip), &module);
    const auto module_base = reinterpret_cast<std::uintptr_t>(module);
    if (module_base == exe_base) {
      std::snprintf(line.data(), line.size(), "  #%d exe+0x%llX\n", frame,
                    static_cast<unsigned long long>(context.Rip - exe_base));
    } else {
      char module_name[MAX_PATH] = "?";
      if (module) {
        GetModuleFileNameA(module, module_name, MAX_PATH);
      }
      const char* short_name = std::strrchr(module_name, '\\');
      std::snprintf(line.data(), line.size(), "  #%d %s+0x%llX (abs 0x%llX)\n", frame,
                    short_name ? short_name + 1 : module_name,
                    static_cast<unsigned long long>(context.Rip - module_base),
                    static_cast<unsigned long long>(context.Rip));
    }
    AppendToCrashLog(line.data());
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
    if (!function) {
      // Leaf function: the return address is at the top of the stack.
      context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
      context.Rsp += 8;
      continue;
    }
    void* handler_data = nullptr;
    DWORD64 establisher_frame = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context,
                     &handler_data, &establisher_frame, nullptr);
  }
#else
  (void)fault_context;
#endif
}

LONG WINAPI Wwe13UnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
  if (info && info->ExceptionRecord) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const DWORD code = record->ExceptionCode;
    const std::uintptr_t host_pc = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
    std::uintptr_t fault_address = host_pc;
    const bool has_fault_address = record->ExceptionAddress != nullptr;
    bool report_fault_address = has_fault_address;
    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
      fault_address = static_cast<std::uintptr_t>(record->ExceptionInformation[1]);
      report_fault_address = true;
    }
    const DWORD thread_id = GetCurrentThreadId();
    const std::string thread_name = WindowsThreadName(thread_id);
    std::array<char, 64> address_text{};
    if (report_fault_address) {
      std::snprintf(address_text.data(), address_text.size(), "0x%llX",
                    static_cast<unsigned long long>(fault_address));
    } else {
      std::snprintf(address_text.data(), address_text.size(), "not-applicable");
    }
    std::array<char, 1024> message{};
    std::snprintf(message.data(), message.size(),
                  "\n========== WWE '13 CRASH REPORT BEGIN ==========\n"
                  "exception_code=0x%08lX faulting_address=%s host_pc=0x%llX host_offset=0x%llX\n"
                  "host_function=unresolved guest_function=unresolved\n"
                  "thread=%s thread_id=%lu\n"
                  "Please attach this log to a bug report.\n"
                  "========== WWE '13 CRASH REPORT END ============\n",
                  static_cast<unsigned long>(code), address_text.data(),
                  static_cast<unsigned long long>(host_pc),
                  static_cast<unsigned long long>(
                      host_pc - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))),
                  thread_name.c_str(),
                  static_cast<unsigned long>(thread_id));
    AppendToCrashLog(message.data());
    AppendStackToCrashLog(info->ContextRecord);
    if (g_flush_request_event && g_flush_complete_event &&
        SetEvent(g_flush_request_event)) {
      WaitForSingleObject(g_flush_complete_event, 3000);
    }
  }
  if (g_previous_exception_filter && g_previous_exception_filter != Wwe13UnhandledExceptionFilter) {
    return g_previous_exception_filter(info);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void InstallPlatformCrashHandler(const std::string& log_path) {
  int wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, log_path.c_str(), -1,
                                      nullptr, 0);
  std::wstring wide_path;
  if (wide_size > 1) {
    wide_path.resize(static_cast<std::size_t>(wide_size - 1));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, log_path.c_str(), -1, wide_path.data(),
                        wide_size);
  }
  if (!wide_path.empty()) {
    g_crash_log = CreateFileW(wide_path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  }
  if (g_crash_log == INVALID_HANDLE_VALUE) {
    REXLOG_WARN("[wwe13-crash] could not open configured log for direct crash notes");
  }
  g_flush_request_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g_flush_complete_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (g_flush_request_event && g_flush_complete_event) {
    HANDLE worker = CreateThread(nullptr, 0, CrashFlushWorker, nullptr, 0, nullptr);
    if (worker) {
      CloseHandle(worker);
    } else {
      CloseHandle(g_flush_request_event);
      CloseHandle(g_flush_complete_event);
      g_flush_request_event = nullptr;
      g_flush_complete_event = nullptr;
    }
  }
  g_previous_exception_filter = SetUnhandledExceptionFilter(Wwe13UnhandledExceptionFilter);
}

void StartTestCrashIfRequested() {
  const char* value = std::getenv(kTestCrashEnv);
  if (!value || std::strcmp(value, "1") != 0) {
    return;
  }
  REXLOG_WARN("[wwe13-test-crash] armed; deliberate access violation in 8 seconds");
  std::thread([] {
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const auto set_description = kernel ? reinterpret_cast<SetThreadDescriptionFn>(
                                              GetProcAddress(kernel, "SetThreadDescription"))
                                        : nullptr;
    if (set_description) {
      set_description(GetCurrentThread(), L"wwe13-test-crash");
    }
    std::this_thread::sleep_for(kTestCrashDelay);
    volatile std::uintptr_t invalid_address = 1;
    *reinterpret_cast<volatile std::uint32_t*>(invalid_address) = 0x13;
  }).detach();
}

#else

struct CrashEvent {
  int signal_number;
  std::uintptr_t fault_address;
  bool has_fault_address;
  std::uintptr_t host_pc;
  pid_t thread_id;
  char thread_name[16];
};

constexpr int kFatalSignals[] = {SIGSEGV, SIGILL, SIGFPE, SIGABRT, SIGBUS};
int g_crash_log_fd = -1;
// GH3/#17 diagnostic: when WWE13_CRASH_GUEST_CTX=1, the fatal-signal report also
// dumps the guest PPCContext (rbx), the guest memory base (r14) and the object /
// vtable words at the fault, to identify a wild indirect-call target. Read-only.
bool g_dump_guest_ctx = false;
// Load base of the executable, so the signal-safe report can print a module offset for addr2line.
std::uintptr_t g_exe_base = 0;
int g_request_pipe[2] = {-1, -1};
int g_ack_pipe[2] = {-1, -1};
volatile sig_atomic_t g_crash_in_progress = 0;

const char* SignalName(int signal_number) {
  switch (signal_number) {
    case SIGSEGV:
      return "SIGSEGV";
    case SIGILL:
      return "SIGILL";
    case SIGFPE:
      return "SIGFPE";
    case SIGABRT:
      return "SIGABRT";
    case SIGBUS:
      return "SIGBUS";
    default:
      return "signal";
  }
}

std::size_t AppendLiteral(char* output, std::size_t offset, std::size_t capacity,
                          const char* text) {
  while (*text && offset < capacity) {
    output[offset++] = *text++;
  }
  return offset;
}

std::size_t AppendUnsigned(char* output, std::size_t offset, std::size_t capacity,
                           std::uint64_t value, unsigned base, bool upper = false) {
  char digits[32];
  std::size_t count = 0;
  do {
    const unsigned digit = static_cast<unsigned>(value % base);
    digits[count++] = static_cast<char>(digit < 10 ? '0' + digit
                                                   : (upper ? 'A' : 'a') + (digit - 10));
    value /= base;
  } while (value && count < sizeof(digits));
  while (count && offset < capacity) {
    output[offset++] = digits[--count];
  }
  return offset;
}

void AppendHex(char* output, std::size_t& offset, std::size_t capacity,
               std::uintptr_t value) {
  offset = AppendLiteral(output, offset, capacity, "0x");
  offset = AppendUnsigned(output, offset, capacity, value, 16, true);
}

std::size_t AppendAddress(char* output, std::size_t offset, std::size_t capacity,
                          const CrashEvent& event) {
  if (!event.has_fault_address) {
    return AppendLiteral(output, offset, capacity, "not-applicable");
  }
  AppendHex(output, offset, capacity, event.fault_address);
  return offset;
}

void SignalSafeThreadName(char (&name)[16], pid_t thread_id) {
  constexpr char prefix[] = "/proc/self/task/";
  char path[64];
  std::size_t position = 0;
  for (char c : prefix) {
    if (!c) break;
    path[position++] = c;
  }
  position = AppendUnsigned(path, position, sizeof(path) - 6,
                            static_cast<std::uint64_t>(thread_id), 10);
  constexpr char suffix[] = "/comm";
  for (char c : suffix) {
    if (!c) break;
    path[position++] = c;
  }
  path[position] = '\0';
  const int descriptor = open(path, O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) {
    for (std::size_t i = 0; i < sizeof("unknown"); ++i) name[i] = "unknown"[i];
    return;
  }
  const ssize_t length = read(descriptor, name, sizeof(name) - 1);
  close(descriptor);
  if (length <= 0) {
    for (std::size_t i = 0; i < sizeof("unknown"); ++i) name[i] = "unknown"[i];
    return;
  }
  name[length < static_cast<ssize_t>(sizeof(name)) ? length : sizeof(name) - 1] = '\0';
  for (ssize_t i = 0; name[i]; ++i) {
    if (name[i] == '\n' || name[i] == '\r') {
      name[i] = '\0';
      break;
    }
  }
}

std::uintptr_t HostProgramCounter(void* signal_context) {
  if (!signal_context) {
    return 0;
  }
#if defined(__x86_64__) && defined(REG_RIP)
  const auto* context = static_cast<const ucontext_t*>(signal_context);
  return static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
  const auto* context = static_cast<const ucontext_t*>(signal_context);
  return static_cast<std::uintptr_t>(context->uc_mcontext.pc);
#else
  return 0;
#endif
}

void WriteCrashBlockSignalSafe(const CrashEvent& event) {
  char block[768];
  std::size_t length = 0;
  const std::size_t capacity = sizeof(block);
  length = AppendLiteral(block, length, capacity,
                         "\n========== WWE '13 CRASH REPORT BEGIN ==========\nexception=");
  length = AppendLiteral(block, length, capacity, SignalName(event.signal_number));
  length = AppendLiteral(block, length, capacity, " (");
  length = AppendUnsigned(block, length, capacity,
                          static_cast<std::uint64_t>(event.signal_number), 10);
  length = AppendLiteral(block, length, capacity, ") faulting_address=");
  length = AppendAddress(block, length, capacity, event);
  length = AppendLiteral(block, length, capacity, " host_pc=");
  AppendHex(block, length, capacity, event.host_pc);
  if (g_exe_base != 0 && event.host_pc >= g_exe_base) {
    length = AppendLiteral(block, length, capacity, " host_offset=");
    AppendHex(block, length, capacity, event.host_pc - g_exe_base);
  }
  length = AppendLiteral(block, length, capacity,
                         "\nhost_function=unresolved guest_function=unresolved\nthread=");
  length = AppendLiteral(block, length, capacity, event.thread_name);
  length = AppendLiteral(block, length, capacity, " thread_id=");
  length = AppendUnsigned(block, length, capacity,
                          static_cast<std::uint64_t>(event.thread_id), 10);
  length = AppendLiteral(block, length, capacity,
                         "\nPlease attach this log to a bug report.\n"
                         "========== WWE '13 CRASH REPORT END ============\n");

  const int output_fd = g_crash_log_fd >= 0 ? g_crash_log_fd : STDERR_FILENO;
  std::size_t written_total = 0;
  while (written_total < length) {
    const ssize_t written = write(output_fd, block + written_total, length - written_total);
    if (written <= 0) {
      break;
    }
    written_total += static_cast<std::size_t>(written);
  }
}

void WriteAllSignalSafe(const char* data, std::size_t length) {
  const int output_fd = g_crash_log_fd >= 0 ? g_crash_log_fd : STDERR_FILENO;
  std::size_t written_total = 0;
  while (written_total < length) {
    const ssize_t written = write(output_fd, data + written_total, length - written_total);
    if (written <= 0) {
      break;
    }
    written_total += static_cast<std::size_t>(written);
  }
}

// Registers, an unwound stack as module offsets and the executable mappings, so a single crash
// report is enough to resolve the faulting call chain with addr2line. backtrace() was primed at
// install time so the unwinder is already loaded; everything else is plain write()/read().
void WriteCrashDetailsSignalSafe(void* signal_context) {
  char block[2048];
  std::size_t length = 0;
  const std::size_t capacity = sizeof(block);
#if defined(__x86_64__) && defined(REG_RIP)
  if (signal_context) {
    const auto& gregs = static_cast<const ucontext_t*>(signal_context)->uc_mcontext.gregs;
    static constexpr struct {
      const char* name;
      int index;
    } kRegisters[] = {{"rax", REG_RAX}, {"rbx", REG_RBX}, {"rcx", REG_RCX}, {"rdx", REG_RDX},
                      {"rsi", REG_RSI}, {"rdi", REG_RDI}, {"rbp", REG_RBP}, {"rsp", REG_RSP},
                      {"r8", REG_R8},   {"r9", REG_R9},   {"r10", REG_R10}, {"r11", REG_R11},
                      {"r12", REG_R12}, {"r13", REG_R13}, {"r14", REG_R14}, {"r15", REG_R15}};
    length = AppendLiteral(block, length, capacity, "registers:");
    for (const auto& reg : kRegisters) {
      length = AppendLiteral(block, length, capacity, " ");
      length = AppendLiteral(block, length, capacity, reg.name);
      length = AppendLiteral(block, length, capacity, "=");
      AppendHex(block, length, capacity, static_cast<std::uintptr_t>(gregs[reg.index]));
    }
    length = AppendLiteral(block, length, capacity, "\n");
  }
#endif
  void* frames[48];
  const int frame_count = backtrace(frames, 48);
  length = AppendLiteral(block, length, capacity, "backtrace (exe offsets for addr2line):\n");
  for (int i = 0; i < frame_count; ++i) {
    const auto address = reinterpret_cast<std::uintptr_t>(frames[i]);
    length = AppendLiteral(block, length, capacity, "  #");
    length = AppendUnsigned(block, length, capacity, static_cast<std::uint64_t>(i), 10);
    if (g_exe_base != 0 && address >= g_exe_base && address - g_exe_base < 0x40000000) {
      length = AppendLiteral(block, length, capacity, " exe+");
      AppendHex(block, length, capacity, address - g_exe_base);
    } else {
      length = AppendLiteral(block, length, capacity, " abs ");
      AppendHex(block, length, capacity, address);
    }
    length = AppendLiteral(block, length, capacity, "\n");
  }
  length = AppendLiteral(block, length, capacity, "executable mappings:\n");
  WriteAllSignalSafe(block, length);

#if defined(__x86_64__) && defined(REG_RIP)
  // Optional GH3/#17 diagnostic: identify a wild guest indirect-call target.
  if (g_dump_guest_ctx && signal_context) {
    const auto& gregs = static_cast<const ucontext_t*>(signal_context)->uc_mcontext.gregs;
    const std::uintptr_t membase = static_cast<std::uintptr_t>(gregs[REG_R14]);
    const std::uintptr_t ctx = static_cast<std::uintptr_t>(gregs[REG_RBX]);
    auto guest_u32 = [](std::uintptr_t address) -> std::uint32_t {
      return __builtin_bswap32(*reinterpret_cast<volatile std::uint32_t*>(address));
    };
    char gblock[640];
    std::size_t glen = 0;
    const std::size_t gcap = sizeof(gblock);
    glen = AppendLiteral(gblock, glen, gcap, "guest_dump: ctx=");
    AppendHex(gblock, glen, gcap, ctx);
    glen = AppendLiteral(gblock, glen, gcap, " base=");
    AppendHex(gblock, glen, gcap, membase);
    const std::uint32_t guest_this = guest_u32(ctx + 8);  // PPCContext::r3
    const std::uint32_t saved_this = guest_u32(ctx + 0x100);  // the code saved ctx.r3 here
    glen = AppendLiteral(gblock, glen, gcap, " r3=");
    AppendHex(gblock, glen, gcap, guest_this);
    glen = AppendLiteral(gblock, glen, gcap, " saved_r3=");
    AppendHex(gblock, glen, gcap, saved_this);
    glen = AppendLiteral(gblock, glen, gcap, " lr=");
    AppendHex(gblock, glen, gcap, guest_u32(ctx + 0x108));
    if (guest_this != 0) {
      const std::uint32_t raw_field4 = *reinterpret_cast<volatile std::uint32_t*>(
          static_cast<std::uintptr_t>(membase + guest_this + 4));
      const std::uint32_t field4 = __builtin_bswap32(raw_field4);
      glen = AppendLiteral(gblock, glen, gcap, " raw4=");
      AppendHex(gblock, glen, gcap, raw_field4);
      glen = AppendLiteral(gblock, glen, gcap, " field4=");
      AppendHex(gblock, glen, gcap, field4);
      glen = AppendLiteral(gblock, glen, gcap, " this_words:");
      for (int i = 0; i < 8; ++i) {
        glen = AppendLiteral(gblock, glen, gcap, " ");
        AppendHex(gblock, glen, gcap, guest_u32(membase + guest_this + i * 4));
      }
      glen = AppendLiteral(gblock, glen, gcap, " vtbl_words:");
      for (int i = 0; i < 16; ++i) {
        glen = AppendLiteral(gblock, glen, gcap, " ");
        AppendHex(gblock, glen, gcap, guest_u32(membase + field4 + i * 4));
      }
    }
    glen = AppendLiteral(gblock, glen, gcap, "\n");
    WriteAllSignalSafe(gblock, glen);
  }
#endif

  // Copy only the r-xp lines of /proc/self/maps (the guest heap mappings are not executable).
  const int maps_fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  if (maps_fd < 0) {
    return;
  }
  char chunk[4096];
  char line[512];
  std::size_t line_length = 0;
  ssize_t read_count;
  while ((read_count = read(maps_fd, chunk, sizeof(chunk))) > 0) {
    for (ssize_t i = 0; i < read_count; ++i) {
      if (line_length < sizeof(line) - 1) {
        line[line_length++] = chunk[i];
      }
      if (chunk[i] != '\n') {
        continue;
      }
      line[line_length] = '\0';
      bool executable = false;
      for (std::size_t j = 0; j + 4 < line_length; ++j) {
        if (line[j] == ' ' && line[j + 1] == 'r' && line[j + 2] == '-' && line[j + 3] == 'x' &&
            line[j + 4] == 'p') {
          executable = true;
          break;
        }
      }
      if (executable) {
        WriteAllSignalSafe("  ", 2);
        WriteAllSignalSafe(line, line_length);
      }
      line_length = 0;
    }
  }
  close(maps_fd);
}

void ReraiseWithDefaultDisposition(int signal_number) {
  struct sigaction action {};
  action.sa_handler = SIG_DFL;
  sigemptyset(&action.sa_mask);
  sigaction(signal_number, &action, nullptr);
  sigset_t unblocked;
  sigemptyset(&unblocked);
  sigaddset(&unblocked, signal_number);
  sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
  raise(signal_number);
  _exit(128 + signal_number);
}

void CrashSignalHandler(int signal_number, siginfo_t* signal_info, void* signal_context) {
  if (g_crash_in_progress) {
    _exit(128 + signal_number);
  }
  g_crash_in_progress = 1;

  CrashEvent event{};
  event.signal_number = signal_number;
  event.has_fault_address = signal_number != SIGABRT && signal_info && signal_info->si_code > 0;
  event.fault_address = event.has_fault_address
                            ? reinterpret_cast<std::uintptr_t>(signal_info->si_addr)
                            : 0;
  event.host_pc = HostProgramCounter(signal_context);
  event.thread_id = static_cast<pid_t>(syscall(SYS_gettid));
  SignalSafeThreadName(event.thread_name, event.thread_id);
  WriteCrashBlockSignalSafe(event);
  WriteCrashDetailsSignalSafe(signal_context);

  // The reporter thread flushes the normal spdlog sinks outside signal context.
  // A bounded wait prevents a deadlocked sink from trapping a fatal process.
  if (g_request_pipe[1] >= 0 && g_ack_pipe[0] >= 0) {
    const char request = 'F';
    if (write(g_request_pipe[1], &request, sizeof(request)) == sizeof(request)) {
      alarm(3);
      char acknowledgement = 0;
      ssize_t result;
      do {
        result = read(g_ack_pipe[0], &acknowledgement, sizeof(acknowledgement));
      } while (result < 0 && errno == EINTR);
      alarm(0);
    }
  }
  ReraiseWithDefaultDisposition(signal_number);
}

void CrashFlushWorker(int request_fd, int acknowledgement_fd) {
  char request = 0;
  while (read(request_fd, &request, sizeof(request)) == sizeof(request)) {
    if (request == 'F') {
      FlushAllLoggerCategories();
      const char acknowledgement = 'A';
      if (write(acknowledgement_fd, &acknowledgement, sizeof(acknowledgement)) !=
          sizeof(acknowledgement)) {
        return;
      }
    }
  }
}

void InstallPlatformCrashHandler(const std::string& log_path) {
  Dl_info self_info{};
  if (dladdr(reinterpret_cast<void*>(&CrashSignalHandler), &self_info) && self_info.dli_fbase) {
    g_exe_base = reinterpret_cast<std::uintptr_t>(self_info.dli_fbase);
  }
  g_dump_guest_ctx = std::getenv("WWE13_CRASH_GUEST_CTX") != nullptr;
  // Load and initialise the unwinder now; the first backtrace() call may allocate.
  void* prime_frames[2];
  backtrace(prime_frames, 2);
  g_crash_log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (g_crash_log_fd < 0) {
    REXLOG_WARN("[wwe13-crash] could not open configured log for direct crash notes");
  }
  if (pipe(g_request_pipe) != 0 || pipe(g_ack_pipe) != 0) {
    g_request_pipe[0] = g_request_pipe[1] = -1;
    g_ack_pipe[0] = g_ack_pipe[1] = -1;
    REXLOG_WARN("[wwe13-crash] could not create logger-flush pipes; crash note uses direct write only");
  } else {
    try {
      std::thread(CrashFlushWorker, g_request_pipe[0], g_ack_pipe[1]).detach();
    } catch (const std::system_error&) {
      REXLOG_WARN("[wwe13-crash] could not start logger-flush worker; crash note uses direct write only");
      close(g_request_pipe[0]);
      close(g_request_pipe[1]);
      close(g_ack_pipe[0]);
      close(g_ack_pipe[1]);
      g_request_pipe[0] = g_request_pipe[1] = -1;
      g_ack_pipe[0] = g_ack_pipe[1] = -1;
    }
  }

  struct sigaction action {};
  sigemptyset(&action.sa_mask);
  action.sa_sigaction = CrashSignalHandler;
  action.sa_flags = SA_SIGINFO;
  for (const int signal_number : kFatalSignals) {
    if (sigaction(signal_number, &action, nullptr) != 0) {
      REXLOG_WARN("[wwe13-crash] could not install handler for {}", SignalName(signal_number));
    }
  }
}

void StartTestCrashIfRequested() {
  const char* value = std::getenv(kTestCrashEnv);
  if (!value || std::strcmp(value, "1") != 0) {
    return;
  }
  REXLOG_WARN("[wwe13-test-crash] armed; deliberate access violation in 8 seconds");
  std::thread([] {
    pthread_setname_np(pthread_self(), "wwe13-test-crash");
    std::this_thread::sleep_for(kTestCrashDelay);
    volatile std::uintptr_t invalid_address = 1;
    *reinterpret_cast<volatile std::uint32_t*>(invalid_address) = 0x13;
  }).detach();
}

#endif

void LogHeader(const std::filesystem::path& game_folder,
               GameFileCheckResult game_file_result) {
  const bool has_executable = HasRegularFile(game_folder / "default.xex");
  const bool has_update = HasRegularFile(game_folder / "default.xexp");
  const char* update_version = game_file_result == GameFileCheckResult::kSupported
                                   ? "2.0.1.0 (file-validated)"
                                   : "unknown/not verified";
  const std::string cores_ram = CoreCountAndRam();

  REXLOG_INFO("========== WWE '13 BUG REPORT PROFILE BEGIN ==========");
  REXLOG_INFO("build: version={} git={} dirty={} date_utc={} sdk_git={} sdk_dirty={}", WWE13_VERSION,
              WWE13_BUILD_COMMIT, WWE13_BUILD_DIRTY, WWE13_BUILD_DATE, WWE13_SDK_COMMIT, WWE13_SDK_DIRTY);
  REXLOG_INFO("host: os={} cpu={} cores_ram={}", OsVersion(), CpuName(), cores_ram);
  REXLOG_INFO("power: {}", PowerState());
  REXLOG_INFO(
      "settings: resolution_scale={} fullscreen={} debug_ui={} vulkan_device={} vulkan_device_id={}",
      REXCVAR_GET(resolution_scale), REXCVAR_GET(fullscreen) ? "true" : "false",
      REXCVAR_GET(debug_ui) ? "true" : "false", REXCVAR_GET(vulkan_device),
      REXCVAR_GET(vulkan_device_id));
  REXLOG_INFO("settings_env: WWE13_KEEP_60={} WWE13_LOCK_30={} WWE13_F24_PIXEL_RATE={}",
              EnvValue("WWE13_KEEP_60"), EnvValue("WWE13_LOCK_30"),
              EnvValue("WWE13_F24_PIXEL_RATE"));
  REXLOG_INFO("game_files: default.xex={} default.xexp={} title_update={}",
              has_executable ? "present" : "missing", has_update ? "present" : "missing",
              update_version);
  REXLOG_INFO("Vulkan selected-device details follow after backend selection.");
  REXLOG_INFO("========== WWE '13 BUG REPORT PROFILE END ============");
}

}  // namespace

void InitializeReleaseDiagnostics(const std::filesystem::path& game_folder,
                                  GameFileCheckResult game_file_result) {
  InstallPlatformCrashHandler(LogFilePath());
  LogHeader(game_folder, game_file_result);
  StartTestCrashIfRequested();
}

void FlushReleaseLogs() { FlushAllLoggerCategories(); }

}  // namespace wwe13
