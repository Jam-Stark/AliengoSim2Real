cmake_minimum_required(VERSION 3.10)
project(a2_lowlevel)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(unitree_hg REQUIRED)

add_library(a2_lowlevel
  src/a2_lowlevel_interface.cpp
  src/a2_crc.cpp
)
target_include_directories(a2_lowlevel PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(a2_lowlevel PUBLIC cxx_std_17)
ament_target_dependencies(a2_lowlevel
  rclcpp
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

install(TARGETS
  a2_lowlevel
  a2_lowlevel_smoke
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
ament_export_dependencies(rclcpp unitree_hg)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
