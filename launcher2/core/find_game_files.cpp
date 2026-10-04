#include "launcher_core.h"

#include "internal/util.h"

#include <algorithm>
#include <optional>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace wwe13::launcher {
namespace {

bool HasExecutable(const fs::path& folder) {
  std::error_code error;
  return fs::is_regular_file(folder / "default.xex", error) && !error;
}

bool SkipDirectory(const fs::path& directory) {
  const std::string name = internal::LowerAscii(internal::PathToUtf8(directory.filename()));
  static const std::set<std::string> skipped = {
      ".git", ".hg", ".svn", ".cache", "node_modules", "proc", "sys", "dev", "run",
      "windows", "program files", "program files (x86)", "programdata", "system volume information",
      "$recycle.bin", "lost+found", "recovery", "appdata"};
  return skipped.contains(name);
}

// Files that can never be an Xbox 360 title-update / DLC package (STFS packages have no extension or an
// unrelated one). Skipping them without opening keeps "Find Automatically" fast on drives full of media.
bool SkipPackageCandidate(const fs::path& file) {
  const std::string extension = internal::LowerAscii(internal::PathToUtf8(file.extension()));
  static const std::set<std::string> skipped = {
      ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".m4v", ".ts", ".m2ts", ".webm", ".flv", ".mpg", ".mpeg", ".vob",
      ".mp3", ".flac", ".wav", ".ogg", ".m4a", ".aac", ".wma", ".opus",
      ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".heic", ".tif", ".tiff",
      ".srt", ".sub", ".idx", ".ass", ".nfo", ".txt", ".pdf", ".doc", ".docx", ".xls", ".xlsx",
      ".exe", ".dll", ".msi", ".iso", ".xex", ".xexp", ".zip", ".rar", ".7z"};
  return skipped.contains(extension);
}

// The exact names the owner's WWE '13 title update and DLC packs are known by on disk, plus the names an
// Xbox / Xenia content folder uses. A package is only opened on the strength of one of these names, one of
// the known sizes, or a 545108B4 / 000B0000 / 00000002 path component; the header is then verified.
const std::set<std::string>& KnownPackageNames() {
  static const std::set<std::string> names = {
      // Title update (content type 000B0000).
      "tu_1a5225k_0000008000000.0000000000082",
      // DLC packs as commonly downloaded (content type 00000002).
      "wwe '13 - dlc pack 2 (world) (addon)",
      "wwe '13 - dlc pack 3 (world) (addon)",
      "wwe '13 - pack 1 (world) (dlc)",
      // DLC as stored on an Xbox / Xenia content folder.
      "9faf6bd4eadad29a4aa3517843077fc877ddfa0wwe",
      "ae201cfdeffb0c988b4a1dc89edf2b4f7491fb5354",
      "b64239e5b81fc5efe8cef7e695f77223a9d39a0f54"};
  return names;
}

// DLC names may survive a player renaming only the extension; the file stem still identifies them. The
// title update is matched by its full name only (its extension is part of the name).
const std::set<std::string>& KnownDlcNameStems() {
  static const std::set<std::string> stems = {
      "wwe '13 - dlc pack 2 (world) (addon)",
      "wwe '13 - dlc pack 3 (world) (addon)",
      "wwe '13 - pack 1 (world) (dlc)",
      "9faf6bd4eadad29a4aa3517843077fc877ddfa0wwe",
      "ae201cfdeffb0c988b4a1dc89edf2b4f7491fb5354",
      "b64239e5b81fc5efe8cef7e695f77223a9d39a0f54"};
  return stems;
}

bool IsKnownPackageSize(uintmax_t size) {
  static const std::set<uintmax_t> sizes = {
      4329472u,      // title update
      332611584u,    // DLC Pack 2
      325292032u,    // DLC Pack 3
      57344u};       // Pack 1
  return sizes.contains(size);
}

bool IsKnownPackageName(const fs::path& file) {
  const std::string name = internal::LowerAscii(internal::PathToUtf8(file.filename()));
  if (KnownPackageNames().contains(name)) return true;
  const std::string stem = internal::LowerAscii(internal::PathToUtf8(file.stem()));
  if (stem.empty() || stem == name) return false;  // no extension to rename
  return KnownDlcNameStems().contains(stem);
}

// Xbox / Xenia content layout: anything under a 545108B4 folder, or under its 000B0000 (title update) /
// 00000002 (DLC) subfolders, is a package candidate even when its name is unusual.
bool IsKnownContentPath(const fs::path& file) {
  for (const fs::path& component : file.parent_path()) {
    const std::string part = internal::LowerAscii(internal::PathToUtf8(component));
    if (part == "545108b4" || part == "000b0000" || part == "00000002") return true;
  }
  return false;
}

// Rule for "Find Automatically": a file is opened (header read) only when its name, its size or its folder
// says it could be a WWE '13 package. A known name wins even over the media skip list (a DLC whose
// extension was renamed), but the skip list is otherwise a fast pre-filter, so a video/music/picture file
// that merely happens to have a known size or an Xbox content path is never opened.
bool PackageCandidateWorthOpening(const fs::path& file, uintmax_t size) {
  if (IsKnownPackageName(file)) return true;
  if (SkipPackageCandidate(file)) return false;
  if (IsKnownPackageSize(size)) return true;
  return IsKnownContentPath(file);
}

// Incremented once per candidate header read. Reset at the start of each FindGameFiles call.
std::atomic<uint64_t> g_header_reads{0};

void AddUnique(std::vector<fs::path>& paths, const fs::path& path) {
  if (path.empty()) return;
  std::error_code error;
  fs::path normalized = fs::absolute(path, error);
  if (error) normalized = path;
  normalized = normalized.lexically_normal();
  if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) paths.push_back(std::move(normalized));
}

