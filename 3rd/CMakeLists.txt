# Vcpkg Packages - find_package() calls
# Packages are auto-installed by vcpkg manifest mode

# Threading (required on Linux/macOS for std::thread, std::mutex, etc.)
find_package(Threads REQUIRED)

# Core dependencies (always required)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# llama.cpp (optional: local LLM inference)
if(BUILD_LLAMA_LOCAL_PROVIDER)
    find_package(llama CONFIG REQUIRED)
endif()

# GUI dependencies (only when building dashboard)
if(BUILD_DASHBOARD)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(Stb REQUIRED)
    find_package(glad CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)
endif()

# ============================================================================
# Print Configuration
# ============================================================================
message(STATUS "Vcpkg packages found:")
message(STATUS "  CURL ${CURL_VERSION}")
message(STATUS "  nlohmann_json, spdlog")
if(BUILD_LLAMA_LOCAL_PROVIDER)
    message(STATUS "  llama-cpp (local LLM inference)")
endif()
if(BUILD_DASHBOARD)
    message(STATUS "  glfw3, Stb, glad, imgui, OpenGL")
endif()
