cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/kifume/inference/v1/inference.proto)
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/kifu-source.c src/kifu-source-backend.c src/kifu-source-capture.c src/kifu-source-render.c src/kifu-source-utils.c src/kifu-client.cpp)

if(MSVC)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /wd4996)
endif()

file(MAKE_DIRECTORY ${GENERATED_DIR})

add_custom_command(
  OUTPUT
    ${GENERATED_DIR}/kifume/inference/v1/inference.pb.cc
    ${GENERATED_DIR}/kifume/inference/v1/inference.pb.h
    ${GENERATED_DIR}/kifume/inference/v1/inference.grpc.pb.cc
    ${GENERATED_DIR}/kifume/inference/v1/inference.grpc.pb.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
  COMMAND $<TARGET_FILE:protobuf::protoc>
  ARGS
    --proto_path=${PROTO_DIR}
    --cpp_out=${GENERATED_DIR}
    --grpc_out=${GENERATED_DIR}
    --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
    ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
  VERBATIM
)

add_library(kifume_plugin_proto
  ${GENERATED_DIR}/kifume/inference/v1/inference.pb.cc
  ${GENERATED_DIR}/kifume/inference/v1/inference.grpc.pb.cc
)

if(MSVC)
  target_compile_options(kifume_plugin_proto PRIVATE /wd4996)
endif()

target_include_directories(kifume_plugin_proto PUBLIC ${GENERATED_DIR})
target_link_libraries(kifume_plugin_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE kifume_plugin_proto)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${GENERATED_DIR})

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
