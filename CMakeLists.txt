cmake_minimum_required(VERSION 3.5)

project(visca2uvc)

set(ABSL_PROPAGATE_CXX_STD ON)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(abseil-cpp)
add_subdirectory(libuvc)

add_executable(visca2uvc main.cc)

target_include_directories(visca2uvc SYSTEM PUBLIC
  libuvc/include
  build/libuvc/include
)

target_link_libraries(visca2uvc absl::statusor absl::strings absl::flags absl::flags_parse LibUVC::UVC)
