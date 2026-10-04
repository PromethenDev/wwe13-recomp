#include "launcher_core.h"

#include "internal/stfs.h"
#include "internal/util.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace wwe13::launcher {
namespace {

constexpr std::string_view kAccountId = "B13EBABEBABEBABE";
constexpr uint32_t kWwe13TitleId = 0x545108B4;
constexpr uint32_t kLegacyEntranceTitleId = 0x54510890;
constexpr uint32_t kSaveContentType = 0x00000001;
constexpr uint64_t kContentHeaderSize = 0x14C;
constexpr size_t kHeaderDisplayNameOffset = 0x08;
constexpr size_t kHeaderPackageNameOffset = 0x108;
constexpr size_t kHeaderXuidOffset = 0x138;
constexpr size_t kHeaderTitleIdOffset = 0x140;
constexpr size_t kHeaderLicenseOffset = 0x148;
constexpr std::string_view kPayloadName = "SaveData.Dat";

struct ParsedCreation {
  CreationInfo info;
  std::unique_ptr<internal::StfsReader> reader;
  const internal::StfsEntry* payload = nullptr;
};

struct StagedCreation {
  CreationInfo info;
  fs::path stage_package;
  fs::path stage_header;
  fs::path destination;
  fs::path header_destination;
  fs::path old_package;
  fs::path old_header;
  bool old_package_moved = false;
  bool old_header_moved = false;
  bool package_installed = false;
  bool header_installed = false;
};

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string PathToUtf8(const fs::path& path) {
  return internal::PathToUtf8(path);
}

fs::path PathFromUtf8(const std::string& text) {
  return internal::PathFromUtf8(text);
}

bool IsValidComponent(const std::string& name) {
  if (name.empty() || name == "." || name == "..") return false;
  for (unsigned char character : name) {
    if (character == 0 || character == '/' || character == '\\' || character == ':' || character < 0x20) {
      return false;
    }
  }
  return true;
}

std::optional<CreationKind> KindFromExtension(const fs::path& path) {
  const std::string extension = Lower(PathToUtf8(path.extension()));
  if (extension == ".cas") return CreationKind::kSuperstar;
  if (extension == ".enc") return CreationKind::kEntrance;
  if (extension == ".car") return CreationKind::kArena;
  if (extension == ".pt") return CreationKind::kLogos;
  if (extension == ".dat") return CreationKind::kSave;
  return std::nullopt;
}

std::string SlotFromName(const std::string& name, CreationKind kind) {
  if (kind == CreationKind::kSave) return "—";
  if (name.size() >= 2 && std::isdigit(static_cast<unsigned char>(name[0])) &&
      std::isdigit(static_cast<unsigned char>(name[1]))) {
    return name.substr(0, 2);
  }
  return "—";
}

uint32_t ReadBigEndianU32(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

void WriteBigEndianU32(uint8_t* bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value >> 24);
  bytes[1] = static_cast<uint8_t>(value >> 16);
  bytes[2] = static_cast<uint8_t>(value >> 8);
  bytes[3] = static_cast<uint8_t>(value);
}

void WriteBigEndianU64(uint8_t* bytes, uint64_t value) {
  for (unsigned index = 0; index < 8; ++index) {
    bytes[index] = static_cast<uint8_t>(value >> (56 - index * 8));
  }
}

void AppendUtf8(uint32_t codepoint, std::string* result) {
  if (codepoint <= 0x7F) {
    result->push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    result->push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    result->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    result->push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    result->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    result->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    result->push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    result->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    result->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    result->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::string DecodeUtf16Be(const uint8_t* bytes, size_t size) {
  std::string result;
  for (size_t offset = 0; offset + 1 < size; offset += 2) {
    uint32_t codepoint = (static_cast<uint32_t>(bytes[offset]) << 8) | bytes[offset + 1];
    if (!codepoint) break;
    if (codepoint >= 0xD800 && codepoint <= 0xDBFF && offset + 3 < size) {
      const uint32_t low = (static_cast<uint32_t>(bytes[offset + 2]) << 8) | bytes[offset + 3];
      if (low >= 0xDC00 && low <= 0xDFFF) {
        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
        offset += 2;
      } else {
        codepoint = 0xFFFD;
      }
    } else if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
      codepoint = 0xFFFD;
    }
    AppendUtf8(codepoint, &result);
  }
  return result;
}

bool EncodeUtf8ToUtf16Be(const std::string& input, std::vector<uint8_t>* output) {
  output->clear();
  auto append_unit = [&](uint16_t unit) {
    output->push_back(static_cast<uint8_t>(unit >> 8));
    output->push_back(static_cast<uint8_t>(unit));
  };
  for (size_t offset = 0; offset < input.size();) {
    const uint8_t first = static_cast<uint8_t>(input[offset]);
    uint32_t codepoint = 0;
    size_t count = 0;
    if (first <= 0x7F) {
      codepoint = first;
      count = 1;
    } else if ((first & 0xE0) == 0xC0) {
      codepoint = first & 0x1F;
      count = 2;
    } else if ((first & 0xF0) == 0xE0) {
      codepoint = first & 0x0F;
      count = 3;
    } else if ((first & 0xF8) == 0xF0) {
      codepoint = first & 0x07;
      count = 4;
    } else {
      return false;
    }
    if (offset + count > input.size()) return false;
    for (size_t index = 1; index < count; ++index) {
      const uint8_t next = static_cast<uint8_t>(input[offset + index]);
      if ((next & 0xC0) != 0x80) return false;
      codepoint = (codepoint << 6) | (next & 0x3F);
    }
    if ((count == 2 && codepoint < 0x80) || (count == 3 && codepoint < 0x800) ||
        (count == 4 && codepoint < 0x10000) || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
      return false;
    }
    if (codepoint <= 0xFFFF) {
      append_unit(static_cast<uint16_t>(codepoint));
    } else {
      codepoint -= 0x10000;
      append_unit(static_cast<uint16_t>(0xD800 + (codepoint >> 10)));
      append_unit(static_cast<uint16_t>(0xDC00 + (codepoint & 0x3FF)));
    }
    offset += count;
  }
  return true;
}

fs::path AccountRoot(const Paths& paths) {
  return paths.userdata_dir / PathFromUtf8(std::string(kAccountId)) / "545108B4";
}

fs::path PackageRoot(const Paths& paths) {
  return AccountRoot(paths) / "00000001";
}

fs::path HeaderRoot(const Paths& paths) {
  return AccountRoot(paths) / "Headers" / "00000001";
}

bool MakeContentHeader(const std::string& display_name, const std::string& package_name,
                       std::vector<uint8_t>* output, std::string* error) {
  if (!IsValidComponent(package_name) || package_name.size() > 42) {
    if (error) *error = "This creation package has a name that is too long to install.";
    return false;
  }
  std::vector<uint8_t> display;
  if (!EncodeUtf8ToUtf16Be(display_name, &display) || display.size() > 0xFE) {
    if (error) *error = "This creation has a display name that is too long to install.";
    return false;
  }
  output->assign(kContentHeaderSize, 0);
  WriteBigEndianU32(output->data(), 1);  // HDD
  WriteBigEndianU32(output->data() + 4, kSaveContentType);
  std::copy(display.begin(), display.end(), output->begin() + kHeaderDisplayNameOffset);
  std::copy(package_name.begin(), package_name.end(),
            output->begin() + kHeaderPackageNameOffset);
  WriteBigEndianU64(output->data() + kHeaderXuidOffset, 0);
  WriteBigEndianU32(output->data() + kHeaderTitleIdOffset, kWwe13TitleId);
  (*output)[kHeaderLicenseOffset] = 0xFF;
  (*output)[kHeaderLicenseOffset + 1] = 0xFF;
  (*output)[kHeaderLicenseOffset + 2] = 0xFF;
  (*output)[kHeaderLicenseOffset + 3] = 0xFF;
  return true;
}

bool LoadCreation(const fs::path& package_path, ParsedCreation* parsed, std::string* error) {
  auto reader = std::make_unique<internal::StfsReader>();
  if (!reader->Open(package_path, error)) return false;
  const auto& metadata = reader->metadata();
  if (metadata.content_type != kSaveContentType) {
    if (error) *error = "This is not a WWE '13 creation or save package.";
    return false;
  }
  const auto kind = KindFromExtension(package_path);
  if (!kind) {
    if (error) *error = "This file type is not supported as a creation or save.";
    return false;
  }
  const std::string package_name = PathToUtf8(package_path.filename());
  if (!IsValidComponent(package_name) || package_name.size() > 42) {
    if (error) *error = "This creation package has an invalid or overly long filename.";
    return false;
  }
  const bool legacy_entrance = *kind == CreationKind::kEntrance &&
                               metadata.title_id == kLegacyEntranceTitleId;
  if (metadata.title_id != kWwe13TitleId && !legacy_entrance) {
    if (error) {
      *error = *kind == CreationKind::kEntrance
                   ? "This entrance package is not for WWE '13 (title ID 545108B4 or the known 54510890 variant)."
                   : "This package is not for WWE '13.";
    }
    return false;
  }
  const internal::StfsEntry* payload = nullptr;
  for (const auto& entry : reader->entries()) {
    if (entry.directory) continue;
    if (payload || Lower(PathToUtf8(entry.relative_path)) != Lower(std::string(kPayloadName))) {
      if (error) *error = "This package does not contain the expected SaveData.Dat file.";
      return false;
    }
    payload = &entry;
  }
  if (!payload || payload->size == 0) {
    if (error) *error = "This package does not contain the expected SaveData.Dat file.";
    return false;
  }
  CreationInfo info;
  info.kind = *kind;
  info.display_name = metadata.display_name;
  info.package_name = package_name;
  info.slot = SlotFromName(package_name, *kind);
  info.title_id = metadata.title_id;
  info.title_id_normalized = legacy_entrance;
  if (*kind != CreationKind::kSave && info.slot == "—") {
    if (error) *error = "This creation filename must begin with its two-digit slot number.";
    return false;
  }
  parsed->info = std::move(info);
  parsed->reader = std::move(reader);
  parsed->payload = payload;
  return true;
}

bool ReadSidecar(const fs::path& path, std::vector<uint8_t>* bytes) {
  if (!internal::ReadBinaryFile(path, *bytes) || bytes->size() < kHeaderTitleIdOffset + 4) return false;
  return true;
}

std::string SidecarName(const std::vector<uint8_t>& bytes) {
  if (bytes.size() < kHeaderPackageNameOffset + 1) return {};
  size_t length = 0;
  while (kHeaderPackageNameOffset + length < std::min<size_t>(bytes.size(), 0x138) &&
         bytes[kHeaderPackageNameOffset + length] != 0) {
    ++length;
  }
  return std::string(reinterpret_cast<const char*>(bytes.data() + kHeaderPackageNameOffset), length);
}

bool IsSafeNameMatch(const std::string& left, const std::string& right) {
  return Lower(left) == Lower(right);
}

fs::path UniqueStageRoot(const Paths& paths, std::error_code* error) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  for (unsigned index = 0; index < 100; ++index) {
    fs::path candidate = paths.userdata_dir / (".wwe13-creations-" + std::to_string(now) + "-" +
                                                std::to_string(index));
    error->clear();
    if (fs::create_directory(candidate, *error) && !*error) return candidate;
    if (*error && *error != std::errc::file_exists) return {};
  }
  *error = std::make_error_code(std::errc::file_exists);
  return {};
}

Result StageOne(ParsedCreation& parsed, const fs::path& stage_root,
                const ProgressFn& progress, const CancelFlag& cancel,
                uint64_t* completed, uint64_t total) {
  std::error_code error;
  StagedCreation staged;
  staged.info = parsed.info;
  staged.stage_package = stage_root / "packages" / PathFromUtf8(staged.info.package_name);
  staged.stage_header = stage_root / "headers" /
                        PathFromUtf8(staged.info.package_name + ".header");
  fs::create_directories(staged.stage_package, error);
  if (error) return Result::Fail("The creation's temporary folder could not be created.");
  fs::create_directories(staged.stage_header.parent_path(), error);
  if (error) return Result::Fail("The creation's temporary folder could not be created.");
  const fs::path payload_path = staged.stage_package / std::string(kPayloadName);
  std::ofstream output(payload_path, std::ios::binary | std::ios::trunc);
  if (!output) return Result::Fail("A temporary creation file could not be created.");
  if (progress) progress(*completed, total, staged.info.package_name);
  auto after_chunk = [&](uint64_t count) {
    *completed += count;
    if (progress) progress(*completed, total, staged.info.package_name);
    return !cancel.load();
  };
  std::string read_error;
  const bool copied = parsed.reader->CopyFile(*parsed.payload, output, after_chunk, &read_error);
  output.close();
  if (cancel.load()) return Result::Fail("The import was canceled.");
  if (!copied || !output) {
    return Result::Fail(read_error.empty() ? "A file in this creation package could not be read." : read_error);
  }
  std::vector<uint8_t> header;
  if (!MakeContentHeader(staged.info.display_name, staged.info.package_name, &header, &read_error)) {
    return Result::Fail(read_error);
  }
  if (!internal::WriteBinaryFile(staged.stage_header, header)) {
    return Result::Fail("The creation's content information could not be written.");
  }
  return Result::Ok();
}

void RollBack(std::vector<StagedCreation>* packages) {
  for (auto iterator = packages->rbegin(); iterator != packages->rend(); ++iterator) {
    std::error_code ignored;
    if (iterator->package_installed) fs::remove_all(iterator->destination, ignored);
    ignored.clear();
    if (iterator->header_installed) fs::remove(iterator->header_destination, ignored);
    ignored.clear();
    if (iterator->old_package_moved) fs::rename(iterator->old_package, iterator->destination, ignored);
    ignored.clear();
    if (iterator->old_header_moved) fs::rename(iterator->old_header, iterator->header_destination, ignored);
  }
}

Result InstallBatch(const Paths& paths, std::vector<ParsedCreation>* parsed,
                    bool replace_existing, const ProgressFn& progress, const CancelFlag& cancel,
                    fs::path* backup_file_out = nullptr) {
  if (parsed->empty()) return Result::Fail("Choose at least one creation package to add.");
  std::set<std::string> unique_names;
  uint64_t total = 0;
  for (const auto& package : *parsed) {
    if (package.info.kind == CreationKind::kSave && package.info.package_name != "SaveData.dat") {
      return Result::Fail("Use Import a full save to replace the main WWE '13 save.");
    }
    const std::string name = Lower(package.info.package_name);
    if (!unique_names.insert(name).second) {
      return Result::Fail("The selected files contain the same creation slot more than once.");
    }
    total += package.payload->size;
  }
  std::error_code error;
  const fs::path content_root = PackageRoot(paths);
  const fs::path headers_root = HeaderRoot(paths);
  for (const auto& package : *parsed) {
    const fs::path destination = content_root / PathFromUtf8(package.info.package_name);
    const fs::path header = headers_root / PathFromUtf8(package.info.package_name + ".header");
    error.clear();
    const bool content_exists = fs::exists(destination, error);
    if (error) return Result::Fail("The existing creation folder could not be checked.");
    error.clear();
    const bool header_exists = fs::exists(header, error);
    if (error) return Result::Fail("The existing creation information could not be checked.");
    if ((content_exists || header_exists) && !replace_existing) {
      return Result::Fail("A creation already exists in this slot. Confirm replacement to continue.");
    }
    error.clear();
    if ((content_exists && fs::is_symlink(fs::symlink_status(destination, error))) || error) {
      return Result::Fail("The existing creation folder cannot be replaced safely.");
    }
    error.clear();
    if ((header_exists && fs::is_symlink(fs::symlink_status(header, error))) || error) {
      return Result::Fail("The existing creation information cannot be replaced safely.");
    }
  }
  if (cancel.load()) return Result::Fail("The import was canceled.");
  const auto space = fs::space(paths.userdata_dir, error);
  if (!error && space.available < total + parsed->size() * 0x10000ull) {
    return Result::Fail("There is not enough free space to install these creations.");
  }

  BackupInfo backup;
  const Result backup_result = BackupSaves(paths, true, &backup);
  if (!backup_result.ok) return backup_result;
  if (backup_file_out) *backup_file_out = backup.file;

  fs::create_directories(paths.userdata_dir, error);
  if (error) return Result::Fail("The saved-game folder could not be created.");

  const fs::path stage_root = UniqueStageRoot(paths, &error);
  if (stage_root.empty() || error) return Result::Fail("A temporary creation folder could not be created.");
  std::vector<StagedCreation> staged;
  staged.reserve(parsed->size());
  uint64_t completed = 0;
  Result result = Result::Ok();
  for (auto& package : *parsed) {
    if (cancel.load()) {
      result = Result::Fail("The import was canceled.");
      break;
    }
    const Result one = StageOne(package, stage_root, progress, cancel, &completed, total);
    if (!one.ok) {
      result = one;
      break;
    }
    StagedCreation record;
    record.info = package.info;
    record.stage_package = stage_root / "packages" / PathFromUtf8(package.info.package_name);
    record.stage_header = stage_root / "headers" / PathFromUtf8(package.info.package_name + ".header");
    record.destination = content_root / PathFromUtf8(package.info.package_name);
    record.header_destination = headers_root / PathFromUtf8(package.info.package_name + ".header");
    record.old_package = stage_root / "old-packages" / PathFromUtf8(package.info.package_name);
    record.old_header = stage_root / "old-headers" / PathFromUtf8(package.info.package_name + ".header");
    staged.push_back(std::move(record));
  }
  if (!result.ok) {
    fs::remove_all(stage_root, error);
    return result;
  }
  if (cancel.load()) {
    fs::remove_all(stage_root, error);
    return Result::Fail("The import was canceled.");
  }
  fs::create_directories(content_root, error);
  if (error) {
    fs::remove_all(stage_root, error);
    return Result::Fail("The saved-game content folder could not be created.");
  }
  fs::create_directories(headers_root, error);
  if (error) {
    fs::remove_all(stage_root, error);
    return Result::Fail("The saved-game content folder could not be created.");
  }

  for (auto& item : staged) {
    error.clear();
    if (fs::exists(item.destination, error)) {
      if (error || !replace_existing) {
        RollBack(&staged);
        fs::remove_all(stage_root, error);
        return Result::Fail("A creation already exists in this slot. Confirm replacement to continue.");
      }
      fs::create_directories(item.old_package.parent_path(), error);
      if (error) {
        RollBack(&staged);
        fs::remove_all(stage_root, error);
        return Result::Fail("The previous creation could not be backed up for replacement.");
      }
      fs::rename(item.destination, item.old_package, error);
      if (error) {
        RollBack(&staged);
        fs::remove_all(stage_root, error);
        return Result::Fail("The previous creation could not be backed up for replacement.");
      }
      item.old_package_moved = true;
    } else if (error) {
      RollBack(&staged);
      fs::remove_all(stage_root, error);
      return Result::Fail("The existing creation folder could not be checked.");
    }
    error.clear();
    if (fs::exists(item.header_destination, error)) {
      if (error || !replace_existing) {
        RollBack(&staged);
        fs::remove_all(stage_root, error);
        return Result::Fail("A creation already exists in this slot. Confirm replacement to continue.");
      }
      fs::create_directories(item.old_header.parent_path(), error);
      if (error) {
        RollBack(&staged);
        fs::remove_all(stage_root, error);
        return Result::Fail("The previous creation information could not be backed up.");
      }
      fs::rename(item.header_destination, item.old_header, error);
      if (error) {
        RollBack(&staged);
        fs::remove_all(stage_root, error);
        return Result::Fail("The previous creation information could not be backed up.");
      }
      item.old_header_moved = true;
    } else if (error) {
      RollBack(&staged);
      fs::remove_all(stage_root, error);
      return Result::Fail("The existing creation information could not be checked.");
    }
    fs::rename(item.stage_package, item.destination, error);
    if (error) {
      RollBack(&staged);
      fs::remove_all(stage_root, error);
      return Result::Fail("A creation could not be installed.");
    }
    item.package_installed = true;
    fs::rename(item.stage_header, item.header_destination, error);
    if (error) {
      RollBack(&staged);
      fs::remove_all(stage_root, error);
      return Result::Fail("Creation information could not be installed.");
    }
    item.header_installed = true;
  }
  fs::remove_all(stage_root, error);
  return Result::Ok();
}

// -------------------------------------------------------------------------------------- content pack manifest
// One tab-separated line per pack: id, when, save_indexed(0|1), backup basename, then the package names.
// Newer lines add the chosen pack name as an extra field right before the packages, marked "name:" so an
// older line (whose fifth field is its first package) still reads as a pack without a name.
// Package names cannot contain tabs/newlines or ':' (IsValidComponent rejects them), so the format is safe.
constexpr char kManifestSeparator = '\t';
constexpr char kManifestNamePrefix[] = "name:";

fs::path ManifestPath(const Paths& paths) {
  return paths.userdata_dir / "custom" / "content-packs.txt";
}

std::string NewPackId() {
  const auto now = std::chrono::system_clock::now();
  const auto nanos = std::chrono::steady_clock::now().time_since_epoch().count();
  return internal::LocalTimestamp(now, "%Y%m%d-%H%M%S") + "-" + std::to_string(nanos);
}

std::string NewPackWhen() {
  return internal::LocalTimestamp(std::chrono::system_clock::now(), "%Y-%m-%dT%H:%M:%S");
}

// A pack name comes from a folder/file name, so it can contain tabs/newlines on some systems; fold them to
// spaces and cap the length so it cannot break the tab-separated manifest or the Content Packs column.
std::string SanitizePackName(std::string name) {
  std::string cleaned;
  cleaned.reserve(name.size());
  for (unsigned char character : name) {
    if (character == '\t' || character == '\n' || character == '\r') {
      cleaned.push_back(' ');
      continue;
    }
    cleaned.push_back(static_cast<char>(character));
  }
  const size_t begin = cleaned.find_first_not_of(' ');
  const size_t end = cleaned.find_last_not_of(' ');
  if (begin == std::string::npos) return {};
  cleaned = cleaned.substr(begin, end - begin + 1);
  if (cleaned.size() > 80) cleaned.resize(80);
  return cleaned;
}

std::vector<std::string> SplitManifestLine(const std::string& line) {
  std::vector<std::string> fields;
  size_t start = 0;
  while (true) {
    const size_t tab = line.find(kManifestSeparator, start);
    if (tab == std::string::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
  return fields;
}

std::vector<ContentPack> ReadContentPacks(const Paths& paths) {
  std::vector<ContentPack> packs;
  try {
    std::string text;
    if (!internal::ReadTextFile(ManifestPath(paths), text)) return packs;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
      if (line.empty()) continue;
      const std::vector<std::string> fields = SplitManifestLine(line);
      if (fields.size() < 4) continue;
      ContentPack pack;
      pack.id = fields[0];
      pack.when = fields[1];
      pack.save_indexed = fields[2] == "1";
      pack.backup = fields[3];
      size_t first_package = 4;
      if (fields.size() >= 5 && fields[4].rfind(kManifestNamePrefix, 0) == 0) {
        pack.name = fields[4].substr(sizeof(kManifestNamePrefix) - 1);
        first_package = 5;
      }
      for (size_t index = first_package; index < fields.size(); ++index) {
        if (!fields[index].empty()) pack.packages.push_back(fields[index]);
      }
      packs.push_back(std::move(pack));
    }
  } catch (...) {
    packs.clear();
  }
  return packs;
}

void WriteContentPacks(const Paths& paths, const std::vector<ContentPack>& packs) {
  std::ostringstream output;
  for (const auto& pack : packs) {
    output << pack.id << kManifestSeparator << pack.when << kManifestSeparator
           << (pack.save_indexed ? "1" : "0") << kManifestSeparator << pack.backup;
    if (!pack.name.empty()) output << kManifestSeparator << kManifestNamePrefix << pack.name;
    for (const auto& package : pack.packages) output << kManifestSeparator << package;
    output << '\n';
  }
  internal::WriteTextFileAtomic(ManifestPath(paths), output.str());
}

void RecordPack(const Paths& paths, const std::vector<std::string>& packages, const fs::path& backup_file,
                const std::string& pack_name) {
  std::vector<ContentPack> packs = ReadContentPacks(paths);
  ContentPack pack;
  pack.id = NewPackId();
  pack.when = NewPackWhen();
  pack.name = SanitizePackName(pack_name);
  pack.packages = packages;
  pack.backup = internal::PathToUtf8(backup_file.filename());
  packs.push_back(std::move(pack));
  WriteContentPacks(paths, packs);
}

// After a full save is imported, every pack installed before it is now indexed by the save's slots.
void MarkPacksSaveIndexed(const Paths& paths) {
  std::vector<ContentPack> packs = ReadContentPacks(paths);
  bool changed = false;
  for (auto& pack : packs) {
    if (!pack.save_indexed) {
      pack.save_indexed = true;
      changed = true;
    }
  }
  if (changed) WriteContentPacks(paths, packs);
}

}  // namespace

std::optional<CreationInfo> InspectCreationPackage(const fs::path& package) {
  try {
    ParsedCreation parsed;
    std::string error;
    if (!LoadCreation(package, &parsed, &error)) return std::nullopt;
    return parsed.info;
  } catch (...) {
    return std::nullopt;
  }
}

std::vector<fs::path> FindCreationPackages(const fs::path& folder) {
  std::vector<fs::path> packages;
  try {
    std::error_code error;
    if (!fs::is_directory(folder, error) || error) return packages;
    for (fs::directory_iterator iterator(folder, fs::directory_options::skip_permission_denied, error), end;
         !error && iterator != end; iterator.increment(error)) {
      error.clear();
      if (!iterator->is_regular_file(error) || error) continue;
      if (InspectCreationPackage(iterator->path())) packages.push_back(iterator->path());
    }
  } catch (...) {
    packages.clear();
  }
  std::sort(packages.begin(), packages.end());
  return packages;
}

std::vector<CreationInfo> ListInstalledCreations(const Paths& paths) {
  std::vector<CreationInfo> installed;
  try {
    const fs::path root = PackageRoot(paths);
    std::error_code error;
    if (!fs::is_directory(root, error) || error) return installed;
    for (fs::directory_iterator iterator(root, fs::directory_options::skip_permission_denied, error), end;
         !error && iterator != end; iterator.increment(error)) {
      error.clear();
      const fs::path directory = iterator->path();
      const fs::file_status status = iterator->symlink_status(error);
      if (error || fs::is_symlink(status) || !fs::is_directory(status)) continue;
      const std::string package_name = PathToUtf8(directory.filename());
      const auto kind = KindFromExtension(directory);
      if (!kind || !IsValidComponent(package_name)) continue;
      error.clear();
      const fs::path payload = directory / std::string(kPayloadName);
      if (!fs::is_regular_file(payload, error) || error) continue;
      CreationInfo info;
      info.kind = *kind;
      info.package_name = package_name;
      info.slot = SlotFromName(package_name, *kind);
      info.title_id = kWwe13TitleId;
      info.display_name = package_name;
      error.clear();
      info.when = internal::IsoLocalTimestamp(fs::last_write_time(directory, error));
      if (error) info.when.clear();
      error.clear();
      std::vector<uint8_t> header;
      const fs::path header_path = HeaderRoot(paths) / PathFromUtf8(package_name + ".header");
      if (ReadSidecar(header_path, &header) && ReadBigEndianU32(header.data() + 4) == kSaveContentType &&
          ReadBigEndianU32(header.data() + kHeaderTitleIdOffset) == kWwe13TitleId) {
        const std::string recorded_name = SidecarName(header);
        if (recorded_name.empty() || IsSafeNameMatch(recorded_name, package_name)) {
          const size_t display_size = std::min<size_t>(0x100, header.size() - kHeaderDisplayNameOffset);
          const std::string display_name = DecodeUtf16Be(header.data() + kHeaderDisplayNameOffset, display_size);
          if (!display_name.empty()) info.display_name = display_name;
        }
      }
      installed.push_back(std::move(info));
    }
  } catch (...) {
    installed.clear();
  }
  std::sort(installed.begin(), installed.end(), [](const CreationInfo& left, const CreationInfo& right) {
    if (left.kind != right.kind) return static_cast<int>(left.kind) < static_cast<int>(right.kind);
    return Lower(left.package_name) < Lower(right.package_name);
  });
  return installed;
}

Result ImportCreations(const Paths& paths, const std::vector<fs::path>& packages,
                       bool replace_existing, const ProgressFn& progress, const CancelFlag& cancel,
                       std::string pack_name) {
  try {
    std::vector<ParsedCreation> parsed;
    parsed.reserve(packages.size());
    std::vector<std::string> package_names;
    package_names.reserve(packages.size());
    for (const auto& package : packages) {
      if (cancel.load()) return Result::Fail("The import was canceled.");
      ParsedCreation item;
      std::string error;
      if (!LoadCreation(package, &item, &error)) {
        return Result::Fail(error.empty() ? "A selected creation package could not be read." : error);
      }
      if (item.info.kind == CreationKind::kSave) {
        return Result::Fail("Use Import a full save to replace the main WWE '13 save.");
      }
      package_names.push_back(item.info.package_name);
      parsed.push_back(std::move(item));
    }
    fs::path backup_file;
    const Result installed = InstallBatch(paths, &parsed, replace_existing, progress, cancel, &backup_file);
    if (!installed.ok) return installed;
    RecordPack(paths, package_names, backup_file, pack_name);
    return Result::Ok();
  } catch (...) {
    return Result::Fail("The selected creations could not be installed.");
  }
}

Result RemoveCreation(const Paths& paths, std::string_view package_name_view) {
  try {
    const std::string package_name(package_name_view);
    if (!IsValidComponent(package_name)) return Result::Fail("This creation could not be removed safely.");
    const auto kind = KindFromExtension(PathFromUtf8(package_name));
    if (!kind || *kind == CreationKind::kSave) {
      return Result::Fail("Use Import a full save to replace the main WWE '13 save.");
    }
    // A creation that a full save indexes must not be removed on its own (the game would report missing
    // content); it has to go with the rest of its pack.
    const std::vector<ContentPack> packs = ReadContentPacks(paths);
    for (const auto& pack : packs) {
      if (!pack.save_indexed) continue;
      for (const auto& name : pack.packages) {
        if (IsSafeNameMatch(name, package_name)) {
          return Result::Fail("These items came in a pack together. Remove the whole pack in Save Data instead.");
        }
      }
    }
    const fs::path destination = PackageRoot(paths) / PathFromUtf8(package_name);
    const fs::path header = HeaderRoot(paths) / PathFromUtf8(package_name + ".header");
    std::error_code error;
    const bool has_data = fs::exists(destination, error);
    if (error) return Result::Fail("The selected creation folder could not be checked.");
    error.clear();
    const bool has_header = fs::exists(header, error);
    if (error || (!has_data && !has_header)) return Result::Fail("This creation is no longer installed.");
    if ((has_data && fs::is_symlink(fs::symlink_status(destination, error))) || error) {
      return Result::Fail("This creation folder cannot be removed safely.");
    }
    error.clear();
    if ((has_header && fs::is_symlink(fs::symlink_status(header, error))) || error) {
      return Result::Fail("This creation information cannot be removed safely.");
    }
    const Result backup = BackupSaves(paths, true, nullptr);
    if (!backup.ok) return backup;
    if (has_data) {
      fs::remove_all(destination, error);
      if (error) return Result::Fail("The creation could not be removed.");
    }
    if (has_header) {
      error.clear();
      fs::remove(header, error);
      if (error) return Result::Fail("The creation data was removed, but its content information could not be removed.");
    }
    return Result::Ok();
  } catch (...) {
    return Result::Fail("The selected creation could not be removed.");
  }
}

Result ImportFullSave(const Paths& paths, const fs::path& package,
                      const ProgressFn& progress, const CancelFlag& cancel) {
  try {
    ParsedCreation parsed;
    std::string error;
    if (!LoadCreation(package, &parsed, &error)) {
      return Result::Fail(error.empty() ? "The full save package could not be read." : error);
    }
    if (parsed.info.kind != CreationKind::kSave || parsed.info.title_id != kWwe13TitleId) {
      return Result::Fail("Choose a WWE '13 SaveData.dat package (title ID 545108B4) for a full-save import.");
    }
    parsed.info.package_name = "SaveData.dat";
    parsed.info.slot = "—";
    std::vector<ParsedCreation> one;
    one.push_back(std::move(parsed));
    const Result installed = InstallBatch(paths, &one, true, progress, cancel);
    if (!installed.ok) return installed;
    MarkPacksSaveIndexed(paths);
    return Result::Ok();
  } catch (...) {
    return Result::Fail("The full save could not be imported.");
  }
}

std::vector<ContentPack> ListContentPacks(const Paths& paths) {
  return ReadContentPacks(paths);
}

// Removes one installed creation's content folder and its header sidecar (no backup). Returns Ok when
// neither is present. Used for the no-backup pack-removal path.
Result RemovePackageFiles(const Paths& paths, std::string_view package_name) {
  const fs::path destination = PackageRoot(paths) / PathFromUtf8(std::string(package_name));
  const fs::path header = HeaderRoot(paths) / PathFromUtf8(std::string(package_name) + ".header");
  std::error_code error;
  const bool has_data = fs::exists(destination, error);
  if (error) return Result::Fail("A creation folder could not be checked.");
  error.clear();
  const bool has_header = fs::exists(header, error);
  if (error) return Result::Fail("A creation folder could not be checked.");
  if (has_data) {
    if (fs::is_symlink(fs::symlink_status(destination, error)) || error) {
      return Result::Fail("A creation folder cannot be removed safely.");
    }
    fs::remove_all(destination, error);
    if (error) return Result::Fail("A creation could not be removed.");
  }
  if (has_header) {
    error.clear();
    fs::remove(header, error);
    if (error) return Result::Fail("Creation information could not be removed.");
  }
  return Result::Ok();
}

Result RemoveContentPack(const Paths& paths, const std::string& pack_id) {
  try {
    std::vector<ContentPack> packs = ReadContentPacks(paths);
    const auto found = std::find_if(packs.begin(), packs.end(), [&](const ContentPack& pack) {
      return pack.id == pack_id;
    });
    if (found == packs.end()) return Result::Fail("That pack is no longer recorded.");
    const fs::path backup = found->backup.empty() ? fs::path{} : paths.backups_dir / PathFromUtf8(found->backup);
    std::error_code error;
    if (!backup.empty()) {
      if (!fs::is_regular_file(backup, error) || error) {
        return Result::Fail("The automatic backup for this pack is no longer available, so it cannot be restored.");
      }
      const Result restored = RestoreBackup(paths, backup);
      if (!restored.ok) return restored;
    } else {
      // No pre-import backup (fresh install with nothing to back up): remove the pack's items directly.
      // A save-indexed pack also brought its own full save; remove that too so no dangling slots remain.
      for (const auto& name : found->packages) {
        const Result removed = RemovePackageFiles(paths, name);
        if (!removed.ok) return removed;
      }
      if (found->save_indexed) {
        const Result removed = RemovePackageFiles(paths, "SaveData.dat");
        if (!removed.ok) return removed;
      }
    }
    packs.erase(found);
    WriteContentPacks(paths, packs);
    return Result::Ok();
  } catch (...) {
    return Result::Fail("The pack could not be removed.");
  }
}

bool IsCreationRemovalSafe(const Paths& paths, std::string_view package_name) {
  const std::vector<ContentPack> packs = ReadContentPacks(paths);
  for (const auto& pack : packs) {
    if (!pack.save_indexed) continue;
    for (const auto& name : pack.packages) {
      if (IsSafeNameMatch(name, std::string(package_name))) return false;
    }
  }
  return true;
}

}  // namespace wwe13::launcher
