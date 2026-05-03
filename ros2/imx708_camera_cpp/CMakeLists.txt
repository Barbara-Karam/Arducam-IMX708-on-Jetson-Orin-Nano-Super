cmake_minimum_required(VERSION 3.8)
project(imx708_camera_cpp)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(OpenCV REQUIRED)

add_executable(imx708_publisher src/imx708_publisher.cpp)
ament_target_dependencies(imx708_publisher
  rclcpp
  sensor_msgs
  cv_bridge
  image_transport
  OpenCV
)

install(TARGETS imx708_publisher
  DESTINATION lib/${PROJECT_NAME})

ament_package()
