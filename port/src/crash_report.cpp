// WWE '13 PC - crash and freeze reports (from WWE '13 PC)
//
// Crash: an unhandled host exception writes logs\crash_<time>.txt (exception code, faulting
// module+offset, every thread's current module+offset) and a small minidump.
// Freeze: a watchdog thread writes logs\freeze_<time>.txt when the game has not presented a
// frame for 20 s while the window is not minimised, then keeps sampling every 5 s for 30 s so
// the report shows where each thread is stuck. Offsets are symbolized offline with the PDB
// kept in dist\symbols-v<version>.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>

#pragma comment(lib, "dbghelp.lib")

std::atomic<uint64_t> g_wwe13_present_count{0};  // counted by the Swap hook once it exists

namespace {

std::filesystem::path LogsDir() {
  wchar_t exe[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  auto dir = std::filesystem::path(exe).parent_path() / L"logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::wstring Stamp() {
  wchar_t buf[32];
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  wcsftime(buf, 32, L"%Y-%m-%d_%H-%M-%S", &tm);
  return buf;
}

// "module+0xoffset" for an address in this process.
std::string Where(uint64_t addr) {
  HMODULE mod = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(addr), &mod) && mod) {
    char name[MAX_PATH]{};
    GetModuleFileNameA(mod, name, MAX_PATH);
    const char* base = strrchr(name, '\\');
    char out[MAX_PATH + 32];
    std::snprintf(out, sizeof(out), "%s+0x%llX", base ? base + 1 : name,
                  static_cast<unsigned long long>(addr - reinterpret_cast<uint64_t>(mod)));
    return out;
  }
  char out[32];
  std::snprintf(out, sizeof(out), "0x%llX", static_cast<unsigned long long>(addr));
  return out;
}

// Writes each thread's RIP (and the return addresses found in its first stack words).
void DumpThreads(FILE* f, DWORD skip_tid) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 te{sizeof(te)};
  const DWORD pid = GetCurrentProcessId();
  for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
    if (te.th32OwnerProcessID != pid || te.th32ThreadID == skip_tid) continue;
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                           te.th32ThreadID);
    if (!th) continue;
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (SuspendThread(th) != DWORD(-1)) {
      if (GetThreadContext(th, &ctx)) {
        PWSTR desc = nullptr;
        char tname[128] = "";
        if (SUCCEEDED(GetThreadDescription(th, &desc)) && desc) {
          WideCharToMultiByte(CP_UTF8, 0, desc, -1, tname, sizeof(tname), nullptr, nullptr);
          LocalFree(desc);
        }
        FILETIME c, e, k, u;
        GetThreadTimes(th, &c, &e, &k, &u);
        const double cpu_s = ((uint64_t(u.dwHighDateTime) << 32 | u.dwLowDateTime) +
                              (uint64_t(k.dwHighDateTime) << 32 | k.dwLowDateTime)) / 1e7;
        std::fprintf(f, "thread %lu \"%s\" cpu %.1fs  rip %s\n", te.th32ThreadID, tname, cpu_s,
                     Where(ctx.Rip).c_str());
        // Likely return addresses near the top of the stack (heuristic, code addresses only).
        int shown = 0;
        for (int i = 0; i < 64 && shown < 8; ++i) {
          uint64_t v = 0;
          SIZE_T got = 0;
          if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(ctx.Rsp + i * 8), &v, 8, &got)) break;
          HMODULE m = nullptr;
          if (v > 0x10000 &&
              GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCWSTR>(v), &m) && m) {
            std::fprintf(f, "    ret? %s\n", Where(v).c_str());
            ++shown;
          }
        }
      }
      ResumeThread(th);
    }
    CloseHandle(th);
  }
  CloseHandle(snap);
}

