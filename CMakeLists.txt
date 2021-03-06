project("ims-json")

cmake_minimum_required(VERSION 3.10.0)

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin )
set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/lib )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/lib )

set(libsrc src/json.c)
include_directories(src)

add_library(ims-json-static STATIC ${libsrc})
add_library(ims-json-shared SHARED ${libsrc})
add_executable( ims-json-cli jsonc/main.c)

target_link_libraries( ims-json-cli ims-json-static )

SET_TARGET_PROPERTIES(ims-json-static PROPERTIES OUTPUT_NAME ims-json CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(ims-json-shared PROPERTIES OUTPUT_NAME ims-json CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(ims-json-cli PROPERTIES OUTPUT_NAME ims-json CLEAN_DIRECT_OUTPUT 1)

install(TARGETS ims-json-shared ims-json-static ims-json-cli
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

INSTALL ( DIRECTORY ${CMAKE_SOURCE_DIR}/src/
    DESTINATION include/ims-json
    FILES_MATCHING PATTERN "*.h*"
)
