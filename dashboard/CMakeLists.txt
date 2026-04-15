project(dashboard)

file(GLOB_RECURSE DASHBOARD_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

file(GLOB_RECURSE DASHBOARD_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(dashboard ${DASHBOARD_SOURCES} ${DASHBOARD_HEADERS})

target_include_directories(dashboard PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/imguiext
    ${CMAKE_CURRENT_SOURCE_DIR}/imguiext/imapp
    ${CMAKE_CURRENT_SOURCE_DIR}/imguiext/imfonts
    ${CMAKE_CURRENT_SOURCE_DIR}/imguiext/imterm
    ${Stb_INCLUDE_DIR}
)

if(MSVC)
    target_compile_definitions(dashboard PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Link libraries
target_link_libraries(dashboard PRIVATE
    librimeclaw
    CURL::libcurl
    OpenGL::GL
    glfw
    glad::glad
    imgui::imgui
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    Threads::Threads
)

if(WIN32)
    set_target_properties(dashboard PROPERTIES
        OUTPUT_NAME "dashboard"
        WIN32_EXECUTABLE TRUE
    )
elseif(APPLE)
    set_target_properties(dashboard PROPERTIES
        OUTPUT_NAME "dashboard"
    )
else()
    set_target_properties(dashboard PROPERTIES
        OUTPUT_NAME "dashboard"
    )
endif()

foreach(CONFIG_TYPE Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set_target_properties(dashboard PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}/${CONFIG_TYPE}
    )
endforeach()

if(WIN32)
    # Add Windows resource file for icon
    set(RESOURCES_FILE "${CMAKE_CURRENT_SOURCE_DIR}/dashboard.rc")
    if(EXISTS ${RESOURCES_FILE})
        target_sources(dashboard PRIVATE ${RESOURCES_FILE})
    endif()
endif()

# Copy resources to output directory after build
add_custom_command(TARGET dashboard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:dashboard>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/config/rimeclaw_config.json"
        "$<TARGET_FILE_DIR:dashboard>/rimeclaw_config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/config/.rimeclaw"
        "$<TARGET_FILE_DIR:dashboard>/.rimeclaw"
)

# Copy GGUF models to dashboard output directory when local provider is enabled
if(BUILD_LLAMA_LOCAL_PROVIDER)
    add_custom_command(TARGET dashboard POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/3rd/models"
            "$<TARGET_FILE_DIR:dashboard>/models"
        COMMENT "Copying GGUF models to dashboard output directory"
    )
endif()
