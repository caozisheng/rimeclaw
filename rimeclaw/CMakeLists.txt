project(librimeclaw)

file(GLOB_RECURSE RIMECLAW_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

file(GLOB_RECURSE RIMECLAW_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(librimeclaw STATIC ${RIMECLAW_SOURCES} ${RIMECLAW_HEADERS})

target_include_directories(librimeclaw PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_definitions(librimeclaw PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _WINDOWS
    )
endif()

# nlohmann::json binary size optimization
target_compile_definitions(librimeclaw PUBLIC
    JSON_NO_IO
    JSON_DISABLE_ENUM_SERIALIZATION=1
    NLOHMANN_JSON_NAMESPACE_NO_VERSION=1
)

target_link_libraries(librimeclaw PUBLIC
    CURL::libcurl
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    Threads::Threads
)

if(BUILD_LLAMA_LOCAL_PROVIDER)
    target_link_libraries(librimeclaw PUBLIC llama)
    target_compile_definitions(librimeclaw PUBLIC BUILD_LLAMA_LOCAL_PROVIDER=1)
endif()

set_target_properties(librimeclaw PROPERTIES
    OUTPUT_NAME "rimeclaw"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
