# GenerateBuildInfo.cmake
# Arguments required:
# - SOURCE_DIR: Path to the root source directory
# - OUTPUT_FILE: Path to the output header file (GeneratedBuildInfo.h)
# - PROJECT_VERSION: The version string of the project

file(GLOB_RECURSE SOURCE_FILES "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h")
set(LATEST_TIME 0)
set(LATEST_FILE "")

foreach(F IN LISTS SOURCE_FILES)
    file(TIMESTAMP "${F}" FILE_TIME "%s")
    if(FILE_TIME GREATER LATEST_TIME)
        set(LATEST_TIME ${FILE_TIME})
        set(LATEST_FILE "${F}")
    endif()
endforeach()

if(LATEST_FILE)
    file(TIMESTAMP "${LATEST_FILE}" FORMATTED_TIME "%H:%M:%S")
else()
    set(FORMATTED_TIME "Unknown")
endif()

find_package(Git QUIET)
set(GIT_INFO "")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_HASH)
        set(GIT_INFO "Git: ${GIT_HASH} | ")
    endif()
endif()

set(CONTENT "#pragma once\n#define DYNAMIC_BUILD_IDENTITY \"Build: v${PROJECT_VERSION} | ${GIT_INFO}Last Edit: ${FORMATTED_TIME}\"\n")

file(WRITE "${OUTPUT_FILE}.tmp" "${CONTENT}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUTPUT_FILE}.tmp" "${OUTPUT_FILE}")
