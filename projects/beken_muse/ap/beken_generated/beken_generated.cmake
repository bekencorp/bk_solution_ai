# beken_generated.cmake

# Get the directory where this file is located (i.e., the beken_generated directory)
get_filename_component(BEKEN_GENERATED_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)

# Recursively find all .c source files
# NOTE: plain GLOB expands at CONFIGURE time only — after ADDING a new .c
# under beken_generated, force cmake to re-run (touch any .cmake or wipe
# the build dir), otherwise the file is silently never compiled and the
# link fails with "undefined reference". CONFIGURE_DEPENDS cannot fix it:
# this file is also included in cmake -P script mode by
# component_get_requirements, where CONFIGURE_DEPENDS is rejected.
file(GLOB_RECURSE BEKEN_GENERATOR_SOURCES
    "${BEKEN_GENERATED_DIR}/*.c"
)

# beken_muse trim: the generated pages whose demo backends are not built
# (edge-AI, Agora chat/vision, music, robot video, ASR, DOA, BT music,
# Beken smart-config provisioning) are DELETED from this tree; no source
# filtering needed here. page_hooks.c / ui_nav_router.c / ui_i18n.c keep
# only the matching live symbols.

# Recursively find all .h header files and collect their directories (excluding root directory)
file(GLOB_RECURSE BEKEN_HEADER_FILES "${BEKEN_GENERATED_DIR}/*.h")
get_filename_component(BEKEN_GENERATED_ROOT "${BEKEN_GENERATED_DIR}" ABSOLUTE)
set(BEKEN_GENERATOR_INCLUDE_DIRS "${BEKEN_GENERATED_ROOT}")
foreach(header_file ${BEKEN_HEADER_FILES})
    get_filename_component(header_dir ${header_file} DIRECTORY)
    get_filename_component(header_dir_abs ${header_dir} ABSOLUTE)
    # Only add subdirectories (not root directory)
    if(NOT "${header_dir_abs}" STREQUAL "${BEKEN_GENERATED_ROOT}")
        list(APPEND BEKEN_GENERATOR_INCLUDE_DIRS ${header_dir_abs})
    endif()
endforeach()

# Remove duplicates
if(BEKEN_GENERATOR_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES BEKEN_GENERATOR_INCLUDE_DIRS)
endif()

# Output debug information
list(LENGTH BEKEN_GENERATOR_SOURCES SOURCE_COUNT)
message(STATUS "Beken Generated: Found ${SOURCE_COUNT} source files")
message(STATUS "Beken Generated: Include directories: ${BEKEN_GENERATOR_INCLUDE_DIRS}")
