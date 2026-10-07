// Single-instance guard for the game (GitHub #6).
//
// A player who starts the game twice (for example by double-clicking the
// launcher again while the first start-up is still preparing shaders on a slow
// machine) ends up with two game processes fighting over the same GPU, the same
// user data and the same save. The guard lets only the first process that uses
// a given user-data folder run; a second one exits with a clear message.
//
// The lock is scoped to the user-data folder, so the project's parallel test
// lanes (each with its own --user_data_root) and two separate installations
// still run side by side. It is released automatically when the process exits.

#pragma once

#include <filesystem>
#include <string>

namespace wwe13 {

// Returns true when this process now holds the single-instance lock for
// `user_data_root` (or when locking is not applicable). Returns false when
// another running process already holds it. The lock is held until the process
// exits.
bool AcquireUserDataInstanceLock(const std::filesystem::path& user_data_root);

// True when another process currently holds the lock for `user_data_root`.
// Does not take the lock itself (used by the launcher to warn before starting).
bool AnotherUserDataInstanceRunning(const std::filesystem::path& user_data_root);

}  // namespace wwe13
