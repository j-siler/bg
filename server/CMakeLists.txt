cmake_minimum_required(VERSION 3.20)
project(bg_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Homebrew hints (Apple Silicon)
if(APPLE AND EXISTS "/opt/homebrew")
  list(APPEND CMAKE_PREFIX_PATH
    "/opt/homebrew/opt/grpc"
    "/opt/homebrew/opt/protobuf")
endif()

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

# --- locate source proto (either location is fine) ---
set(_proto_candidates
  "${CMAKE_CURRENT_SOURCE_DIR}/proto/bg/v1/bg.proto"     # server-local
  "${CMAKE_CURRENT_SOURCE_DIR}/../proto/bg/v1/bg.proto"  # repo root
)
set(PROTO_ABS "")
foreach(cand IN LISTS _proto_candidates)
  if(EXISTS "${cand}")
    set(PROTO_ABS "${cand}")
    break()
  endif()
endforeach()
if(NOT PROTO_ABS)
  message(FATAL_ERROR "Missing bg.proto. Create server/proto/bg/v1/bg.proto or ../proto/bg/v1/bg.proto")
endif()

# --- copy proto into build tree (stable include root for protoc) ---
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

# --- outputs we expect from protoc ---
set(GEN_SRCS
  "${GEN_DIR}/bg/v1/bg.pb.cc"
  "${GEN_DIR}/bg/v1/bg.grpc.pb.cc"
)
set(GEN_HDRS
  "${GEN_DIR}/bg/v1/bg.pb.h"
  "${GEN_DIR}/bg/v1/bg.grpc.pb.h"
)

# --- run protoc from inside BUILD_PROTO_ROOT; DO NOT embed quotes in args ---
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

# --- server target ---
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." REALPATH)

add_executable(bg_server
  main.cc
  ${GEN_SRCS} ${GEN_HDRS}
  "${REPO_ROOT}/board.cpp"
  "${REPO_ROOT}/boardrenderer.cpp"
)

target_include_directories(bg_server PRIVATE
  ${GEN_DIR}
  "${REPO_ROOT}"
)

target_link_libraries(bg_server PRIVATE
  gRPC::grpc++
  protobuf::libprotobuf
)

# Diagnostics
message(STATUS "Proto source: ${PROTO_ABS}")
message(STATUS "Proto copied to: ${BUILD_PROTO_DST}")
message(STATUS "Generated dir: ${GEN_DIR}")
