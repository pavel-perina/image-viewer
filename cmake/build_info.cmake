# Writes the header that says which build this is: the commit, "-dirty" when the tree has
# changes not committed, and when it was built. Run at every build, as `cmake -P`, because
# neither changes at configure time. The header is replaced only if its text differs, so a
# build that changes nothing leaves nothing to recompile.
#
# In: SOURCE_DIR (the git checkout), OUTPUT (the header to write).
find_package(Git QUIET)
set(commit "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${SOURCE_DIR}
        OUTPUT_VARIABLE described
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE status)
    if(status EQUAL 0 AND NOT described STREQUAL "")
        set(commit "${described}")
    endif()
endif()
string(TIMESTAMP built "%Y-%m-%d %H:%M UTC" UTC)

set(text "#pragma once\n#define IV_BUILD_COMMIT \"${commit}\"\n#define IV_BUILD_TIME \"${built}\"\n")
set(old "")
if(EXISTS ${OUTPUT})
    file(READ ${OUTPUT} old)
endif()
# Within the same minute the text is the same, so a rebuild then leaves nothing to compile.
if(NOT old STREQUAL text)
    file(WRITE ${OUTPUT} "${text}")
endif()
