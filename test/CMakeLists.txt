project(test)

file(GLOB TEST_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

file(GLOB TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(test ${TEST_SOURCES} ${TEST_HEADERS})

# include src code headers
target_include_directories(test PRIVATE
    ${CMAKE_SOURCE_DIR}
)

if(MSVC)
    target_compile_definitions(test PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Link src code compiled static libraries
target_link_libraries(test PRIVATE
    librimeclaw
)

# Link vcpkg installed libraries
target_link_libraries(test PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

set_target_properties(test PROPERTIES
    OUTPUT_NAME "test"
)

foreach(CONFIG_TYPE Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set_target_properties(test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}/${CONFIG_TYPE}
    )
endforeach()

# Copy config to output directory after build
add_custom_command(TARGET test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/config/rimeclaw_config.json"
        "$<TARGET_FILE_DIR:test>/rimeclaw_config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/config/.rimeclaw"
        "$<TARGET_FILE_DIR:test>/.rimeclaw"
)
