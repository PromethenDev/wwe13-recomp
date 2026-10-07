// See single_instance.h. GitHub #6: stop a second game process from starting
// against the same user-data folder.

#include "single_instance.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace wwe13 {
namespace {

// FNV-1a (64-bit): stable across builds and platforms, so the launcher and the
// game compute the same key for the same folder.
uint64_t HashString(const std::string& text) {
  uint64_t hash = 1469598103934665603ull;
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string InstanceKey(const std::filesystem::path& user_data_root) {
  std::error_code ec;
  std::filesystem::path canonical = std::filesystem::weakly_canonical(user_data_root, ec);
  std::string text = (ec ? user_data_root : canonical).generic_string();
#ifdef _WIN32
  // Windows paths are case-insensitive; normalise so C:\X and c:\x collide.
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  }
#endif
  uint64_t hash = HashString(text);
  const char* digits = "0123456789abcdef";
  std::string key;
  key.reserve(16);
  for (int i = 15; i >= 0; --i) key.push_back(digits[(hash >> (i * 4)) & 0xF]);
  return key;
}

#ifdef _WIN32
std::wstring ToWide(const std::string& text) {
  if (text.empty()) return {};
  int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring wide(size_t(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), length);
  return wide;
}

std::wstring MutexName(const std::string& key) {
  // "Local\" keeps the object in the player's session; two Windows users can
  // each run their own copy.
  std::wstring name = L"Local\\WWE13Recomp-";
  name += ToWide(key);
  return name;
}

HANDLE g_mutex = nullptr;
#else
int g_lock_fd = -1;

std::string LockFilePath(const std::string& key) {
  const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
  std::string dir = (runtime_dir && runtime_dir[0]) ? runtime_dir : "/tmp";
  while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
  return dir + "/wwe13-recomp-" + key + ".lock";
}
#endif

}  // namespace

bool AcquireUserDataInstanceLock(const std::filesystem::path& user_data_root) {
  if (user_data_root.empty()) return true;  // No user-data folder: no scope to guard.
  const std::string key = InstanceKey(user_data_root);
#ifdef _WIN32
  HANDLE mutex = CreateMutexW(nullptr, FALSE, MutexName(key).c_str());
  if (mutex == nullptr) return true;  // Fail open: never block play on a kernel object error.
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return false;
  }
  g_mutex = mutex;  // Held for the rest of the process; the OS releases it on exit.
  return true;
#else
  const std::string path = LockFilePath(key);
  int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) return true;
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    ::close(fd);
    return false;
  }
  g_lock_fd = fd;
  return true;
#endif
}

bool AnotherUserDataInstanceRunning(const std::filesystem::path& user_data_root) {
  if (user_data_root.empty()) return false;
  const std::string key = InstanceKey(user_data_root);
#ifdef _WIN32
  HANDLE mutex = CreateMutexW(nullptr, FALSE, MutexName(key).c_str());
  if (mutex == nullptr) return false;
  const bool existing = GetLastError() == ERROR_ALREADY_EXISTS;
  CloseHandle(mutex);
  return existing;
#else
  const std::string path = LockFilePath(key);
  int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) return false;
  const bool running = flock(fd, LOCK_EX | LOCK_NB) != 0;
  if (!running) flock(fd, LOCK_UN);
  ::close(fd);
  return running;
#endif
}

}  // namespace wwe13
