cmake_minimum_required(VERSION 3.20)
project(bg_client_tui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Homebrew hints (Apple Silicon) ---
if(APPLE AND EXISTS "/opt/homebrew")
  list(APPEND CMAKE_PREFIX_PATH
    "/opt/homebrew/opt/grpc"
    "/opt/homebrew/opt/protobuf")
endif()

# Use CONFIG packages to avoid Find-module conflicts
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(Curses REQUIRED)

# ===== Locate source proto (either location is fine) =====
# Prefer shared proto under server/, but support repo-root too
set(_proto_candidates
  "${CMAKE_CURRENT_SOURCE_DIR}/../server/proto/bg/v1/bg.proto" # typical
  "${CMAKE_CURRENT_SOURCE_DIR}/../proto/bg/v1/bg.proto"        # alternate
  "${CMAKE_CURRENT_SOURCE_DIR}/proto/bg/v1/bg.proto"           # client-local
)
set(PROTO_ABS "")
foreach(cand IN LISTS _proto_candidates)
  if(EXISTS "${cand}")
    set(PROTO_ABS "${cand}")
    break()
  endif()
endforeach()
if(NOT PROTO_ABS)
  message(FATAL_ERROR "Missing bg.proto. Put it at ../server/proto/bg/v1/bg.proto (recommended).")
endif()

# ===== Copy proto into build tree, then generate there =====
set(BUILD_PROTO_ROOT "${CMAKE_CURRENT_BINARY_DIR}/proto")
set(BUILD_PROTO_DST  "${BUILD_PROTO_ROOT}/bg/v1/bg.proto")
set(GEN_DIR          "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${BUILD_PROTO_ROOT}/bg/v1")
file(MAKE_DIRECTORY "${GEN_DIR}/bg/v1")

add_custom_command(
  OUTPUT "${BUILD_PROTO_DST}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROTO_ABS}" "${BUILD_PROTO_DST}"
  DEPENDS "${PROTO_ABS}"
  COMMENT "Copying proto to build tree: ${BUILD_PROTO_DST}"
  VERBATIM
)

# Generated files
set(GEN_SRCS
  "${GEN_DIR}/bg/v1/bg.pb.cc"
  "${GEN_DIR}/bg/v1/bg.grpc.pb.cc"
)
set(GEN_HDRS
  "${GEN_DIR}/bg/v1/bg.pb.h"
  "${GEN_DIR}/bg/v1/bg.grpc.pb.h"
)

# Run protoc from inside BUILD_PROTO_ROOT (no quoted args)
add_custom_command(
  OUTPUT ${GEN_SRCS} ${GEN_HDRS}
  WORKING_DIRECTORY "${BUILD_PROTO_ROOT}"
  COMMAND $<TARGET_FILE:protobuf::protoc>
          --proto_path ${BUILD_PROTO_ROOT}
          --cpp_out ${GEN_DIR}
          --grpc_out ${GEN_DIR}
          --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
          bg/v1/bg.proto
  DEPENDS "${BUILD_PROTO_DST}" protobuf::protoc gRPC::grpc_cpp_plugin
  COMMENT "Generating C++ from bg/v1/bg.proto (within build tree)"
  VERBATIM
)

# Repo root so we can include headers and renderer sources
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." REALPATH)

add_executable(bg_tui
  main.cc
  ${GEN_SRCS} ${GEN_HDRS}
  "${REPO_ROOT}/boardrenderer.cpp"
  "${REPO_ROOT}/ncurses_renderer.cpp"
)

# Headers for generated code + your headers (board.hpp etc.)
target_include_directories(bg_tui PRIVATE
  ${GEN_DIR}
  "${REPO_ROOT}"
)

# Link gRPC/Protobuf and ncurses
if(TARGET Curses::Curses)
  target_link_libraries(bg_tui PRIVATE gRPC::grpc++ protobuf::libprotobuf Curses::Curses)
else()
  target_include_directories(bg_tui PRIVATE ${CURSES_INCLUDE_DIR})
  target_link_libraries(bg_tui PRIVATE gRPC::grpc++ protobuf::libprotobuf ${CURSES_LIBRARIES})
endif()

# Helpful diagnostics
message(STATUS "Proto source: ${PROTO_ABS}")
message(STATUS "Proto copied to: ${BUILD_PROTO_DST}")
message(STATUS "Generated dir: ${GEN_DIR}")
