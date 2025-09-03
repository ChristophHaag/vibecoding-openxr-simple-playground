# OpenXR C Playground

This example exercises many areas of the OpenXR API.
Some parts of the API are abstracted, though the abstractions are intentionally kept simple for simple editing.

For a simpler, more straightforward example, see
https://gitlab.freedesktop.org/monado/demos/openxr-simple-example

# Building

## Linux

Install the appropriate packages for sdl2, openxr loader, glx, xlib.

    cmake -G Ninja -B build
    ninja -C build

## Windows

### msvc + vcpkg

Make sure to git clone --recursive or git submodule update --init.

Install visual studio (express) or Build Tools for Visual Studio. Open a developer powershell and run

    .\vcpkg\bootstrap-vcpkg.bat
    .\vcpkg\vcpkg.exe install sdl2:x64-windows openxr-loader:x64-windows
    cmake -GNinja -Bbuild
    ninja -C build

### clang + source build

This script assumes that git, clang (llvm), cmake and ninja are installed system wide (e.g. with scoop). It builds all dependencies directly with cmake.

For most people the msvc + vcpkg build is the better option.

    .\make_simple_playground.ps1

# Running

Unless the OpenXR runtime is installed in the file system, the `XR_RUNTIME_JSON` variable has to be set for the loader to know where to look for the runtime and how the runtime is named

    XR_RUNTIME_JSON=~/monado/build/openxr_monado-dev.json

then, you should be ready to run `./openxr-playground`.

If you want to use API layers that are not installed in the default path, set the variable `XR_API_LAYER_PATH`

    XR_API_LAYER_PATH=/path/to/api_layers/

This will enable to loader to find api layers at this path and enumerate them with `xrEnumerateApiLayerProperties()`

API Layers can be enabled either with code or the loader can be told to enable an API layer with `XR_ENABLE_API_LAYERS`

    XR_ENABLE_API_LAYERS=XR_APILAYER_LUNARG_core_validation
