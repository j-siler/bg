cmake_minimum_required(VERSION 3.20)

# Top-level project for the whole tree.
project(bg LANGUAGES CXX)

# Global C++ settings.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Helpful default for single-config generators (e.g., Makefiles, Ninja).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Homebrew / macOS hints so subprojects can find deps without repeating boilerplate.
if(APPLE AND EXISTS "/opt/homebrew")
  list(APPEND CMAKE_PREFIX_PATH
    "/opt/homebrew/opt/grpc"
    "/opt/homebrew/opt/protobuf"
    "/opt/homebrew/opt/c-ares"
    "/opt/homebrew/opt/re2"
    "/opt/homebrew/opt/openssl@3"
    "/opt/homebrew/opt/ncurses"
  )
endif()

# Repo-wide include path for headers that live at the repo root (optional).
# Subprojects can add ${PROJECT_SOURCE_DIR} to their target_include_directories
# if they include headers from the root (e.g., boardrenderer.hpp, etc.).
include(GNUInstallDirs)

# Add subprojects. Each subdir has its own CMakeLists.txt.
# They can still use the prefix hints above.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/server/CMakeLists.txt")
  add_subdirectory(server)
else()
  message(STATUS "Skipping server: no server/CMakeLists.txt found.")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/client-tui/CMakeLists.txt")
  add_subdirectory(client-tui)
else()
  message(STATUS "Skipping client-tui: no client-tui/CMakeLists.txt found.")
endif()

# Convenience messages
message(STATUS "Top-level project: ${PROJECT_NAME}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
