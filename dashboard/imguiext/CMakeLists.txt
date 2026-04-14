project(imguiext)

file(GLOB_RECURSE IMGUIEXT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

file(GLOB_RECURSE IMGUIEXT_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_library(imguiext STATIC ${IMGUIEXT_SOURCES} ${IMGUIEXT_HEADERS})

target_include_directories(imguiext PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/imapp
    ${CMAKE_CURRENT_SOURCE_DIR}/imfonts
    ${CMAKE_CURRENT_SOURCE_DIR}/imterm
)

target_link_libraries(imguiext PUBLIC
    imgui::imgui
    glad::glad
    glfw
)

if(MSVC)
    target_compile_definitions(imguiext PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()
