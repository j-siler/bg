cmake_minimum_required(VERSION 3.16)
project(bg LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build toggles (optional)
option(BUILD_CLIENT_TUI "Build client TUI target" ON)
option(BUILD_SERVER     "Build server target"     ON)

# Add the two subprojects; they do their own deps and proto/grpc generation.
if(BUILD_CLIENT_TUI)
  add_subdirectory(client-tui)
endif()

if(BUILD_SERVER)
  add_subdirectory(server)
endif()

message(STATUS "Root aggregator: BUILD_CLIENT_TUI=${BUILD_CLIENT_TUI}  BUILD_SERVER=${BUILD_SERVER}")
