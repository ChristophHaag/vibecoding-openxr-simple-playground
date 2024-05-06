# Install cmake, ninja, llvm system wide, e.g. with scoop

if (!(Test-Path -Path deps)) {
    mkdir deps
}
cd deps

if (!(Test-Path -Path SDL_install)) {
    git clone https://github.com/libsdl-org/SDL.git -b release-2.26.2
    cd SDL
    cmake -G Ninja -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSDL_SHARED=OFF -DSDL_STATIC=ON -DCMAKE_INSTALL_PREFIX="..\SDL_install"
    ninja -C build install
    cd ..
}

if (!(Test-Path -Path OpenXR-SDK_install)) {
    git clone https://github.com/KhronosGroup/OpenXR-SDK.git
    cd OpenXR-SDK
    cmake -G Ninja -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="..\OpenXR-SDK_install"
    ninja -C build install
    cd ..
}

cd ..

if (!(Test-Path -Path build/build.ninja)) {
    # link with lld-link instead of msvc to avoid weird assertion in OpenXR-SDK json manifest ifstream
    cmake -DCMAKE_CXX_LINK_EXECUTABLE=lld-link -G Ninja -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSDL2_DIR="deps\SDL_install\cmake\" -DOpenXR_DIR="deps\OpenXR-SDK_install\cmake\" -DCMAKE_INSTALL_PREFIX=".\install"
}
ninja -C build install