void WriteMiniDump(const std::filesystem::path& path, EXCEPTION_POINTERS* ep) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
  MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                    static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                    ep ? &mei : nullptr, nullptr, nullptr);
  CloseHandle(file);
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS* ep) {
  static std::atomic<bool> once{false};
  if (once.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
  const auto stamp = Stamp();
  const auto dir = LogsDir();
  if (FILE* f = _wfopen((dir / (L"crash_" + stamp + L".txt")).c_str(), L"w")) {
    std::fprintf(f, "WWE '13 PC crash report\n");
    std::fprintf(f, "exception 0x%08lX at %s\n", ep->ExceptionRecord->ExceptionCode,
                 Where(reinterpret_cast<uint64_t>(ep->ExceptionRecord->ExceptionAddress)).c_str());
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
      std::fprintf(f, "access %s 0x%llX\n", ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                   static_cast<unsigned long long>(ep->ExceptionRecord->ExceptionInformation[1]));
    // The faulting thread's own stack, deeper than the others': a crash inside a GPU driver
    // (issues #6, #8) only says which of our calls led there a few driver frames further up.
    std::fprintf(f, "\nfaulting thread %lu  rip %s\n", GetCurrentThreadId(),
                 Where(ep->ContextRecord->Rip).c_str());
    int shown = 0;
    for (int i = 0; i < 2048 && shown < 40; ++i) {
      uint64_t v = 0;
      SIZE_T got = 0;
      if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(ep->ContextRecord->Rsp + i * 8),
                             &v, 8, &got))
        break;
      HMODULE m = nullptr;
      if (v > 0x10000 &&
          GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(v), &m) && m) {
        std::fprintf(f, "    ret? %s\n", Where(v).c_str());
        ++shown;
      }
    }
    std::fprintf(f, "\nthreads:\n");
    DumpThreads(f, GetCurrentThreadId());
    std::fclose(f);
  }
  WriteMiniDump(dir / (L"crash_" + stamp + L".dmp"), ep);
  return EXCEPTION_CONTINUE_SEARCH;
}

void Watchdog() {
  auto last_change = std::chrono::steady_clock::now();
  uint64_t last_count = 0;
  bool reported = false;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const uint64_t count = g_wwe13_present_count.load();
    if (count != last_count || count == 0) {  // frames are flowing (or the game is still booting)
      last_count = count;
      last_change = std::chrono::steady_clock::now();
      reported = false;
      continue;
    }
    const auto stalled = std::chrono::steady_clock::now() - last_change;
    if (reported || stalled < std::chrono::seconds(20)) continue;
    // A minimised window legitimately stops presenting.
    bool minimised = false;
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
      DWORD pid = 0;
      GetWindowThreadProcessId(h, &pid);
      if (pid == GetCurrentProcessId() && IsWindowVisible(h) && IsIconic(h)) *reinterpret_cast<bool*>(p) = true;
      return TRUE;
    }, reinterpret_cast<LPARAM>(&minimised));
    if (minimised) continue;
    reported = true;
    const auto path = LogsDir() / (L"freeze_" + Stamp() + L".txt");
    if (FILE* f = _wfopen(path.c_str(), L"w")) {
      std::fprintf(f, "WWE '13 PC freeze report: no frame presented for %lld s\n",
                   static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(stalled).count()));
      for (int sample = 0; sample < 6; ++sample) {
        std::fprintf(f, "\n=== sample %d ===\n", sample + 1);
        DumpThreads(f, GetCurrentThreadId());
        std::fflush(f);
        if (g_wwe13_present_count.load() != last_count) {
          std::fprintf(f, "\n(the game recovered)\n");
          break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
      std::fclose(f);
    }
  }
}

}  // namespace

// Called from Wwe13App::OnPostSetup (after the SDK and SDL set up their own handlers).
void InstallWwe13CrashReporter() {
  static std::atomic<bool> started{false};
  SetUnhandledExceptionFilter(OnCrash);
  // Freeze watchdog: started once g_wwe13_present_count is counted (Swap hook, step 2).
  (void)started;
}
