cmake_minimum_required(VERSION 3.0)
project(openxr-example)

if (POLICY CMP0072)
  cmake_policy (SET CMP0072 NEW)
endif(POLICY CMP0072)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.14.0")
	cmake_policy(SET CMP0083 NEW)
	include(CheckPIESupported)
	check_pie_supported()
endif()

include(CheckPIESupported)
check_pie_supported()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(X11 REQUIRED)
find_package(OpenGL REQUIRED)

INCLUDE(FindPkgConfig)
PKG_SEARCH_MODULE(SDL2 REQUIRED sdl2)

add_executable(openxr-playground main.c)

include(FindPkgConfig)
pkg_search_module(OPENXR openxr)
if (OPENXR_FOUND)
  MESSAGE("OpenXR found with pkg-config")
  target_link_libraries(openxr-playground PRIVATE ${OPENXR_LIBRARIES})
else()
  MESSAGE("OpenXR not found with pkg-config, trying cmake script")
  # current issue in upstream OpenXR cmake files requires us to find Threads on our own
  find_package(Threads REQUIRED)
  find_package(OpenXR REQUIRED)
  if (NOT OpenXR_FOUND)
     MESSAGE(FATAL_ERROR "OpenXR not found!")
  endif()

  target_include_directories(openxr-playground PRIVATE ${OpenXR_INCLUDE_DIR})
  target_link_libraries(openxr-playground PRIVATE OpenXR::openxr_loader)
endif()

target_link_libraries(openxr-playground PRIVATE Xrandr ${X11_LIBRARIES} ${OPENGL_LIBRARIES} ${SDL2_LIBRARIES} m)
target_include_directories(openxr-playground PRIVATE ${SDL2_INCLUDE_DIRS})

if(MSVC)
  target_compile_options(openxr-playground PRIVATE /W4 /WX)
else(MSVC)
  target_compile_options(openxr-playground PRIVATE -pedantic -Wall -Wextra -Wno-unused-parameter)
endif(MSVC)

install(TARGETS openxr-playground RUNTIME DESTINATION bin)
