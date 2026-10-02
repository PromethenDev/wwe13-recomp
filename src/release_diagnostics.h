#pragma once

#include <filesystem>

#include "game_version_check.h"

namespace wwe13 {

void InitializeReleaseDiagnostics(const std::filesystem::path& game_folder,
                                  GameFileCheckResult game_file_result);
void FlushReleaseLogs();

}  // namespace wwe13
