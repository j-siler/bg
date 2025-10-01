cmake_minimum_required(VERSION 3.20)
project(bg_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE AND EXISTS "/opt/homebrew")
  list(APPEND CMAKE_PREFIX_PATH
    "/opt/homebrew/opt/grpc"
    "/opt/homebrew/opt/protobuf")
endif()

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." REALPATH)

# ------------------------------
# Game proto (repo root preferred)
# ------------------------------
set(_game_proto_candidates
  "${REPO_ROOT}/proto/bg/v1/bg.proto"
  "${CMAKE_CURRENT_SOURCE_DIR}/proto/bg/v1/bg.proto"
)
set(GAME_PROTO_SRC "")
foreach(cand IN LISTS _game_proto_candidates)
  if(EXISTS "${cand}")
    set(GAME_PROTO_SRC "${cand}")
    break()
  endif()
endforeach()
if(NOT GAME_PROTO_SRC)
  message(FATAL_ERROR "Missing game proto. Create proto/bg/v1/bg.proto (preferred) or server/proto/bg/v1/bg.proto")
endif()

set(BUILD_GAME_ROOT "${CMAKE_CURRENT_BINARY_DIR}/proto")
set(BUILD_GAME_DST  "${BUILD_GAME_ROOT}/bg/v1/bg.proto")
set(GEN_GAME_DIR    "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${BUILD_GAME_ROOT}/bg/v1")
file(MAKE_DIRECTORY "${GEN_GAME_DIR}/bg/v1")

add_custom_command(
  OUTPUT "${BUILD_GAME_DST}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GAME_PROTO_SRC}" "${BUILD_GAME_DST}"
  DEPENDS "${GAME_PROTO_SRC}"
  COMMENT "Copying game proto to build tree: ${BUILD_GAME_DST}"
  VERBATIM
)

set(GEN_GAME_SRCS
  "${GEN_GAME_DIR}/bg/v1/bg.pb.cc"
  "${GEN_GAME_DIR}/bg/v1/bg.grpc.pb.cc"
)
set(GEN_GAME_HDRS
  "${GEN_GAME_DIR}/bg/v1/bg.pb.h"
  "${GEN_GAME_DIR}/bg/v1/bg.grpc.pb.h"
)

add_custom_command(
  OUTPUT ${GEN_GAME_SRCS} ${GEN_GAME_HDRS}
  WORKING_DIRECTORY "${BUILD_GAME_ROOT}"
  COMMAND $<TARGET_FILE:protobuf::protoc>
          --proto_path ${BUILD_GAME_ROOT}
          --cpp_out ${GEN_GAME_DIR}
          --grpc_out ${GEN_GAME_DIR}
          --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
          bg/v1/bg.proto
  DEPENDS "${BUILD_GAME_DST}" protobuf::protoc gRPC::grpc_cpp_plugin
  COMMENT "Generating C++ from bg/v1/bg.proto (within build tree)"
  VERBATIM
)

# ------------------------------
# Admin proto (repo root)
# ------------------------------
set(ADMIN_PROTO_SRC "${REPO_ROOT}/proto/admin/v1/admin.proto")
if(NOT EXISTS "${ADMIN_PROTO_SRC}")
  message(FATAL_ERROR "Missing admin proto at ${ADMIN_PROTO_SRC}")
endif()

set(BUILD_ADMIN_ROOT "${CMAKE_CURRENT_BINARY_DIR}/proto_admin")
set(BUILD_ADMIN_DST  "${BUILD_ADMIN_ROOT}/admin/v1/admin.proto")
set(GEN_ADMIN_DIR    "${CMAKE_CURRENT_BINARY_DIR}/generated_admin")

file(MAKE_DIRECTORY "${BUILD_ADMIN_ROOT}/admin/v1")
file(MAKE_DIRECTORY "${GEN_ADMIN_DIR}/admin/v1")

add_custom_command(
  OUTPUT "${BUILD_ADMIN_DST}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ADMIN_PROTO_SRC}" "${BUILD_ADMIN_DST}"
  DEPENDS "${ADMIN_PROTO_SRC}"
  COMMENT "Copying admin proto to build tree: ${BUILD_ADMIN_DST}"
  VERBATIM
)

set(GEN_ADMIN_SRCS
  "${GEN_ADMIN_DIR}/admin/v1/admin.pb.cc"
  "${GEN_ADMIN_DIR}/admin/v1/admin.grpc.pb.cc"
)
set(GEN_ADMIN_HDRS
  "${GEN_ADMIN_DIR}/admin/v1/admin.pb.h"
  "${GEN_ADMIN_DIR}/admin/v1/admin.grpc.pb.h"
)

add_custom_command(
  OUTPUT ${GEN_ADMIN_SRCS} ${GEN_ADMIN_HDRS}
  WORKING_DIRECTORY "${BUILD_ADMIN_ROOT}"
  COMMAND $<TARGET_FILE:protobuf::protoc>
          --proto_path ${BUILD_ADMIN_ROOT}
          --cpp_out ${GEN_ADMIN_DIR}
          --grpc_out ${GEN_ADMIN_DIR}
          --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
          admin/v1/admin.proto
  DEPENDS "${BUILD_ADMIN_DST}" protobuf::protoc gRPC::grpc_cpp_plugin
  COMMENT "Generating C++ from admin/v1/admin.proto (within build tree)"
  VERBATIM
)

# ------------------------------
# Targets
# ------------------------------

# game server (uses game proto)
add_executable(bg_server
  main.cc
  ${GEN_GAME_SRCS} ${GEN_GAME_HDRS}
  "${REPO_ROOT}/board.cpp"
  "${REPO_ROOT}/boardrenderer.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/logger.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/auth.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/match.cpp"
)
target_include_directories(bg_server PRIVATE
  ${GEN_GAME_DIR}
  "${REPO_ROOT}"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(bg_server PRIVATE
  gRPC::grpc++
  protobuf::libprotobuf
)

# smoke repl (no proto deps)
add_executable(bg_smoke
  "${CMAKE_CURRENT_SOURCE_DIR}/smoke_main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/logger.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/auth.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/match.cpp"
)
target_include_directories(bg_smoke PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

# admin rpc server (uses admin proto)
add_executable(bg_admin
  "${CMAKE_CURRENT_SOURCE_DIR}/admin_server_main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/rpc_auth_match.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/logger.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/auth.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/match.cpp"
  ${GEN_ADMIN_SRCS} ${GEN_ADMIN_HDRS}
)
target_include_directories(bg_admin PRIVATE
  ${GEN_ADMIN_DIR}
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(bg_admin PRIVATE
  gRPC::grpc++
  protobuf::libprotobuf
)

# Reflection intentionally disabled to keep prod-like surface.
# (No BG_HAS_REFLECTION, no grpc++_reflection link here.)

message(STATUS "Game proto source: ${GAME_PROTO_SRC}")
message(STATUS "Game generated dir: ${GEN_GAME_DIR}")
message(STATUS "Admin proto source: ${ADMIN_PROTO_SRC}")
message(STATUS "Admin generated dir: ${GEN_ADMIN_DIR}")
