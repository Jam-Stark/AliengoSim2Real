cmake_minimum_required(VERSION 3.10)
project(a2_lowlevel)

option(BUILD_A2_POLICY_DEPLOY "Build A2 LibTorch policy deploy executable" OFF)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(unitree_hg REQUIRED)

set(A2_POLICY_DEFAULT_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/../../policy/A2_policy/policy.pt"
  CACHE FILEPATH "Default A2 TorchScript policy path"
)
set(A2_POLICY_DEFAULT_JSON_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/../../policy/A2_policy/policy.json"
  CACHE FILEPATH "Default A2 policy contract JSON path"
)

add_library(a2_lowlevel
  src/a2_lowlevel_interface.cpp
  src/a2_remote.cpp
  src/a2_crc.cpp
)
target_include_directories(a2_lowlevel PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(a2_lowlevel PUBLIC cxx_std_17)
ament_target_dependencies(a2_lowlevel
  rclcpp
  std_msgs
  unitree_hg
)

add_executable(a2_lowlevel_smoke
  src/a2_lowlevel_smoke.cpp
)
target_link_libraries(a2_lowlevel_smoke
  a2_lowlevel
)
target_compile_features(a2_lowlevel_smoke PRIVATE cxx_std_17)
ament_target_dependencies(a2_lowlevel_smoke
  rclcpp
  unitree_hg
)

set(A2_INSTALL_TARGETS
  a2_lowlevel
  a2_lowlevel_smoke
)

if(BUILD_A2_POLICY_DEPLOY)
  find_package(Torch REQUIRED)
  find_path(JSONCPP_INCLUDE_DIR NAMES json/json.h PATH_SUFFIXES jsoncpp)
  find_library(JSONCPP_LIB NAMES jsoncpp)
  if(NOT JSONCPP_INCLUDE_DIR OR NOT JSONCPP_LIB)
    message(FATAL_ERROR
      "Could not find jsoncpp. Install libjsoncpp-dev or provide "
      "JSONCPP_INCLUDE_DIR/JSONCPP_LIB."
    )
  endif()

  set(cpp_manager_env_path
    "${CMAKE_CURRENT_SOURCE_DIR}/../../utils/cpp_manager_env"
  )
  file(GLOB cpp_manager_env "${cpp_manager_env_path}/*.cpp")

  add_executable(a2_policy_deploy
    src/a2_policy_deploy_node.cpp
    ${cpp_manager_env}
  )
  target_include_directories(a2_policy_deploy PRIVATE
    "${cpp_manager_env_path}"
    "${JSONCPP_INCLUDE_DIR}"
  )
  target_compile_definitions(a2_policy_deploy PRIVATE
    A2_POLICY_DEFAULT_PATH="${A2_POLICY_DEFAULT_PATH}"
    A2_POLICY_DEFAULT_JSON_PATH="${A2_POLICY_DEFAULT_JSON_PATH}"
  )
  if(TORCH_CXX_FLAGS)
    separate_arguments(TORCH_CXX_FLAGS_LIST UNIX_COMMAND "${TORCH_CXX_FLAGS}")
    target_compile_options(a2_policy_deploy PRIVATE ${TORCH_CXX_FLAGS_LIST})
  endif()
  target_compile_features(a2_policy_deploy PRIVATE cxx_std_17)
  target_link_libraries(a2_policy_deploy
    a2_lowlevel
    ${TORCH_LIBRARIES}
    ${JSONCPP_LIB}
  )
  ament_target_dependencies(a2_policy_deploy
    rclcpp
    std_msgs
    unitree_hg
  )
  list(APPEND A2_INSTALL_TARGETS a2_policy_deploy)
endif()

install(TARGETS
  ${A2_INSTALL_TARGETS}
  EXPORT a2_lowlevelTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

ament_export_include_directories(include)
ament_export_libraries(a2_lowlevel)
ament_export_targets(a2_lowlevelTargets HAS_LIBRARY_TARGET)
ament_export_dependencies(rclcpp std_msgs unitree_hg)

if(BUILD_TESTING)
  enable_testing()
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  add_executable(a2_remote_decode_test
    test/a2_remote_decode_test.cpp
    src/a2_remote.cpp
  )
  target_include_directories(a2_remote_decode_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_compile_features(a2_remote_decode_test PRIVATE cxx_std_17)
  add_test(NAME a2_remote_decode_test COMMAND a2_remote_decode_test)
endif()

ament_package()
