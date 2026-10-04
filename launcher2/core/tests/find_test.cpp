// find_test: "Find Automatically" opens a file only when its name, its size or its Xbox content path
// makes it a WWE '13 TU/DLC candidate, then verifies the STFS header before offering it. Synthetic tree,
// no game files needed. The number of header reads is counted by the core (FindGameFilesHeaderReadCount),
// not by tracing the process.
#include "../launcher_core.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace wwe13::launcher;

namespace {

constexpr uint32_t kTitleId = 0x545108B4;
constexpr uint32_t kTitleUpdateType = 0x000B0000;
constexpr uint32_t kDlcType = 0x00000002;
constexpr uintmax_t kTitleUpdateSize = 4329472;

void Check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void SetSearchRoots(const fs::path& root) {
  const std::string value = root.string();
#ifdef _WIN32
  const std::wstring wide(value.begin(), value.end());
  _wputenv_s(L"WWE13_LAUNCHER_TEST_SEARCH_ROOTS", wide.c_str());
#else
  Check(::setenv("WWE13_LAUNCHER_TEST_SEARCH_ROOTS", value.c_str(), 1) == 0,
        "could not set the test search root");
#endif
}

void ClearSearchRootsForTest() {
#ifdef _WIN32
  _wputenv_s(L"WWE13_LAUNCHER_TEST_SEARCH_ROOTS", L"");
#else
  ::unsetenv("WWE13_LAUNCHER_TEST_SEARCH_ROOTS");
#endif
}

void PutBigEndianU32(uint8_t* bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value >> 24);
  bytes[1] = static_cast<uint8_t>(value >> 16);
  bytes[2] = static_cast<uint8_t>(value >> 8);
  bytes[3] = static_cast<uint8_t>(value);
}

// Writes just the STFS header fields the inspector reads plus, optionally, pads the file to an exact
// total size (a sparse ext4/NTFS file; nothing fills the payload).
void WritePackage(const fs::path& path, const char magic[4], uint32_t content_type, uint32_t title_id,
                  uintmax_t total_size) {
  fs::create_directories(path.parent_path());
  {
    std::array<uint8_t, 0x364> header{};
    std::memcpy(header.data(), magic, 4);
    PutBigEndianU32(header.data() + 0x344, content_type);
    PutBigEndianU32(header.data() + 0x360, title_id);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Check(static_cast<bool>(output), "could not create " + path.string());
    output.write(reinterpret_cast<const char*>(header.data()),
                 static_cast<std::streamsize>(header.size()));
    Check(static_cast<bool>(output), "could not write " + path.string());
  }
  if (total_size > 0) {
    std::error_code error;
    fs::resize_file(path, total_size, error);
    Check(!error, "could not size " + path.string());
  }
}

void WriteFile(const fs::path& path, const std::string& contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
  Check(static_cast<bool>(output), "could not write " + path.string());
}

fs::path Normalize(const fs::path& path) {
  std::error_code error;
  fs::path absolute = fs::absolute(path, error);
  if (error) absolute = path;
  return absolute.lexically_normal();
}

bool HasPackage(const std::vector<fs::path>& packages, const fs::path& path) {
  const fs::path wanted = Normalize(path);
  return std::find(packages.begin(), packages.end(), wanted) != packages.end();
}

std::vector<fs::path> FindPackagesNow(const Paths& paths) {
  std::vector<fs::path> packages;
  CancelFlag cancel{false};
  FindGameFiles(paths, ProgressFn{}, cancel, &packages);
  return packages;
}

