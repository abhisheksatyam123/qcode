# Pre-compress text WebUI assets next to themselves ("app.js" -> "app.js.gz").
# The server sends the .gz with Content-Encoding: gzip when the client accepts
# it, so no response is compressed per request.
#
# Usage: cmake -DWEBUI_DIR=<dir> -P compress_webui.cmake
if(NOT WEBUI_DIR)
  message(FATAL_ERROR "WEBUI_DIR is required")
endif()

file(GLOB_RECURSE _qcode_webui_files
  LIST_DIRECTORIES false
  "${WEBUI_DIR}/*.js" "${WEBUI_DIR}/*.css" "${WEBUI_DIR}/*.html"
  "${WEBUI_DIR}/*.svg" "${WEBUI_DIR}/*.json" "${WEBUI_DIR}/*.ttf")

foreach(_file IN LISTS _qcode_webui_files)
  # Remove a stale .gz so a source file that changed is never served old bytes.
  file(REMOVE "${_file}.gz")
  execute_process(
    COMMAND gzip -9 -n -c "${_file}"
    OUTPUT_FILE "${_file}.gz"
    RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "gzip failed for ${_file} (exit ${_rc})")
  endif()
endforeach()
