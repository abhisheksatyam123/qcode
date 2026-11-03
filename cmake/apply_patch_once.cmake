# Applies a git patch to the current directory unless it is already applied.
# FetchContent runs PATCH_COMMAND again when a build tree is reconfigured, and a
# plain `git apply` fails on the second run because the change is in place.
#
# Usage: cmake -DPATCH_FILE=<patch> -P apply_patch_once.cmake
if(NOT PATCH_FILE)
  message(FATAL_ERROR "PATCH_FILE is required")
endif()

execute_process(COMMAND git apply --reverse --check "${PATCH_FILE}"
  RESULT_VARIABLE _already_applied OUTPUT_QUIET ERROR_QUIET)
if(_already_applied EQUAL 0)
  message(STATUS "Patch already applied: ${PATCH_FILE}")
  return()
endif()

execute_process(COMMAND git apply --whitespace=nowarn "${PATCH_FILE}"
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Failed to apply ${PATCH_FILE}")
endif()
