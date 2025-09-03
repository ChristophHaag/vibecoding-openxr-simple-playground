cmake_minimum_required(VERSION 3.5)

option(VCPKG "Use vcpkg for dependencies" OFF)

if (VCPKG)
  MESSAGE("Using vcpkg in ./vcpkg")
  set(CMAKE_TOOLCHAIN_FILE ./vcpkg/scripts/buildsystems/vcpkg.cmake)
endif()

project(openxr-playground)

if (POLICY CMP0072)
  cmake_policy (SET CMP0072 NEW)
endif(POLICY CMP0072)

# OpenGL headers on windows are a pain, just use the one in our "GL" subdir
IF (WIN32)
  SET(SYSTEM_INCLUDES "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/external/GL")
  # SET(EXTRA_LIBS "-lopengl32")

  # TODO: visual studio build getopt?
  # find_library(GETOPT_LIB getopt)
  # SET(EXTRA_LIBS ${GETOPT_LIB})
  SET(SYSTEM_INCLUDES "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/external/getopt")
ENDIF()

# On Windows, find SDL2 with its CMake package. Everywhere else, use pkg-config.
IF (WIN32)
	find_package(SDL2 REQUIRED)
ELSE()
	INCLUDE(FindPkgConfig)
	PKG_SEARCH_MODULE(SDL2 REQUIRED sdl2)
ENDIF()

add_executable(openxr-playground main.c ${EXTRA_SOURCES})

find_package(OpenGL REQUIRED)

IF(NOT WIN32)
  find_package(X11 REQUIRED)
  SET(EXTRA_LIBS "Xrandr ${X11_LIBRARIES}" m)

  include(FindPkgConfig)
  pkg_search_module(EGL egl)
  list(APPEND EXTRA_LIBS ${EGL_LIBRARIES})
  list(APPEND SYSTEM_INCLUDES ${EGL_INCLUDE_DIRS})
ENDIF()

# Try OpenXRConfig.cmake from OpenXR SDK
find_package(OpenXR REQUIRED)
if (NOT OpenXR_FOUND)
    MESSAGE(FATAL_ERROR "OpenXR not found!")
endif()

# We use included openxr headers, so do not add the header path from the found OpenXR SDK
# list(APPEND SYSTEM_INCLUDES ${OpenXR_INCLUDE_DIR})

list(APPEND SYSTEM_INCLUDES ${CMAKE_SOURCE_DIR}/external)
list(APPEND EXTRA_LIBS OpenXR::openxr_loader)

# Alternative: try openxr.pc from OpenXR SDK
# include(FindPkgConfig)
# pkg_search_module(OPENXR openxr)
# if (OPENXR_FOUND)
#   MESSAGE("OpenXR found with pkg-config")
#   list(APPEND EXTRA_LIBS ${OPENXR_LIBRARIES})
# ENDIF()

target_link_libraries(openxr-playground PRIVATE ${OPENGL_LIBRARIES} ${SDL2_LIBRARIES} ${EXTRA_LIBS})
# ${CMAKE_SOURCE_DIR}/external/openxr_headers for external/openxr_headers/XR_MNDX_xdev_space.h ->  openxr_extension_helpers.h
target_include_directories(openxr-playground PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/external/openxr_headers)
target_include_directories(openxr-playground SYSTEM PRIVATE ${SYSTEM_INCLUDES})


if(MSVC)
  target_compile_options(openxr-playground PRIVATE /D_CRT_SECURE_NO_WARNINGS /W4 /WX /wd4100 /wd4244 /wd4127 /wd4201 /wd4245 /wd4305 /wd4477)
  target_link_options(openxr-playground PRIVATE /STACK:10485760)
else(MSVC)
  target_compile_options(openxr-playground PRIVATE -pedantic -Wall -Wextra -Wno-unused-parameter)
endif(MSVC)

install(TARGETS openxr-playground RUNTIME DESTINATION bin)