bool HasTestSearchRootOverride() {
#ifdef _WIN32
  const wchar_t* roots = _wgetenv(L"WWE13_LAUNCHER_TEST_SEARCH_ROOTS");
#else
  const char* roots = std::getenv("WWE13_LAUNCHER_TEST_SEARCH_ROOTS");
#endif
  return roots && *roots;
}

fs::path HomeDirectory() {
#ifdef _WIN32
  if (const wchar_t* profile = _wgetenv(L"USERPROFILE")) return fs::path(profile);
  const wchar_t* drive = _wgetenv(L"HOMEDRIVE");
  const wchar_t* home = _wgetenv(L"HOMEPATH");
  if (drive && home) return fs::path(std::wstring(drive) + home);
#else
  if (const char* home = std::getenv("HOME")) return internal::PathFromUtf8(home);
#endif
  std::error_code error;
  return fs::current_path(error);
}

std::string UnescapeMount(std::string value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 3 < value.size() && value[i + 1] == '0') {
      const std::string code = value.substr(i + 1, 3);
      if (code == "040") decoded.push_back(' ');
      else if (code == "011") decoded.push_back('\t');
      else if (code == "134") decoded.push_back('\\');
      else decoded.push_back(value[i]);
      if (code == "040" || code == "011" || code == "134") {
        i += 3;
        continue;
      }
    }
    decoded.push_back(value[i]);
  }
  return decoded;
}

std::vector<fs::path> SearchRoots(const fs::path& exe_dir, const fs::path& home) {
  std::vector<fs::path> roots;
#ifdef _WIN32
  if (const wchar_t* test_roots = _wgetenv(L"WWE13_LAUNCHER_TEST_SEARCH_ROOTS");
      test_roots && *test_roots) {
    std::wstring_view remaining(test_roots);
    constexpr wchar_t separator = L';';
    while (!remaining.empty()) {
      const size_t end = remaining.find(separator);
      const std::wstring_view item = remaining.substr(0, end);
      if (!item.empty()) AddUnique(roots, fs::path(item));
      if (end == std::wstring_view::npos) break;
      remaining.remove_prefix(end + 1);
    }
    return roots;
  }
#else
  if (const char* test_roots = std::getenv("WWE13_LAUNCHER_TEST_SEARCH_ROOTS");
      test_roots && *test_roots) {
    constexpr char separator = ':';
    std::string_view remaining(test_roots);
    while (!remaining.empty()) {
      const size_t end = remaining.find(separator);
      const std::string_view item = remaining.substr(0, end);
      if (!item.empty()) AddUnique(roots, internal::PathFromUtf8(std::string(item)));
      if (end == std::string_view::npos) break;
      remaining.remove_prefix(end + 1);
    }
    return roots;
  }
#endif
  AddUnique(roots, exe_dir);
  AddUnique(roots, home);
#ifdef _WIN32
  const DWORD mask = GetLogicalDrives();
  for (unsigned drive = 0; drive < 26; ++drive) {
    if (mask & (1u << drive)) {
      std::string root;
      root.push_back(static_cast<char>('A' + drive));
      root += ":\\";
      AddUnique(roots, internal::PathFromUtf8(root));
    }
  }
#else
  std::ifstream mounts("/proc/self/mounts");
  std::string source, target, type, options;
  while (mounts >> source >> target >> type >> options) {
    if (type == "proc" || type == "sysfs" || type == "devtmpfs" || type == "devpts" ||
        type == "cgroup" || type == "cgroup2" || type == "securityfs" || type == "pstore" ||
        type == "debugfs" || type == "tracefs" || type == "mqueue" || type == "configfs" ||
        type == "fusectl") continue;
    AddUnique(roots, internal::PathFromUtf8(UnescapeMount(target)));
  }
  AddUnique(roots, "/");
  AddUnique(roots, "/mnt");
  AddUnique(roots, "/media");
#endif
  return roots;
}

}  // namespace