// (i) renamed-extension TU stand-in matching the exact size, (ii) known name with the wrong title ID,
// (iv) a Xenia 545108B4/000B0000 layout, plus a game folder that must still be detected.
void TestEligibilityAndVerification(const fs::path& root) {
  SetSearchRoots(root);

  const fs::path by_size = root / "somewhere" / "my_update.bin";
  WritePackage(by_size, "CON ", kTitleUpdateType, kTitleId, kTitleUpdateSize);

  const fs::path dlc_renamed = root / "downloads" / "WWE '13 - DLC Pack 2 (World) (Addon).bin";
  WritePackage(dlc_renamed, "LIVE", kDlcType, kTitleId, 900);

  // A known DLC name wins even over the media skip list: a player who renamed the extension to a media
  // one must still be found (rule (a)).
  const fs::path dlc_media_extension = root / "downloads" / "WWE '13 - Pack 1 (World) (DLC).mkv";
  WritePackage(dlc_media_extension, "CON ", kDlcType, kTitleId, 900);

  // Known TU name, valid magic and content type, but another game's title ID: opened (name match) and
  // rejected by the header.
  const fs::path wrong_title = root / "downloads" / "TU_1A5225K_0000008000000.0000000000082";
  WritePackage(wrong_title, "CON ", kTitleUpdateType, 0x545108B5, 900);

  // Nothing about this file says "package": it must never be opened.
  const fs::path ordinary = root / "downloads" / "notes.bin";
  WriteFile(ordinary, "hello");

  const fs::path xenia_tu = root / "545108B4" / "000B0000" / "unknown-tu.dat";
  WritePackage(xenia_tu, "PIRS", kTitleUpdateType, kTitleId, 900);
  const fs::path xenia_dlc =
      root / "545108B4" / "00000002" / "9FAF6BD4EADAD29A4AA3517843077FC877DDFA0WWE";
  WritePackage(xenia_dlc, "CON ", kDlcType, kTitleId, 900);

  const fs::path game = root / "WWE 13";
  WriteFile(game / "default.xex", "not really an xex");

  Paths paths;
  paths.exe_dir = root / "launcher";
  const std::vector<fs::path> packages = FindPackagesNow(paths);

  Check(HasPackage(packages, by_size), "renamed-extension TU stand-in (exact size) was not found");
  Check(HasPackage(packages, dlc_renamed), "DLC with a renamed extension was not found");
  Check(HasPackage(packages, dlc_media_extension),
        "DLC renamed to a media extension was skipped by the media pre-filter");
  Check(HasPackage(packages, xenia_tu), "file in a 545108B4/000B0000 layout was not found");
  Check(HasPackage(packages, xenia_dlc), "file in a 545108B4/00000002 layout was not found");
  Check(!HasPackage(packages, wrong_title), "wrong title ID was accepted after header verification");
  Check(!HasPackage(packages, ordinary), "an ordinary file was offered as a package");
  Check(packages.size() == 5, "unexpected number of packages found: " + std::to_string(packages.size()));
  // Six candidate files were opened (five valid + the wrong-title TU); the ordinary file was not.
  Check(FindGameFilesHeaderReadCount() == 6,
        "header read count was " + std::to_string(FindGameFilesHeaderReadCount()) + ", expected 6");

  // The search root override stays set: clearing it would walk the real drives. A second call proves the
  // game-folder (default.xex) detection survived the rewrite.
  std::vector<fs::path> ignored;
  CancelFlag cancel{false};
  const std::vector<FoundGame> found = FindGameFiles(paths, ProgressFn{}, cancel, &ignored);
  Check(found.size() == 1, "game folder detection changed: expected one default.xex folder");
  Check(found.front().folder == Normalize(game), "the folder with default.xex was not the candidate");
  ClearSearchRootsForTest();
}

// (iii) 2,000 media files and 2,000 random extensionless files must not be opened at all.
void TestNothingUnrelatedIsOpened(const fs::path& root) {
  SetSearchRoots(root);
  const char* media_extensions[] = {".mkv", ".mp4", ".mp3", ".jpg", ".txt", ".zip"};
  for (int index = 0; index < 2000; ++index) {
    const std::string name = "clip_" + std::to_string(index) + media_extensions[index % 6];
    WriteFile(root / name, std::string(static_cast<size_t>(index % 61) + 1, 'x'));
  }
  for (int index = 0; index < 2000; ++index) {
    const std::string name = "data_" + std::to_string(index);  // extensionless
    WriteFile(root / name, std::string(static_cast<size_t>(index % 37) + 1, 'y'));
  }

  Paths paths;
  paths.exe_dir = root / "launcher";
  const std::vector<fs::path> packages = FindPackagesNow(paths);
  Check(packages.empty(), "an unrelated file was offered as a package");
  Check(FindGameFilesHeaderReadCount() == 0,
        "unrelated files were opened: " + std::to_string(FindGameFilesHeaderReadCount()));
  ClearSearchRootsForTest();
}

// (v) cancel mid-walk returns promptly instead of finishing the whole scan.
void TestCancelMidWalk(const fs::path& root) {
  SetSearchRoots(root);
  for (int index = 0; index < 500; ++index) {
    fs::create_directories(root / ("dir_" + std::to_string(index)));
  }

  Paths paths;
  paths.exe_dir = root / "launcher";
  CancelFlag cancel{false};
  uint64_t visits = 0;
  const auto start = std::chrono::steady_clock::now();
  const std::vector<FoundGame> found = FindGameFiles(
      paths,
      [&](uint64_t, uint64_t, const std::string&) {
        if (++visits == 3) cancel.store(true);
      },
      cancel, nullptr);
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  Check(cancel.load(), "the cancel flag was never observed");
  Check(visits >= 3 && visits < 100,
        "cancel did not stop the walk promptly: " + std::to_string(visits) + " folders visited");
  Check(seconds < 5.0, "cancelled walk took too long: " + std::to_string(seconds) + "s");
  Check(found.empty(), "an empty synthetic tree produced game candidates");
  std::cout << "cancel: " << visits << " folders visited in " << seconds * 1000.0 << " ms\n";
  ClearSearchRootsForTest();
}

}  // namespace

int main() {
  try {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path base = fs::temp_directory_path() / ("wwe13-find-test-" + std::to_string(stamp));
    const fs::path eligibility = base / "eligibility";
    const fs::path noise = base / "noise";
    const fs::path cancel = base / "cancel";
    fs::create_directories(base);

    TestEligibilityAndVerification(eligibility);
    TestNothingUnrelatedIsOpened(noise);
    TestCancelMidWalk(cancel);

    std::error_code error;
    fs::remove_all(base, error);
    Check(!error, "temporary find fixture folder could not be removed");
    std::cout << "find_test: PASS (eligibility, header verification, unopened files, Xenia layout, "
                 "cancel)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "find_test: FAIL: " << error.what() << '\n';
    return 1;
  }
}
