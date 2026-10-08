// WWE '13 launcher core: everything the launcher does that is not drawing. No UI, no SDL video.
// Shared contract between the core implementation and the SDL3 + Dear ImGui front end (launcher2/ui).
// Windows and Linux, UTF-8 strings everywhere (std::filesystem::path for paths).
//
// Long operations (scan, ISO extract, package import, backup) run on a worker thread owned by the caller;
// they report through a ProgressFn and honour cancellation. They never throw: errors come back as
// Result::error with a player-readable message (no emulator/debug wording).
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wwe13::launcher {

namespace fs = std::filesystem;

struct Result {
  bool ok = true;
  std::string error;  // player-readable, empty when ok
  static Result Ok() { return {}; }
  static Result Fail(std::string message) { return {false, std::move(message)}; }
};

// done/total in bytes (or items when bytes are unknown); label = what is happening now ("Extracting pac/ch/ch82.pac").
using ProgressFn = std::function<void(uint64_t done, uint64_t total, const std::string& label)>;
using CancelFlag = std::atomic<bool>;

// ---------------------------------------------------------------------------------------------- paths
// Where things live. Portable layout: everything next to the launcher (exe_dir). Same rules on both OSes.
struct Paths {
  fs::path exe_dir;        // folder of the launcher executable
  fs::path game_exe;       // wwe13.exe (Windows) / wwe13 (Linux) next to the launcher
  fs::path settings_file;  // wwe13-enhanced.ini (existing schema-7 file is read and kept compatible)
  fs::path userdata_dir;   // exe_dir/userdata (saves under B13EBABEBABEBABE/545108B4, DLC under 0000000000000000/545108B4/00000002)
  fs::path music_dir;      // exe_dir/music (the game reads custom songs from here)
  fs::path logs_dir;       // exe_dir/logs
  fs::path backups_dir;    // exe_dir/backups
  fs::path default_game_folder;  // exe_dir/"WWE 13"
};
Paths ResolvePaths(const fs::path& launcher_executable);

// ---------------------------------------------------------------------------------------------- settings
enum class Resolution { k480p, k576p, k720p, k1080p, k1440p };
enum class FrameRate { kClassic, kKeep60, kLock30 };
enum class AntiAliasing { kOriginal4x, kFaster2x };
enum class DisplayMode { kFullscreen, kWindowed };

struct Settings {
  Resolution resolution = Resolution::k720p;
  FrameRate frame_rate = FrameRate::kClassic;
  AntiAliasing anti_aliasing = AntiAliasing::kOriginal4x;
  DisplayMode display = DisplayMode::kFullscreen;
  std::string gpu_id;    // stable id of the chosen Vulkan device ("" = recommended)
  fs::path game_folder;  // folder holding default.xex + default.xexp
  bool auto_start = false;          // "Start straight away next time" (hold Shift at start to come back)
  bool auto_backup = true;          // back up saves before each launch, keep last 5
  bool stretch_to_fill = false;     // stretch the 16:9 picture over wider screens (21:9) instead of black bars
  bool sync_to_display = true;      // pace frames on the display's vblank (WWE13_HOST_VSYNC); off = steady timer
  bool check_updates = true;        // look for a newer release when the launcher starts
  std::string skipped_update_version;  // "Skip this version" choice ("" = none)
  bool explicit_choice = false;     // false = follow RecommendedSettings()
};
// Reads the existing wwe13-enhanced.ini (UTF-16LE with BOM written by the old launcher, or UTF-8) including
// its schema-7 keys and legacy migrations; unknown keys are preserved on save.
Settings LoadSettings(const Paths& paths);
Result SaveSettings(const Paths& paths, const Settings& settings);

// ---------------------------------------------------------------------------------------------- PC info
struct GpuInfo {
  std::string id;          // stable across runs (vendor:device[:index])
  std::string name;        // "AMD Radeon 780M Graphics"
  bool integrated = false;
  int vulkan_index = -1;   // what the game's --vulkan_device expects
};
std::vector<GpuInfo> ListGpus();  // Vulkan enumeration (volk), CPU/llvmpipe devices excluded
// The defaults shown as "Recommended for this PC": integrated/handheld -> 480p, 30 locked, Original AA;
// discrete -> 720p, Classic, Original AA. Returns the reason line too ("Radeon 780M - integrated graphics").
struct Recommendation {
  Settings settings;
  std::string reason;
};
Recommendation RecommendedSettings(const std::vector<GpuInfo>& gpus);

// ---------------------------------------------------------------------------------------------- game files (F1)
enum class FileState { kMissing, kFound, kWrongVersion, kCorrupt };
struct GameFilesStatus {
  FileState base_game = FileState::kMissing;     // default.xex, title ID 545108B4
  FileState title_update = FileState::kMissing;  // default.xexp, TU 2.0.1.0 matching the base
  int dlc_installed = 0;                         // packs found under userdata (0..3)
  int dlc_known = 3;
  fs::path game_folder;
  std::string detail;  // one line per item for the UI
  bool ready() const { return base_game == FileState::kFound && title_update == FileState::kFound; }
};
// Checks one folder (same title-ID / TU checks as the game's src/game_version_check.cpp).
GameFilesStatus CheckGameFolder(const fs::path& folder, const Paths& paths);
// Searches: next to the launcher, previous choice, all drives / home folders (bounded depth), Xenia and
// Xenia Canary content folders. Returns every candidate found, best first.
struct FoundGame {
  fs::path folder;
  GameFilesStatus status;
};
// found_packages (optional): WWE '13 title-update / DLC packages seen during the same walk (package files
// and Xenia content folders), so the launcher can offer to install them. Search order puts the places
// players keep these files (launcher folder and its parent, Downloads/Desktop/Documents, the game
// folder's parent, drive roots one level) ahead of the deeper walk; only files matching a known TU/DLC
// name, a known package size, or an Xbox content path are opened at all.
std::vector<FoundGame> FindGameFiles(const Paths& paths, const ProgressFn& progress, const CancelFlag& cancel,
                                     std::vector<fs::path>* found_packages = nullptr);
// Test-only: how many candidate package files FindGameFiles opened (header read) during its most recent
// call. Reset at the start of every call so a test can prove unrelated files are never touched.
uint64_t FindGameFilesHeaderReadCount();

// ---------------------------------------------------------------------------------------------- disc image (F2)
// Player's own Xbox 360 disc image (XDVDFS / XISO, including Redump images with a video partition offset).
// Verifies it is WWE '13 (default.xex title ID) before extracting into dest_folder.
Result ExtractDiscImage(const fs::path& iso, const fs::path& dest_folder, const ProgressFn& progress,
                        const CancelFlag& cancel);

// ---------------------------------------------------------------------------------------------- TU / DLC (F3)
// STFS CON/LIVE/PIRS packages (the title update package and the DLC packs) or Xenia content folders.
// Title update: installs default.xexp (+ its data files) into the game folder. DLC: installs into
// userdata/0000000000000000/545108B4/00000002/<name> with its Headers entry, like the game expects.
enum class PackageKind { kTitleUpdate, kDlc, kUnknown };
struct PackageInfo {
  PackageKind kind = PackageKind::kUnknown;
  std::string display_name;  // from the package header
  uint32_t title_id = 0;
};
std::optional<PackageInfo> InspectPackage(const fs::path& package_or_folder);
// Header-only variant: checks the STFS magic, content type (0x344) and title ID (0x360) without parsing
// the file list. "Find Automatically" uses this to verify a candidate cheaply; InspectPackage still runs
// the full check at install time.
std::optional<PackageInfo> InspectPackageHeader(const fs::path& package);
// Installable files a player dropped into a game folder (top level only): disc images (.iso/.img) and WWE '13
// title-update / DLC package files. Never throws; unreadable entries are skipped.
struct FolderInstallables {
  std::vector<fs::path> disc_images;
  std::vector<fs::path> packages;
  bool has_title_update = false;
  bool from_search = false;  // packages found by "Find Automatically" anywhere on the PC
};
FolderInstallables FindInstallableFiles(const fs::path& folder);
Result ImportPackage(const fs::path& package_or_folder, const Paths& paths, const fs::path& game_folder,
                     const ProgressFn& progress, const CancelFlag& cancel);

// ---------------------------------------------------------------------------------------------- custom creations / saves
// WWE '13 creator packages are saved-game-content STFS containers with one SaveData.Dat payload. They live beside
// the user's main save under B13EBABEBABEBABE/545108B4/00000001 and have a matching Headers sidecar.
enum class CreationKind { kSuperstar, kEntrance, kArena, kLogos, kSave };
struct CreationInfo {
  CreationKind kind = CreationKind::kSuperstar;
  std::string display_name;  // in-game name from the STFS display-name field
  std::string package_name;  // package filename, also the installed content-folder name
  std::string slot;          // two-digit filename slot, or "—" for a full save
  std::string when;          // ISO 8601 install time (content folder mtime); empty when unknown
  uint32_t title_id = 0;     // source STFS title ID; 54510890 entrances normalize on import
  bool title_id_normalized = false;
};
std::optional<CreationInfo> InspectCreationPackage(const fs::path& package);
// Finds supported creation/save STFS files directly inside a chosen pack folder.
std::vector<fs::path> FindCreationPackages(const fs::path& folder);
std::vector<CreationInfo> ListInstalledCreations(const Paths& paths);
// Backs up the current userdata account once before any change. Replacements are permitted only when explicitly
// confirmed by the caller; content from a 54510890 entrance package is installed in the 545108B4 runtime folder.
// pack_name names the import batch (the chosen folder's or file's name); it is recorded in the pack manifest
// and shown in the Content Packs list. Empty keeps the old behaviour (the UI derives a name from the first item).
Result ImportCreations(const Paths& paths, const std::vector<fs::path>& packages, bool replace_existing,
                       const ProgressFn& progress, const CancelFlag& cancel,
                       std::string pack_name = {});
Result RemoveCreation(const Paths& paths, std::string_view package_name);
// Replaces only the main SaveData.Dat package contents and its metadata; the caller is responsible for the clear
// replacement confirmation. An automatic save-folder backup is always made first.
Result ImportFullSave(const Paths& paths, const fs::path& package, const ProgressFn& progress,
                      const CancelFlag& cancel);

// ---------------------------------------------------------------------------------------------- content packs (safe removal)
// A pack records what was imported together and the automatic backup taken before that import, so the whole pack can
// be removed later by restoring that backup. A full save indexes the creation slots, so once a full save is imported
// the pack becomes "save-indexed" and its items must be removed as a pack rather than one at a time.
struct ContentPack {
  std::string id;                    // stable id (import timestamp)
  std::string name;                  // chosen folder/file name at import time; empty on old manifests
  std::vector<std::string> packages; // installed package names in this pack (creations only, no full save)
  bool save_indexed = false;         // a full save was imported after these packages were installed
  std::string backup;                // backups/ basename taken before this pack's import (empty when none)
  std::string when;                  // ISO 8601 import time
};
std::vector<ContentPack> ListContentPacks(const Paths& paths);
// Removes the whole pack: restores the automatic backup taken before the pack import (which also removes its items),
// then drops the pack from the manifest. Fails clearly when the backup is no longer available.
Result RemoveContentPack(const Paths& paths, const std::string& pack_id);
// A single creation may be removed on its own only when it is not referenced by a save-indexed pack.
bool IsCreationRemovalSafe(const Paths& paths, std::string_view package_name);

// ---------------------------------------------------------------------------------------------- entrance videos
// The game's replaceable titantron movies live in the game folder under movies/titantron (numeric stems, .bik2).
// A player assigns a video to a titantron; the launcher copies it to
// userdata/custom/entrance-videos/<titantron stem>.<original extension>.
struct TitantronMovie {
  std::string stem;    // "050" (the titantron file stem; also the assignment key)
  fs::path path;       // movies/titantron/050.bik2 (empty when the game folder is not set)
};
struct EntranceVideo {
  std::string stem;    // titantron stem this video replaces
  fs::path path;       // userdata/custom/entrance-videos/<stem>.<ext>
  std::string source;  // original file name the player assigned (basename)
};
// Common video extensions a player can assign to a titantron.
const std::vector<std::string>& SupportedVideoExtensions();
bool IsSupportedVideoFile(const fs::path& file);
std::vector<TitantronMovie> ListTitantronMovies(const fs::path& game_folder);
std::vector<EntranceVideo> ListEntranceVideos(const Paths& paths);
// Copies the video into the entrance-videos folder keyed by the titantron stem (replacing any existing assignment).
Result AssignEntranceVideo(const Paths& paths, std::string_view titantron_stem, const fs::path& video_file);
Result RemoveEntranceVideo(const Paths& paths, std::string_view titantron_stem);

// ---------------------------------------------------------------------------------------------- saves (F5)
struct BackupInfo {
  fs::path file;        // backups/wwe13-saves-YYYYMMDD-HHMMSS[-auto].zip
  std::string when;     // local time, "Today, 9:41 AM" style is the UI's job; this is ISO 8601
  uint64_t bytes = 0;
  bool automatic = false;
};
std::vector<BackupInfo> ListBackups(const Paths& paths);
// automatic: keep the newest 5. include_creations=false (the pre-launch backup) skips imported creation
// packages and marks the zip "saves only"; restoring such a zip keeps the installed creations.
Result BackupSaves(const Paths& paths, bool automatic, BackupInfo* created, bool include_creations = true);
Result RestoreBackup(const Paths& paths, const fs::path& backup_zip);        // backs up the current saves first
Result ImportBackup(const Paths& paths, const fs::path& external_zip);       // copies into backups/ after checking it

// ---------------------------------------------------------------------------------------------- music
struct Song {
  fs::path file;
  std::string title;     // file name without extension
  double seconds = 0;    // 0 when unknown
  std::string format;    // "MP3" / "FLAC" / "WAV" / "OGG"
};
std::vector<Song> ListSongs(const Paths& paths);
// Copies dropped files into music_dir (skips unsupported types with a message; Unicode names are fine).
Result AddSongs(const Paths& paths, const std::vector<fs::path>& files, std::vector<std::string>* skipped);
Result RemoveSong(const Paths& paths, const fs::path& song_file);

// ---------------------------------------------------------------------------------------------- launch
// The exact command line and environment the current launcher uses (launcher/main.cpp BuildArguments /
// BuildEnvironmentOverrides), from Settings: game folder, --fullscreen, --user_data_root, --log_file,
// --vulkan_device, --resolution / --resolution_scale, WWE13_INTERNAL_RES, WWE13_KEEP_60 / WWE13_LOCK_30,
// WWE13_SCENE_AA, WWE13_F24_PIXEL_RATE=1 ...
struct LaunchPlan {
  fs::path executable;
  std::vector<std::string> arguments;
  std::vector<std::pair<std::string, std::string>> environment;  // overrides on top of the launcher's env
  fs::path working_directory;
};
LaunchPlan BuildLaunchPlan(const Paths& paths, const Settings& settings, const std::vector<GpuInfo>& gpus);
// Detached start. With watch_seconds > 0 it also waits that long and fails with kGameClosedEarlyMessage
// when the game process has already exited (e.g. an antivirus block, a missing file or a driver without
// Vulkan), so the launcher can stay open and tell the player instead of silently disappearing.
inline constexpr const char* kGameClosedEarlyMessage =
    "WWE '13 closed right after starting. Press \"Save a Bug Report (logs + settings)\" at the bottom "
    "of the launcher and attach the zip to your report on GitHub.";
Result StartGame(const LaunchPlan& plan, double watch_seconds = 0.0);
// True when a game process currently holds the single-instance lock for this user-data folder (the same
// FNV-1a key and lock the game's src/single_instance.cpp uses). The launcher refuses Play and updates while
// it is true.
bool GameAlreadyRunning(const fs::path& user_data_root);

// ---------------------------------------------------------------------------------------------- keyboard controls
// The game's keyboard keys: keybind_* in wwe13.toml beside the game executable (read by the game at start).
// Key names are ReXGlue's (src/ui/keybinds.cpp): "A", "Space", "Shift", "Up", "Numpad5", ...
struct KeyBinding {
  const char* label;        // controller button it plays as ("Right trigger (RT)")
  const char* cvar;         // keybind_right_trigger
  const char* default_key;  // the game's own default ("Control")
};
struct KeyboardControls {
  std::vector<std::string> keys;  // one per KeyBindings() row; "" = no key
  std::vector<bool> present;      // the row was set in wwe13.toml
  std::vector<bool> modified;     // changed in the launcher since loading
};
const std::vector<KeyBinding>& KeyBindings();  // 24 rows, same order as the previous launcher
bool IsSupportedKeyName(std::string_view name);
KeyboardControls DefaultKeyboardControls();
KeyboardControls LoadKeyboardControls(const Paths& paths);  // never fails: unreadable file -> defaults
std::vector<bool> DuplicateKeyBindings(const KeyboardControls& controls);  // true = shares its key with another row
// Adds the launcher's default key for every keybind_* row the file is missing (first start, older files); keeps rows
// that are present. Does nothing when a merge would create a duplicate key.
Result EnsureKeyboardDefaults(const Paths& paths);
// Rewrites only the keybind_* lines (other settings in the file are kept byte for byte); refuses duplicates.
Result SaveKeyboardControls(const Paths& paths, const KeyboardControls& controls);

// ---------------------------------------------------------------------------------------------- bug report
// Zips the newest game logs, wwe13-enhanced.ini, GPU list and a short system summary into exe_dir.
Result SaveBugReport(const Paths& paths, fs::path* created_zip);

}  // namespace wwe13::launcher
