cmake_minimum_required(VERSION 3.10)
project(aliengo_deploy)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

# ============================================================
# Inference backend: LibTorch (default) or ONNX Runtime
# ============================================================
option(USE_ONNX "Build with ONNX Runtime instead of LibTorch" OFF)
set(ONNXRUNTIME_ROOT "" CACHE PATH "Root directory of ONNX Runtime")

# ============================================================
# ROS1 / catkin
# ============================================================
find_package(catkin REQUIRED COMPONENTS
  roscpp
  std_msgs
  geometry_msgs
  unitree_legged_msgs
  sensor_msgs
)

catkin_package()

# ============================================================
# Third-party libraries
# ============================================================
find_package(Eigen3 REQUIRED)
find_package(OpenCV REQUIRED)

# jsoncpp
find_path(JSONCPP_INCLUDE_DIR NAMES json/json.h PATH_SUFFIXES jsoncpp)
find_library(JSONCPP_LIB NAMES jsoncpp)
if(NOT JSONCPP_INCLUDE_DIR OR NOT JSONCPP_LIB)
  message(FATAL_ERROR "Could not find jsoncpp. Install libjsoncpp-dev.")
endif()

# ============================================================
# Inference backend
# ============================================================
if(USE_ONNX)
  add_definitions(-DUSE_ONNX)
  set(ONNX_SEARCH_HINTS)
  if(ONNXRUNTIME_ROOT)
    list(APPEND ONNX_SEARCH_HINTS "${ONNXRUNTIME_ROOT}")
  endif()
  if(DEFINED ENV{ONNXRUNTIME_ROOT} AND NOT "$ENV{ONNXRUNTIME_ROOT}" STREQUAL "")
    list(APPEND ONNX_SEARCH_HINTS "$ENV{ONNXRUNTIME_ROOT}")
  endif()
  find_path(ONNX_INCLUDE_DIR NAMES onnxruntime_cxx_api.h
    HINTS ${ONNX_SEARCH_HINTS} PATH_SUFFIXES include)
  find_library(ONNX_LIB NAMES onnxruntime
    HINTS ${ONNX_SEARCH_HINTS} PATH_SUFFIXES lib lib64)
  if(ONNX_INCLUDE_DIR AND ONNX_LIB)
    message(STATUS "Found ONNX Runtime include: ${ONNX_INCLUDE_DIR}")
    message(STATUS "Found ONNX Runtime library: ${ONNX_LIB}")
    include_directories(${ONNX_INCLUDE_DIR})
    set(INFERENCE_LIBS ${ONNX_LIB})
  else()
    message(FATAL_ERROR "Could not find ONNX Runtime.")
  endif()
else()
  find_package(Torch REQUIRED)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")
  set(INFERENCE_LIBS ${TORCH_LIBRARIES})
endif()

# ============================================================
# Include paths
# ============================================================
include_directories(
  include
  ${catkin_INCLUDE_DIRS}
  ${EIGEN3_INCLUDE_DIRS}
  ${JSONCPP_INCLUDE_DIR}
)

# Resolve symlinks so that ../utils works correctly when this package
# is symlinked into a catkin workspace (ln -s ... catkin_ws/src/aliengo_deploy)
get_filename_component(REAL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR} REALPATH)
message(STATUS "aliengo_deploy real source dir: ${REAL_SOURCE_DIR}")

# Reusable inference core from utils/
set(CPP_MANAGER_ENV_PATH ${REAL_SOURCE_DIR}/../utils/cpp_manager_env)
include_directories(${CPP_MANAGER_ENV_PATH})
file(GLOB CPP_MANAGER_ENV_SRC ${CPP_MANAGER_ENV_PATH}/*.cpp)

# Optional: local gamepad support
set(CPP_GAMEPAD_PATH ${REAL_SOURCE_DIR}/../utils/cpp_gamepad)
include_directories(${CPP_GAMEPAD_PATH})
file(GLOB CPP_GAMEPAD_SRC ${CPP_GAMEPAD_PATH}/*.cpp)

# ============================================================
# Policy path defaults (can be overridden at runtime)
# ============================================================
set(DEFAULT_ALIENGO_POLICY_OLD_PATH "${REAL_SOURCE_DIR}/../policy/aliengo/")
set(DEFAULT_ALIENGO_POLICY_NEW_PATH "${REAL_SOURCE_DIR}/../policy/aliengo_new/")
if(NOT DEFINED DEFAULT_ALIENGO_POLICY_PATH OR
   "${DEFAULT_ALIENGO_POLICY_PATH}" STREQUAL "${DEFAULT_ALIENGO_POLICY_NEW_PATH}")
  set(DEFAULT_ALIENGO_POLICY_PATH "${DEFAULT_ALIENGO_POLICY_OLD_PATH}"
      CACHE STRING "Default path to Aliengo policy directory" FORCE)
endif()
add_definitions(-DDEFAULT_ALIENGO_POLICY_PATH="${DEFAULT_ALIENGO_POLICY_PATH}")

# ============================================================
# Build target: aliengo_deploy
# ============================================================
add_executable(aliengo_deploy
  src/aliengo_deploy_main.cpp
  src/aliengo_deploy_node.cpp
  src/aliengo_udp_transport.cpp
  src/gait_clock.cpp
  src/wireless_remote_decoder.cpp
  ${CPP_MANAGER_ENV_SRC}
  ${CPP_GAMEPAD_SRC}
)

target_link_libraries(aliengo_deploy
  ${catkin_LIBRARIES}
  ${INFERENCE_LIBS}
  ${OpenCV_LIBS}
  ${JSONCPP_LIB}
  udev
  pthread
)

# ============================================================
# Test tools (fake state publisher + cmd monitor)
# ============================================================
add_executable(fake_low_state_publisher
  src/test/fake_low_state_publisher.cpp
)
target_link_libraries(fake_low_state_publisher
  ${catkin_LIBRARIES}
  pthread
)

add_executable(low_cmd_monitor
  src/test/low_cmd_monitor.cpp
)
target_link_libraries(low_cmd_monitor
  ${catkin_LIBRARIES}
)

# ============================================================
# Install
# ============================================================
install(TARGETS aliengo_deploy fake_low_state_publisher low_cmd_monitor
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(DIRECTORY launch/
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/launch
)
