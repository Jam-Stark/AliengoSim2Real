cmake_minimum_required(VERSION 3.10)
project(a2_piper_stage2_direct)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(a2_lowlevel REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(trajectory_msgs REQUIRED)
find_package(Torch REQUIRED)
find_package(yaml-cpp REQUIRED)

set(A2_PIPER_STAGE2_DEFAULT_BUNDLE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/../../policy_bundle"
  CACHE PATH "Default LMP Stage2 policy bundle directory"
)

add_executable(a2_piper_stage2_direct
  src/stage2_contract.cpp
  src/stage2_runtime.cpp
  src/stage2_direct_node.cpp
)
target_include_directories(a2_piper_stage2_direct PRIVATE
  include
)
target_compile_definitions(a2_piper_stage2_direct PRIVATE
  A2_PIPER_STAGE2_DEFAULT_BUNDLE_PATH="${A2_PIPER_STAGE2_DEFAULT_BUNDLE_PATH}"
)
if(TORCH_CXX_FLAGS)
  separate_arguments(TORCH_CXX_FLAGS_LIST UNIX_COMMAND "${TORCH_CXX_FLAGS}")
  target_compile_options(a2_piper_stage2_direct PRIVATE ${TORCH_CXX_FLAGS_LIST})
endif()
target_link_libraries(a2_piper_stage2_direct
  ${TORCH_LIBRARIES}
  yaml-cpp
)
ament_target_dependencies(a2_piper_stage2_direct
  a2_lowlevel
  diagnostic_msgs
  rclcpp
  sensor_msgs
  std_msgs
  std_srvs
  trajectory_msgs
)

install(TARGETS a2_piper_stage2_direct
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
