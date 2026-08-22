# Compute DOGGY_VERSION = <base>-<YYYY-MM-DD-HHMM>-<short-git-hash>
# Optional inputs: DOGGY_VERSION_BASE, DOGGY_GIT_ROOT, DOGGY_VERSION_HEADER

if(NOT DEFINED DOGGY_VERSION_BASE)
    set(DOGGY_VERSION_BASE "1.0.0")
endif()

if(NOT DEFINED DOGGY_GIT_ROOT OR DOGGY_GIT_ROOT STREQUAL "")
    if(DEFINED CMAKE_SOURCE_DIR AND NOT CMAKE_SOURCE_DIR STREQUAL "")
        set(DOGGY_GIT_ROOT "${CMAKE_SOURCE_DIR}")
    else()
        get_filename_component(DOGGY_GIT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    endif()
endif()

string(TIMESTAMP DOGGY_BUILD_STAMP "%Y-%m-%d-%H%M")

set(DOGGY_GIT_HASH "unknown")
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${DOGGY_GIT_ROOT}"
    OUTPUT_VARIABLE _doggy_git_hash
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _doggy_git_rc
)
if(_doggy_git_rc EQUAL 0 AND NOT _doggy_git_hash STREQUAL "")
    set(DOGGY_GIT_HASH "${_doggy_git_hash}")
endif()

set(DOGGY_VERSION "${DOGGY_VERSION_BASE}-${DOGGY_BUILD_STAMP}-${DOGGY_GIT_HASH}")

if(DEFINED DOGGY_VERSION_HEADER AND NOT DOGGY_VERSION_HEADER STREQUAL "")
    get_filename_component(_doggy_hdr_dir "${DOGGY_VERSION_HEADER}" DIRECTORY)
    file(MAKE_DIRECTORY "${_doggy_hdr_dir}")
    file(WRITE "${DOGGY_VERSION_HEADER}"
        "#ifndef DOGGY_VERSION_H\n#define DOGGY_VERSION_H\n\n#define DOGGY_VERSION \"${DOGGY_VERSION}\"\n\n#endif\n"
    )
endif()
