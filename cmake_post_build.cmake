# cmake_post_build.cmake - embed manifest (all configs) + copy to bin/ (Release only)
# Called from POST_BUILD with -DCONFIG, -DSRC, -DMANIFEST

# Embed UAC manifest (always - both Debug and Release need admin)
message(STATUS "Embedding manifest into ${SRC}")
execute_process(
    COMMAND mt.exe -manifest ${MANIFEST} -outputresource:${SRC}
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "mt.exe failed with exit code ${result}")
endif()

# Copy to project bin/ (Release only)
if(NOT CONFIG STREQUAL "Release")
    message(STATUS "Skipping copy to bin/ (config=${CONFIG}, not Release)")
    return()
endif()

set(BIN_DIR "${CMAKE_CURRENT_LIST_DIR}/bin")
file(MAKE_DIRECTORY "${BIN_DIR}")
set(DST "${BIN_DIR}/TimeoutKill.exe")
message(STATUS "Copying ${SRC} -> ${DST}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SRC}" "${DST}"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "copy failed with exit code ${result}")
endif()
