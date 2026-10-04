#include "launcher_core.h"

#include "internal/util.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <system_error>

namespace wwe13::launcher {
namespace {

fs::path EntranceVideosDir(const Paths& paths) {
  return paths.userdata_dir / "custom" / "entrance-videos";
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

// A titantron stem is a safe single path component (the assignment key): no separators, no
// control characters, not a traversal name. This is the key we write into the entrance-videos folder.
bool IsValidStem(std::string_view stem) {
  if (stem.empty() || stem == "." || stem == ".." || stem.size() > 128) return false;
  for (unsigned char character : stem) {
    if (character == 0 || character == '/' || character == '\\' || character == ':' || character < 0x20) {
      return false;
    }
  }
  return true;
}

// Numeric stems sort as numbers (050, 051, ..., 100), non-numeric stems sort as text.
bool StemLess(const std::string& left, const std::string& right) {
  const bool left_number = !left.empty() && std::all_of(left.begin(), left.end(), [](unsigned char c) {
                             return std::isdigit(c) != 0;
                           });
  const bool right_number = !right.empty() && std::all_of(right.begin(), right.end(), [](unsigned char c) {
                              return std::isdigit(c) != 0;
                            });
  if (left_number && right_number) {
    const uint64_t left_value = std::strtoull(left.c_str(), nullptr, 10);
    const uint64_t right_value = std::strtoull(right.c_str(), nullptr, 10);
    if (left_value != right_value) return left_value < right_value;
  }
  return left < right;
}

}  // namespace

const std::vector<std::string>& SupportedVideoExtensions() {
  static const std::vector<std::string> extensions = {
      ".mp4", ".mkv", ".avi", ".mov", ".webm", ".wmv", ".mpg", ".mpeg", ".m4v"};
  return extensions;
}

bool IsSupportedVideoFile(const fs::path& file) {
  const std::string extension = Lower(internal::PathToUtf8(file.extension()));
  for (const auto& supported : SupportedVideoExtensions()) {
    if (extension == supported) return true;
  }
  return false;
}

std::vector<TitantronMovie> ListTitantronMovies(const fs::path& game_folder) {
  std::vector<TitantronMovie> movies;
  if (game_folder.empty()) return movies;
  try {
    const fs::path directory = game_folder / "movies" / "titantron";
    std::error_code error;
    if (!fs::is_directory(directory, error) || error) return movies;
    for (fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
         !error && iterator != end; iterator.increment(error)) {
      error.clear();
      if (!iterator->is_regular_file(error) || error) continue;
      const std::string stem = internal::PathToUtf8(iterator->path().stem());
      if (!IsValidStem(stem)) continue;
      TitantronMovie movie;
      movie.stem = stem;
      movie.path = iterator->path();
      movies.push_back(std::move(movie));
    }
  } catch (...) {
    movies.clear();
  }
  std::sort(movies.begin(), movies.end(), [](const TitantronMovie& left, const TitantronMovie& right) {
    return StemLess(left.stem, right.stem);
  });
  return movies;
}

std::vector<EntranceVideo> ListEntranceVideos(const Paths& paths) {
  std::vector<EntranceVideo> videos;
  try {
    const fs::path directory = EntranceVideosDir(paths);
    std::error_code error;
    if (!fs::is_directory(directory, error) || error) return videos;
    for (fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
         !error && iterator != end; iterator.increment(error)) {
      error.clear();
      if (!iterator->is_regular_file(error) || error) continue;
      const std::string stem = internal::PathToUtf8(iterator->path().stem());
      if (!IsValidStem(stem)) continue;
      EntranceVideo video;
      video.stem = stem;
      video.path = iterator->path();
      video.source = internal::PathToUtf8(iterator->path().filename());
      videos.push_back(std::move(video));
    }
  } catch (...) {
    videos.clear();
  }
  std::sort(videos.begin(), videos.end(), [](const EntranceVideo& left, const EntranceVideo& right) {
    return StemLess(left.stem, right.stem);
  });
  return videos;
}

Result AssignEntranceVideo(const Paths& paths, std::string_view titantron_stem, const fs::path& video_file) {
  try {
    const std::string stem(titantron_stem);
    if (!IsValidStem(stem)) return Result::Fail("That titantron could not be matched.");
    std::error_code error;
    if (!fs::is_regular_file(video_file, error) || error) {
      return Result::Fail("Choose a video file to assign to this titantron.");
    }
    if (!IsSupportedVideoFile(video_file)) {
      return Result::Fail("Choose a supported video file (MP4, MKV, AVI, MOV, WebM, WMV, MPEG).");
    }
    const fs::path directory = EntranceVideosDir(paths);
    if (!internal::EnsureDirectory(directory)) {
      return Result::Fail("The entrance-videos folder could not be created.");
    }
    // One assignment per titantron: remove any earlier video for this stem, then copy the new one in
    // under <stem>.<original extension>.
    for (const fs::directory_entry& entry :
         fs::directory_iterator(directory, fs::directory_options::skip_permission_denied, error)) {
      error.clear();
      if (!entry.is_regular_file(error) || error) continue;
      if (internal::PathToUtf8(entry.path().stem()) == stem) {
        fs::remove(entry.path(), error);
        if (error) return Result::Fail("The previous video for this titantron could not be removed.");
      }
    }
    const fs::path destination = directory / internal::PathFromUtf8(stem + internal::PathToUtf8(video_file.extension()));
    error.clear();
    if (!fs::copy_file(video_file, destination, fs::copy_options::none, error) || error) {
      return Result::Fail("The video could not be copied into the entrance-videos folder.");
    }
    return Result::Ok();
  } catch (...) {
    return Result::Fail("The video could not be assigned.");
  }
}

Result RemoveEntranceVideo(const Paths& paths, std::string_view titantron_stem) {
  try {
    const std::string stem(titantron_stem);
    if (!IsValidStem(stem)) return Result::Fail("That titantron could not be matched.");
    const fs::path directory = EntranceVideosDir(paths);
    std::error_code error;
    if (!fs::is_directory(directory, error) || error) return Result::Ok();
    bool removed = false;
    for (const fs::directory_entry& entry :
         fs::directory_iterator(directory, fs::directory_options::skip_permission_denied, error)) {
      error.clear();
      if (!entry.is_regular_file(error) || error) continue;
      if (internal::PathToUtf8(entry.path().stem()) == stem) {
        fs::remove(entry.path(), error);
        if (error) return Result::Fail("The video could not be removed.");
        removed = true;
      }
    }
    return removed ? Result::Ok() : Result::Fail("This titantron has no assigned video.");
  } catch (...) {
    return Result::Fail("The video could not be removed.");
  }
}

}  // namespace wwe13::launcher