uint64_t FindGameFilesHeaderReadCount() {
  return g_header_reads.load(std::memory_order_relaxed);
}

std::vector<FoundGame> FindGameFiles(const Paths& paths, const ProgressFn& progress,
                                     const CancelFlag& cancel, std::vector<fs::path>* found_packages) {
  try {
  g_header_reads.store(0, std::memory_order_relaxed);
  struct Candidate {
    fs::path folder;
    size_t rank = 0;
  };
  std::vector<Candidate> candidates;
  std::vector<fs::path> visited;
  std::set<fs::path> scanned_package_folders;
  uint64_t visited_count = 0;
  std::vector<fs::path> package_hits;
  auto add_package = [&](const fs::path& path) {
    std::error_code error;
    fs::path normalized = fs::absolute(path, error);
    if (error) normalized = path;
    normalized = normalized.lexically_normal();
    if (std::find(package_hits.begin(), package_hits.end(), normalized) == package_hits.end()) {
      package_hits.push_back(normalized);
    }
  };
  // Title-update / DLC package files in one folder (top level). A file is only opened when its name, its
  // size or its Xbox content path makes it a candidate; the STFS header then has to verify as a WWE '13
  // title update / DLC package. Ordinary files are never opened.
  auto scan_packages = [&](const fs::path& folder) {
    if (!found_packages || cancel.load(std::memory_order_relaxed)) return;
    std::error_code normalize_error;
    fs::path normalized = fs::absolute(folder, normalize_error);
    if (normalize_error) normalized = folder;
    normalized = normalized.lexically_normal();
    if (!scanned_package_folders.insert(normalized).second) return;
    std::error_code error;
    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
      if (cancel.load(std::memory_order_relaxed)) return;
      std::error_code entry_error;
      if (!it->is_regular_file(entry_error) || entry_error) continue;
      const uintmax_t size = it->file_size(entry_error);
      if (entry_error) continue;
      if (!PackageCandidateWorthOpening(it->path(), size)) continue;
      g_header_reads.fetch_add(1, std::memory_order_relaxed);
      const std::optional<PackageInfo> package = InspectPackageHeader(it->path());
      if (package && package->title_id == 0x545108B4 && package->kind != PackageKind::kUnknown) {
        add_package(it->path());
      }
    }
  };
  auto inspect = [&](const fs::path& folder) {
    if (folder.empty() || cancel.load(std::memory_order_relaxed)) return;
    std::error_code error;
    if (!fs::is_directory(folder, error) || error || !HasExecutable(folder)) return;
    fs::path normalized = fs::absolute(folder, error);
    if (error) normalized = folder;
    normalized = normalized.lexically_normal();
    if (std::find(visited.begin(), visited.end(), normalized) != visited.end()) return;
    visited.push_back(normalized);
    candidates.push_back({normalized, candidates.size()});
    if (progress) progress(visited_count, 0, internal::PathToUtf8(normalized));
  };

  fs::path game_folder_parent;
  if (!HasTestSearchRootOverride()) {
    inspect(paths.default_game_folder);
    const Settings saved = LoadSettings(paths);
    inspect(saved.game_folder);
    game_folder_parent = saved.game_folder.parent_path();
    const fs::path home = HomeDirectory();
    const fs::path title_content = fs::path("545108B4") / "00000000";
    for (const fs::path& content_root : {
             home / "Documents" / "Xenia" / "content",
             home / "Documents" / "Xenia Canary" / "content",
             home / "Documents" / "xenia-canary" / "content",
             paths.exe_dir / "content",
             paths.exe_dir / "xenia-canary" / "content",
             paths.exe_dir / "Xenia Canary" / "content"}) {
      inspect(content_root / title_content);
      inspect(content_root / "545108B4");
      for (const char* kind : {"000B0000", "00000002"}) {
        const fs::path packages = content_root / "545108B4" / kind;
        std::error_code error;
        if (!fs::is_directory(packages, error)) continue;
        scan_packages(packages);
        for (fs::directory_iterator it(packages, fs::directory_options::skip_permission_denied, error), end;
             !error && it != end; it.increment(error)) {
          std::error_code entry_error;
          if (!it->is_directory(entry_error) || entry_error) continue;
          const std::optional<PackageInfo> package = InspectPackage(it->path());
          if (package && package->title_id == 0x545108B4 && package->kind != PackageKind::kUnknown) {
            add_package(it->path());
          }
        }
      }
    }
  }

  const fs::path home = HomeDirectory();
  const std::vector<fs::path> search_roots = SearchRoots(paths.exe_dir, home);

  auto walk_from_root = [&](const fs::path& root, unsigned max_depth) {
    std::error_code error;
    if (!fs::is_directory(root, error) || error) return;
    std::vector<std::pair<fs::path, unsigned>> pending{{root, 0}};
    while (!pending.empty() && !cancel.load(std::memory_order_relaxed)) {
      auto [folder, depth] = std::move(pending.back());
      pending.pop_back();
      inspect(folder);
      scan_packages(folder);
      ++visited_count;
      if (progress) progress(visited_count, 0, internal::PathToUtf8(folder));
      if (depth >= max_depth || cancel.load(std::memory_order_relaxed)) continue;
      fs::directory_iterator iterator(folder, fs::directory_options::skip_permission_denied, error);
      if (error) continue;
      for (const auto& entry : iterator) {
        if (cancel.load(std::memory_order_relaxed)) break;
        error.clear();
        const bool symlink = entry.is_symlink(error);
        if (error || !entry.is_directory(error) || error) continue;
        if (symlink) {
          // Xenia game folders are often linked into a user's library. Inspect
          // the link target as a candidate, but never recurse through it.
          inspect(entry.path());
          continue;
        }
        if (SkipDirectory(entry.path())) continue;
        pending.emplace_back(entry.path(), depth + 1);
      }
    }
  };

  // Search order: where players keep TU/DLC files and game folders first (the launcher folder and its
  // parent, Downloads/Desktop/Documents, the game folder's parent, drive roots one level), then the
  // deeper walk. A folder is scanned for packages at most once, so the priority pass cannot open the
  // same file twice.
  std::vector<fs::path> priority_roots;
  if (!HasTestSearchRootOverride()) {
    AddUnique(priority_roots, paths.exe_dir);
    AddUnique(priority_roots, paths.exe_dir.parent_path());
    AddUnique(priority_roots, home / "Downloads");
    AddUnique(priority_roots, home / "Desktop");
    AddUnique(priority_roots, home / "Documents");
    AddUnique(priority_roots, game_folder_parent);
    AddUnique(priority_roots, paths.default_game_folder.parent_path());
  }
  for (const fs::path& root : search_roots) AddUnique(priority_roots, root);
  for (const fs::path& root : priority_roots) {
    if (cancel.load(std::memory_order_relaxed)) break;
    walk_from_root(root, 1);
  }
  for (const fs::path& root : search_roots) {
    if (cancel.load(std::memory_order_relaxed)) break;
    walk_from_root(root, 4);
  }

  if (found_packages) {
    std::sort(package_hits.begin(), package_hits.end());
    *found_packages = std::move(package_hits);
  }
  std::vector<FoundGame> found;
  found.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    found.push_back({candidate.folder, CheckGameFolder(candidate.folder, paths)});
  }
  std::stable_sort(found.begin(), found.end(), [&](const FoundGame& left, const FoundGame& right) {
    if (left.status.ready() != right.status.ready()) return left.status.ready();
    const auto rank_of = [&](const fs::path& path) {
      auto it = std::find_if(candidates.begin(), candidates.end(), [&](const Candidate& c) {
        return c.folder == path;
      });
      return it == candidates.end() ? candidates.size() : it->rank;
    };
    return rank_of(left.folder) < rank_of(right.folder);
  });
  return found;
  } catch (...) {
    return {};
  }
}

}  // namespace wwe13::launcher
