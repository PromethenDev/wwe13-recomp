#include "../launcher_core.h"

#include "../internal/util.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace wwe13::launcher;

namespace {

void Check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void WriteFile(const fs::path& path, const std::string& contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
  Check(static_cast<bool>(output), "could not write " + path.string());
}

std::string ReadTextFile(const fs::path& path) {
  std::vector<uint8_t> bytes;
  Check(internal::ReadBinaryFile(path, bytes), "could not read " + path.string());
  return std::string(bytes.begin(), bytes.end());
}

void TestVideos(const fs::path& root) {
  // A game folder with a titantron directory (numeric stems, .bik2).
  const fs::path game = root / "WWE 13";
  const fs::path titantron = game / "movies" / "titantron";
  WriteFile(titantron / "050.bik2", "movie 050");
  WriteFile(titantron / "100.bik2", "movie 100");
  WriteFile(titantron / "051.bik2", "movie 051");
  WriteFile(titantron / "readme.txt", "not a movie");  // extra file, should be ignored by stem check? (no .bik2 filtering in list)

  Paths paths;
  paths.exe_dir = root;
  paths.userdata_dir = root / "userdata";

  const std::vector<TitantronMovie> movies = ListTitantronMovies(game);
  Check(movies.size() == 4, "titantron listing count wrong");  // includes readme.txt stem "readme"
  // Numeric stems sort numerically: 050, 051, 100; the non-numeric "readme" sorts after them.
  Check(movies[0].stem == "050" && movies[1].stem == "051" && movies[2].stem == "100",
        "titantron movies did not sort numerically by stem");
  Check(movies[3].stem == "readme", "non-numeric stem did not appear in the list");

  Check(IsSupportedVideoFile(fs::path("clip.mp4")) && IsSupportedVideoFile(fs::path("a.MKV")) &&
            !IsSupportedVideoFile(fs::path("clip.exe")) && !IsSupportedVideoFile(fs::path("clip.txt")),
        "supported-video extension check is wrong");

  const fs::path source = root / "intro.mp4";
  WriteFile(source, "video bytes");
  const Result assigned = AssignEntranceVideo(paths, "050", source);
  Check(assigned.ok, "assigning an entrance video failed: " + assigned.error);
  const fs::path copied = paths.userdata_dir / "custom" / "entrance-videos" / "050.mp4";
  Check(fs::is_regular_file(copied), "assigned video was not copied to the entrance-videos folder");
  Check(ReadTextFile(copied) == "video bytes", "assigned video contents differ");

  auto videos = ListEntranceVideos(paths);
  Check(videos.size() == 1 && videos[0].stem == "050" && videos[0].source == "050.mp4",
        "entrance-video listing is wrong after assignment");

  // Assigning a video with a different extension replaces the previous one.
  const fs::path other = root / "second.mkv";
  WriteFile(other, "other bytes");
  const Result replaced = AssignEntranceVideo(paths, "050", other);
  Check(replaced.ok, "re-assigning an entrance video failed: " + replaced.error);
  Check(!fs::exists(copied), "old video was not removed when the assignment changed");
  Check(fs::is_regular_file(paths.userdata_dir / "custom" / "entrance-videos" / "050.mkv"),
        "re-assigned video was not copied with its own extension");

  const Result bad_stem = AssignEntranceVideo(paths, "../050", source);
  Check(!bad_stem.ok, "an unsafe titantron stem was accepted");
  const Result bad_video = AssignEntranceVideo(paths, "051", fs::path("notes.txt"));
  Check(!bad_video.ok, "a non-video file was accepted as an entrance video");

  const Result removed = RemoveEntranceVideo(paths, "050");
  Check(removed.ok, "removing an entrance video failed: " + removed.error);
  Check(ListEntranceVideos(paths).empty(), "entrance video was not removed");
  Check(!RemoveEntranceVideo(paths, "050").ok, "removing a missing entrance video did not fail");
  Check(!RemoveEntranceVideo(paths, "nope").ok, "removing an unassigned titantron did not fail");
}

}  // namespace

int main() {
  try {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("wwe13-videos-test-" + std::to_string(stamp));
    fs::create_directories(root);
    TestVideos(root);
    std::error_code error;
    fs::remove_all(root, error);
    Check(!error, "temporary videos fixture folder could not be removed");
    std::cout << "videos_test: PASS (list, assign, replace, remove, extension checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "videos_test: FAIL: " << error.what() << '\n';
    return 1;
  }
}
