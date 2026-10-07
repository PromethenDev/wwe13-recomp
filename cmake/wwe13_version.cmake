# Player-facing release version, shared by the game, the launcher and tools/package-release.sh.
# The single source is the VERSION file at the repository root (one line, e.g. "1.3.0"); bump it
# for every release. WWE13_VERSION_COMMIT is the short git hash of the tree being built, with a
# "+dirty" suffix for uncommitted changes, so two builds of the same version can still be told apart.
get_filename_component(_wwe13_version_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_wwe13_version_root}/VERSION")
file(STRINGS "${_wwe13_version_root}/VERSION" WWE13_VERSION LIMIT_COUNT 1)
string(STRIP "${WWE13_VERSION}" WWE13_VERSION)
if(NOT WWE13_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.]+)?$")
  message(FATAL_ERROR "VERSION must look like 1.3.0 or 1.3.0-rc1, got '${WWE13_VERSION}'")
endif()

set(WWE13_VERSION_COMMIT unknown)
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
    WORKING_DIRECTORY "${_wwe13_version_root}"
    OUTPUT_VARIABLE _wwe13_version_hash OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _wwe13_version_result ERROR_QUIET)
  if(_wwe13_version_result EQUAL 0)
    set(WWE13_VERSION_COMMIT "${_wwe13_version_hash}")
    # Re-run configure after every commit/checkout (the reflog) or staging change (the index), so
    # an incremental build never shows a stale hash.
    foreach(_wwe13_git_file logs/HEAD index)
      execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --path-format=absolute --git-path "${_wwe13_git_file}"
        WORKING_DIRECTORY "${_wwe13_version_root}"
        OUTPUT_VARIABLE _wwe13_git_path OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      if(EXISTS "${_wwe13_git_path}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_wwe13_git_path}")
      endif()
    endforeach()
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
      WORKING_DIRECTORY "${_wwe13_version_root}"
      OUTPUT_VARIABLE _wwe13_version_status OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT _wwe13_version_status STREQUAL "")
      string(APPEND WWE13_VERSION_COMMIT "+dirty")
    endif()
  endif()
endif()
