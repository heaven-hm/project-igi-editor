# fetch_igi1conv.cmake
#
# Best-effort: before building the editor, pull the latest released igi1conv.exe
# from the standalone converter repo (jones-hm/project-igi-conv) and refresh the
# bundled exe at assets/editor/tools/igi1conv/igi1conv.exe.
#
# igi1conv ships as a Qt application; the full folder (exe + Qt DLLs) is committed
# under assets/editor/tools/igi1conv/ and copied to bin/<cfg>/editor/tools/igi1conv/
# by the POST_BUILD step. This script only refreshes the exe itself — the Qt
# runtime DLLs stay as committed. Falls back to the committed binary when GitHub
# is unreachable or the release asset is not yet published.

set(_igi1conv_dst "${CMAKE_CURRENT_SOURCE_DIR}/assets/editor/tools/igi1conv/igi1conv.exe")
set(_igi1conv_version "v1.6.0")
set(_igi1conv_sha256 "c79da40b239291e46be488e21012f80f423515b8db72d838a036e29e0269198f")
set(_igi1conv_url "https://github.com/jones-hm/project-igi-conv/releases/download/${_igi1conv_version}/igi1conv.exe")
set(_igi1conv_tmp "${CMAKE_CURRENT_BINARY_DIR}/igi1conv_latest.exe")

if(DEFINED ENV{IGI1CONV_NO_FETCH})
    message(STATUS "igi1conv: IGI1CONV_NO_FETCH set, keeping committed binary")
    return()
endif()

message(STATUS "igi1conv: downloading pinned ${_igi1conv_version} release from ${_igi1conv_url}")
file(DOWNLOAD "${_igi1conv_url}" "${_igi1conv_tmp}"
     TIMEOUT 30
     INACTIVITY_TIMEOUT 15
     STATUS _igi1conv_status)

list(GET _igi1conv_status 0 _igi1conv_code)
if(_igi1conv_code EQUAL 0 AND EXISTS "${_igi1conv_tmp}")
    file(SIZE "${_igi1conv_tmp}" _igi1conv_size)
    # A real PE binary is well over 100 KB; a 404 page or redirect stub is tiny.
    if(_igi1conv_size GREATER 100000)
        file(SHA256 "${_igi1conv_tmp}" _igi1conv_actual_sha256)
        string(TOLOWER "${_igi1conv_actual_sha256}" _igi1conv_actual_sha256)
        if(NOT _igi1conv_actual_sha256 STREQUAL _igi1conv_sha256)
            message(STATUS "igi1conv: checksum mismatch (got ${_igi1conv_actual_sha256}); keeping committed binary")
            file(REMOVE "${_igi1conv_tmp}")
            return()
        endif()
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_igi1conv_tmp}" "${_igi1conv_dst}")
        message(STATUS "igi1conv: refreshed bundled binary from ${_igi1conv_version} (${_igi1conv_size} bytes)")
    else()
        message(STATUS "igi1conv: download too small (${_igi1conv_size} bytes); keeping committed binary")
    endif()
    file(REMOVE "${_igi1conv_tmp}")
else()
    list(GET _igi1conv_status 1 _igi1conv_msg)
    message(STATUS "igi1conv: latest release unavailable (${_igi1conv_msg}); keeping committed binary")
endif()
