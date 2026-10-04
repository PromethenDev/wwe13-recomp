#include "../launcher_core.h"

#include "../internal/util.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace wwe13::launcher;

namespace {

constexpr uint32_t kTitleId = 0x545108B4;
constexpr uint32_t kLegacyTitleId = 0x54510890;
constexpr uint32_t kContentType = 1;
constexpr uint32_t kEndOfChain = 0xFFFFFF;
const std::string kPayload = "synthetic WWE '13 creation/save payload\n";

void Check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void PutBe32(std::vector<uint8_t>* bytes, size_t offset, uint32_t value) {
  (*bytes)[offset] = static_cast<uint8_t>(value >> 24);
  (*bytes)[offset + 1] = static_cast<uint8_t>(value >> 16);
  (*bytes)[offset + 2] = static_cast<uint8_t>(value >> 8);
  (*bytes)[offset + 3] = static_cast<uint8_t>(value);
}

void PutBe16(std::vector<uint8_t>* bytes, size_t offset, uint16_t value) {
  (*bytes)[offset] = static_cast<uint8_t>(value >> 8);
  (*bytes)[offset + 1] = static_cast<uint8_t>(value);
}

void PutLe16(std::vector<uint8_t>* bytes, size_t offset, uint16_t value) {
  (*bytes)[offset] = static_cast<uint8_t>(value);
  (*bytes)[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void PutLe24(std::vector<uint8_t>* bytes, size_t offset, uint32_t value) {
  (*bytes)[offset] = static_cast<uint8_t>(value);
  (*bytes)[offset + 1] = static_cast<uint8_t>(value >> 8);
  (*bytes)[offset + 2] = static_cast<uint8_t>(value >> 16);
}

void WriteUtf16Be(std::vector<uint8_t>* bytes, size_t offset, const std::string& text) {
  for (size_t index = 0; index < text.size(); ++index) {
    (*bytes)[offset + index * 2] = 0;
    (*bytes)[offset + index * 2 + 1] = static_cast<uint8_t>(text[index]);
  }
}

void WriteCreationPackage(const fs::path& path, const std::string& display_name,
                          uint32_t title_id = kTitleId, uint32_t content_type = kContentType,
                          uint32_t allocated_blocks = 1) {
  constexpr size_t kHeaderSize = 0x1000;
  constexpr size_t kPackageSize = kHeaderSize + 3 * 0x1000;
  std::vector<uint8_t> bytes(kPackageSize, 0);
  std::memcpy(bytes.data(), "CON ", 4);
  PutBe32(&bytes, 0x340, kHeaderSize);
  PutBe32(&bytes, 0x344, content_type);
  PutBe32(&bytes, 0x360, title_id);
  PutBe32(&bytes, 0x3A9, 0);
  const size_t volume = 0x379;
  bytes[volume] = 0x24;
  bytes[volume + 2] = 1;  // read-only STFS layout
  PutLe16(&bytes, volume + 3, 1);  // one file-table block
  PutLe24(&bytes, volume + 5, 0);  // file table at logical block zero
  PutBe32(&bytes, volume + 0x1C, 2);  // file table + payload blocks
  WriteUtf16Be(&bytes, 0x411, display_name);

  const std::string name = "SaveData.Dat";
  const size_t entry = 0x2000;
  std::memcpy(bytes.data() + entry, name.data(), name.size());
  bytes[entry + 0x28] = static_cast<uint8_t>(name.size());
  PutLe24(&bytes, entry + 0x2C, allocated_blocks);
  PutLe24(&bytes, entry + 0x2F, 1);  // first data block
  PutBe16(&bytes, entry + 0x32, 0xFFFF);  // root directory
  PutBe32(&bytes, entry + 0x34, static_cast<uint32_t>(kPayload.size()));
  std::memcpy(bytes.data() + 0x3000, kPayload.data(), kPayload.size());
  // Hash block zero records the end of the file-table and payload block chains.
  PutBe32(&bytes, 0x1000 + 0x14, kEndOfChain);
  PutBe32(&bytes, 0x1000 + 0x18 + 0x14, kEndOfChain);

  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  Check(static_cast<bool>(output), "could not write synthetic STFS package " + path.string());
}

std::vector<uint8_t> ReadFile(const fs::path& path) {
  std::vector<uint8_t> data;
  Check(internal::ReadBinaryFile(path, data), "could not read " + path.string());
  return data;
}

std::string ReadText(const fs::path& path) {
  std::string text;
  Check(internal::ReadTextFile(path, text), "could not read text " + path.string());
  return text;
}

uint32_t Be32(const std::vector<uint8_t>& bytes, size_t offset) {
  return (static_cast<uint32_t>(bytes[offset]) << 24) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 8) | bytes[offset + 3];
}

Paths MakePaths(const fs::path& root) {
  Paths paths;
  paths.exe_dir = root;
  paths.userdata_dir = root / "userdata";
  paths.settings_file = root / "wwe13-enhanced.ini";
  paths.backups_dir = root / "backups";
  return paths;
}

const CreationInfo* Find(const std::vector<CreationInfo>& creations, const std::string& name) {
  const auto found = std::find_if(creations.begin(), creations.end(), [&](const CreationInfo& item) {
    return item.package_name == name;
  });
  return found == creations.end() ? nullptr : &*found;
}

void CheckPayload(const fs::path& path) {
  const std::vector<uint8_t> bytes = ReadFile(path);
  Check(std::string(bytes.begin(), bytes.end()) == kPayload, "installed SaveData.Dat payload differs");
}

void TestCreationImport(const fs::path& root) {
  const fs::path input = root / "input";
  const fs::path star = input / "00CustomSuperStar.cas";
  const fs::path entrance = input / "00EncodeDat.enc";
  const fs::path arena = input / "00CustomArena.car";
  const fs::path logos = input / "00PaintTool.pt";
  const fs::path legacy_entrance = input / "35EncodeDat.enc";
  const fs::path full_save = input / "SaveData.dat";
  WriteCreationPackage(star, "01(TEST).A STAR");
  WriteCreationPackage(entrance, "01(TEST).AN ENTRANCE", kTitleId, kContentType, 2);
  WriteCreationPackage(arena, "01(TEST).AN ARENA");
  WriteCreationPackage(logos, "01(TEST).CUSTOM LOGOS");
  WriteCreationPackage(legacy_entrance, "36(TEST).A LEGACY ENTRANCE", kLegacyTitleId, kContentType, 2);
  WriteCreationPackage(full_save, "Synthetic WWE '13 Save");
  const fs::path wrong_title = input / "01OtherTitle.cas";
  WriteCreationPackage(wrong_title, "Other title", 0x12345678);
  const fs::path wrong_type = input / "02WrongType.cas";
  WriteCreationPackage(wrong_type, "Wrong content type", kTitleId, 2);
  const fs::path malformed = input / "03NotAnStfs.car";
  {
    std::ofstream output(malformed, std::ios::binary);
    output << "not STFS";
  }

  const auto star_info = InspectCreationPackage(star);
  Check(star_info && star_info->kind == CreationKind::kSuperstar &&
            star_info->display_name == "01(TEST).A STAR" && star_info->slot == "00" &&
            star_info->title_id == kTitleId && !star_info->title_id_normalized,
        "superstar package metadata was not recognized");
  const auto entrance_info = InspectCreationPackage(legacy_entrance);
  Check(entrance_info && entrance_info->kind == CreationKind::kEntrance &&
            entrance_info->title_id == kLegacyTitleId && entrance_info->title_id_normalized,
        "legacy-title entrance did not request normalization");
  const auto save_info = InspectCreationPackage(full_save);
  Check(save_info && save_info->kind == CreationKind::kSave && save_info->slot == "—",
        "full-save package metadata was not recognized");
  Check(!InspectCreationPackage(wrong_title) && !InspectCreationPackage(wrong_type) &&
            !InspectCreationPackage(malformed),
        "unsupported or malformed package was accepted");
  Check(FindCreationPackages(input).size() == 6,
        "folder scan did not return the supported creation and save packages");

  Paths paths = MakePaths(root / "install");
  CancelFlag cancel{false};
  uint64_t done = 0;
  uint64_t total = 0;
  const Result imported = ImportCreations(paths, {star, entrance, arena, logos, legacy_entrance}, false,
      [&](uint64_t amount, uint64_t bytes, const std::string&) { done = amount; total = bytes; }, cancel);
  Check(imported.ok, "creation packages failed to import: " + imported.error);
  Check(done == total && total == kPayload.size() * 5,
        "creation import progress did not reach the total byte count");

  const fs::path account = paths.userdata_dir / "B13EBABEBABEBABE";
  const fs::path content = account / "545108B4" / "00000001";
  const fs::path headers = account / "545108B4" / "Headers" / "00000001";
  CheckPayload(content / "00CustomSuperStar.cas" / "SaveData.Dat");
  CheckPayload(content / "00EncodeDat.enc" / "SaveData.Dat");
  CheckPayload(content / "00CustomArena.car" / "SaveData.Dat");
  CheckPayload(content / "00PaintTool.pt" / "SaveData.Dat");
  CheckPayload(content / "35EncodeDat.enc" / "SaveData.Dat");
  Check(!fs::exists(account / "54510890"),
        "legacy entrance was not normalized into the WWE '13 content folder");
  const std::vector<uint8_t> normalized_header = ReadFile(headers / "35EncodeDat.enc.header");
  Check(Be32(normalized_header, 4) == kContentType && Be32(normalized_header, 0x140) == kTitleId,
        "legacy entrance sidecar did not receive WWE '13 title metadata");
  auto installed = ListInstalledCreations(paths);
  Check(installed.size() == 5 && Find(installed, "00CustomSuperStar.cas") &&
            Find(installed, "00EncodeDat.enc") && Find(installed, "00CustomArena.car") &&
            Find(installed, "00PaintTool.pt") && Find(installed, "35EncodeDat.enc"),
        "installed creation list is missing a package");
  Check(Find(installed, "35EncodeDat.enc")->display_name == "36(TEST).A LEGACY ENTRANCE",
        "installed creation name was not read from its content header");

  const Result duplicate = ImportCreations(paths, {star}, false, {}, cancel);
  Check(!duplicate.ok && duplicate.error.find("already exists") != std::string::npos,
        "duplicate creation import was not refused before replacement confirmation");
  const fs::path replacement = input / "00CustomSuperStar.cas";
  WriteCreationPackage(replacement, "01(TEST).REPLACED STAR");
  const Result replaced = ImportCreations(paths, {replacement}, true, {}, cancel);
  Check(replaced.ok, "confirmed creation replacement failed: " + replaced.error);
  Check(Find(ListInstalledCreations(paths), "00CustomSuperStar.cas")->display_name ==
            "01(TEST).REPLACED STAR",
        "replacement did not update the installed display name");
  Check(!ListBackups(paths).empty(), "creation changes did not make an automatic backup");

  // Loose creations (no full save imported yet) can be removed one at a time.
  Check(IsCreationRemovalSafe(paths, "00PaintTool.pt"), "loose creation was reported unsafe to remove");
  const Result loose_removed = RemoveCreation(paths, "00PaintTool.pt");
  Check(loose_removed.ok, "loose creation removal failed: " + loose_removed.error);
  Check(!fs::exists(content / "00PaintTool.pt") && !fs::exists(headers / "00PaintTool.pt.header"),
        "loose creation removal left data or its sidecar installed");
  const Result logos_back = ImportCreations(paths, {logos}, false, {}, cancel);
  Check(logos_back.ok, "re-import of the logos package failed: " + logos_back.error);

  const Result full_import = ImportFullSave(paths, full_save, {}, cancel);
  Check(full_import.ok, "full save import failed: " + full_import.error);
  CheckPayload(content / "SaveData.dat" / "SaveData.Dat");
  Check(Be32(ReadFile(headers / "SaveData.dat.header"), 0x140) == kTitleId,
        "full save did not install a WWE '13 header sidecar");
  installed = ListInstalledCreations(paths);
  Check(installed.size() == 6 && Find(installed, "SaveData.dat") &&
            Find(installed, "SaveData.dat")->kind == CreationKind::kSave,
        "installed creation list does not include the full save");

  // After a full save, the pack is save-indexed: single-item removal is refused.
  Check(!IsCreationRemovalSafe(paths, "00CustomArena.car"),
        "a save-indexed creation was reported safe to remove on its own");
  const Result refused = RemoveCreation(paths, "00CustomArena.car");
  Check(!refused.ok && refused.error.find("came in a pack together") != std::string::npos,
        "save-indexed single-item removal was not refused");

  // Pack-level removal removes the whole pack and restores the pre-import state.
  const std::vector<ContentPack> packs = ListContentPacks(paths);
  const ContentPack* arena_pack = nullptr;
  for (const auto& pack : packs) {
    if (std::find(pack.packages.begin(), pack.packages.end(), "00CustomArena.car") !=
        pack.packages.end()) {
      Check(pack.save_indexed, "the arena pack was not marked save-indexed");
      arena_pack = &pack;
      break;
    }
  }
  Check(arena_pack != nullptr, "no recorded pack contains the arena");
  const Result pack_removed = RemoveContentPack(paths, arena_pack->id);
  Check(pack_removed.ok, "pack removal failed: " + pack_removed.error);
  Check(ListInstalledCreations(paths).empty(), "pack removal did not restore the pre-import state");
  Check(!RemoveCreation(paths, "../SaveData.dat").ok && !RemoveCreation(paths, "SaveData.dat").ok,
        "unsafe or main-save removal was accepted");
  Check(!ImportFullSave(paths, legacy_entrance, {}, cancel).ok,
        "an entrance package was accepted as a full save");
}

// A pack imported after a baseline save has a real pre-import backup; removing it restores that backup
// (back to the baseline save, without the pack's creations) rather than removing files in place.
void TestPackRemovalRestore(const fs::path& root) {
  const fs::path input = root / "input";
  const fs::path star = input / "00CustomSuperStar.cas";
  const fs::path save = input / "SaveData.dat";
  WriteCreationPackage(star, "01(TEST).A STAR");
  WriteCreationPackage(save, "Baseline save");
  Paths paths = MakePaths(root / "restore");
  CancelFlag cancel{false};

  const Result baseline = ImportFullSave(paths, save, {}, cancel);
  Check(baseline.ok, "baseline full-save import failed: " + baseline.error);
  const Result added = ImportCreations(paths, {star}, false, {}, cancel);
  Check(added.ok, "creation import after baseline failed: " + added.error);
  Check(ListInstalledCreations(paths).size() == 2, "expected the save plus one creation installed");

  const std::vector<ContentPack> packs = ListContentPacks(paths);
  Check(packs.size() == 1 && packs.front().packages == std::vector<std::string>{"00CustomSuperStar.cas"} &&
            !packs.front().backup.empty(),
        "creation import did not record a pack with a real pre-import backup");
  Check(IsCreationRemovalSafe(paths, "00CustomSuperStar.cas"),
        "creation was unsafe to remove before a full save was imported");

  const Result indexed = ImportFullSave(paths, save, {}, cancel);
  Check(indexed.ok, "second full-save import failed: " + indexed.error);
  Check(!IsCreationRemovalSafe(paths, "00CustomSuperStar.cas"),
        "creation stayed removable after a full save indexed it");

  const Result removed = RemoveContentPack(paths, packs.front().id);
  Check(removed.ok, "pack removal with a real backup failed: " + removed.error);
  const auto after = ListInstalledCreations(paths);
  Check(after.size() == 1 && Find(after, "SaveData.dat") && !Find(after, "00CustomSuperStar.cas"),
        "pack removal did not restore the baseline save without the pack's creation");
  Check(ListContentPacks(paths).empty(), "pack removal did not drop the pack from the manifest");
}

// A pack name chosen at import time is stored in the manifest and read back; an old manifest recorded
// before names existed still reads (packages kept, name empty) so the UI can fall back.
void TestPackNameManifest(const fs::path& root) {
  const fs::path input = root / "input";
  const fs::path star = input / "00CustomSuperStar.cas";
  const fs::path arena = input / "01CustomArena.car";
  WriteCreationPackage(star, "01(TEST).A STAR");
  WriteCreationPackage(arena, "01(TEST).AN ARENA");
  Paths paths = MakePaths(root / "manifest");
  CancelFlag cancel{false};

  const Result named = ImportCreations(paths, {star}, false, {}, cancel, "Best of Save 13");
  Check(named.ok, "named pack import failed: " + named.error);
  std::vector<ContentPack> packs = ListContentPacks(paths);
  Check(packs.size() == 1 && packs.front().name == "Best of Save 13" &&
            packs.front().packages == std::vector<std::string>{"00CustomSuperStar.cas"},
        "the stored pack name did not survive the manifest round trip");

  // A tab/newline in a chosen name is folded to spaces so it cannot break the tab-separated manifest.
  const Result sanitized = ImportCreations(paths, {arena}, false, {}, cancel, " Pack\tName\n ");
  Check(sanitized.ok, "second pack import failed: " + sanitized.error);
  packs = ListContentPacks(paths);
  Check(packs.size() == 2 && packs[1].name == "Pack Name" &&
            packs[1].packages == std::vector<std::string>{"01CustomArena.car"},
        "a pack name with control characters was not sanitized");

  // An old manifest without a name field still reads: packages preserved, name empty.
  const fs::path manifest = paths.userdata_dir / "custom" / "content-packs.txt";
  Check(internal::WriteTextFileAtomic(
            manifest,
            std::string("20200101-000000-1\t2020-01-01T00:00:00\t1\toldbackup.zip\t00CustomSuperStar.cas\n")),
        "could not write the legacy manifest fixture");
  const std::vector<ContentPack> legacy = ListContentPacks(paths);
  Check(legacy.size() == 1 && legacy.front().name.empty() && legacy.front().save_indexed &&
            legacy.front().backup == "oldbackup.zip" &&
            legacy.front().packages == std::vector<std::string>{"00CustomSuperStar.cas"},
        "an old manifest without a name field was not read with the fallback behaviour");
}

Paths MakeRealPaths(const fs::path& userdata) {
  Paths paths;
  paths.userdata_dir = userdata;
  paths.exe_dir = userdata.parent_path();
  paths.backups_dir = paths.exe_dir / "backups";
  paths.settings_file = paths.exe_dir / "wwe13-enhanced.ini";
  return paths;
}

int InstallRealPack(const fs::path& pack_directory, const fs::path& userdata) {
  Paths paths = MakeRealPaths(userdata);
  std::vector<fs::path> creations;
  fs::path full_save;
  for (const auto& package : FindCreationPackages(pack_directory)) {
    const auto info = InspectCreationPackage(package);
    if (!info) continue;
    if (info->kind == CreationKind::kSave) full_save = package;
    else creations.push_back(package);
  }
  Check(creations.size() == 104 && !full_save.empty(),
        "the supplied pack should contain 104 creations plus one SaveData.dat package");
  CancelFlag cancel{false};
  uint64_t completed = 0;
  uint64_t total = 0;
  // Name the pack after the chosen folder (the parent of the per-slot data folder).
  std::string pack_name = pack_directory.parent_path().filename().string();
  if (pack_name.empty()) pack_name = pack_directory.filename().string();
  const Result added = ImportCreations(paths, creations, false,
      [&](uint64_t done, uint64_t size, const std::string&) { completed = done; total = size; }, cancel, pack_name);
  Check(added.ok, "core import of creation packages failed: " + added.error);
  Check(completed == total && total > 0, "core creation import progress did not finish");
  const Result save = ImportFullSave(paths, full_save, {}, cancel);
  Check(save.ok, "core import of the matching full save failed: " + save.error);
  const auto installed = ListInstalledCreations(paths);
  std::array<size_t, 5> counts{};
  for (const auto& item : installed) ++counts[static_cast<size_t>(item.kind)];
  Check(installed.size() == 105 && counts[static_cast<size_t>(CreationKind::kSuperstar)] == 50 &&
            counts[static_cast<size_t>(CreationKind::kEntrance)] == 50 &&
            counts[static_cast<size_t>(CreationKind::kArena)] == 3 &&
            counts[static_cast<size_t>(CreationKind::kLogos)] == 1 &&
            counts[static_cast<size_t>(CreationKind::kSave)] == 1,
        "core-installed pack has the wrong number or kind of items");
  Check(!fs::exists(userdata / "B13EBABEBABEBABE" / "54510890"),
        "normalized entrance packages were written outside the WWE '13 title folder");
  std::cout << "real_pack_install: creations=" << creations.size() << " total_installed=" << installed.size()
            << " backups=" << ListBackups(paths).size() << " userdata=" << userdata << '\n';
  for (const auto& item : installed) {
    std::cout << item.slot << " | " << item.display_name << " | " << static_cast<int>(item.kind)
              << " | " << item.package_name << '\n';
  }
  return 0;
}

int RemoveRealItem(const fs::path& userdata, const std::string& package_name) {
  Paths paths = MakeRealPaths(userdata);
  const Result removed = RemoveCreation(paths, package_name);
  Check(removed.ok, "core removal failed: " + removed.error);
  std::cout << "real_pack_remove: removed=" << package_name << " remaining="
            << ListInstalledCreations(paths).size() << " backups=" << ListBackups(paths).size() << '\n';
  return 0;
}

int ListRealItems(const fs::path& userdata) {
  Paths paths = MakeRealPaths(userdata);
  const auto installed = ListInstalledCreations(paths);
  for (const auto& item : installed) {
    std::cout << item.slot << " | " << item.display_name << " | " << static_cast<int>(item.kind)
              << " | " << item.package_name << '\n';
  }
  std::cout << "real_pack_list: installed=" << installed.size() << " backups=" << ListBackups(paths).size() << '\n';
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 4 && std::string(argv[1]) == "--install-pack") {
      return InstallRealPack(fs::path(argv[2]), fs::path(argv[3]));
    }
    if (argc == 4 && std::string(argv[1]) == "--remove") {
      return RemoveRealItem(fs::path(argv[2]), argv[3]);
    }
    if (argc == 3 && std::string(argv[1]) == "--list") {
      return ListRealItems(fs::path(argv[2]));
    }
    Check(argc == 1, "usage: creations_test [--install-pack PACK_DIR USERDATA_DIR | --remove USERDATA_DIR NAME | --list USERDATA_DIR]");
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("wwe13-creations-test-" + std::to_string(stamp));
    fs::create_directories(root);
    TestCreationImport(root);
    TestPackRemovalRestore(root);
    TestPackNameManifest(root);
    std::error_code error;
    fs::remove_all(root, error);
    Check(!error, "temporary creation fixture folder could not be removed");
    std::cout << "creations_test: PASS (inspect, title normalization, install, backup, replace, full save, "
                 "safe removal, pack removal, pack name manifest)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "creations_test: FAIL: " << error.what() << '\n';
    return 1;
  }
}
