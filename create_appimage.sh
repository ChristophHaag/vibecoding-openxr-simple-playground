#!/bin/bash

DIR="$(dirname "${BASH_SOURCE[0]}")"
DIR="$(realpath "${DIR}")"

mkdir -p "$DIR/appimage"
cd "$DIR/appimage"

if [ -f "appimagetool-x86_64.AppImage" ]
then
	echo "appimagetool-x86_64.AppImage exists, skipping download"
else
	echo "Downloading https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
	wget https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
	chmod +x appimagetool-x86_64.AppImage
fi


if [ -f "linuxdeploy-x86_64.AppImage" ]
then
        echo "linuxdeploy-x86_64.AppImage exists, skipping download"
else
        echo "Downloading https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
        wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
        chmod +x linuxdeploy-x86_64.AppImage
fi

# TODO licensing
if [ -f "openxr-playground.png" ]
then
        echo "openxr-playground.ong exists, skipping download"
else
        echo "Downloading"
        wget -O openxr-playground.png https://upload.wikimedia.org/wikipedia/commons/thumb/0/00/Map_icons_by_Scott_de_Jonge_-_playground.svg/480px-Map_icons_by_Scott_de_Jonge_-_playground.svg.png
fi

echo "[Desktop Entry]
Type=Application
Version=1.0
Name=OpenXR Playground
Comment=OpenXR Playground
Exec=openxr-playground
Icon=openxr-playground
GenericName=OpenXRPlayground
Categories=Utility
Terminal=false
" > "$DIR/appimage/openxr-playground.desktop"

rm -rf "$DIR/appimage/build"
cmake -GNinja -B"$DIR/appimage/build" -DCMAKE_BUILD_TYPE=RelWithDebinfo -DCMAKE_INSTALL_PREFIX=/usr "$DIR"
ninja -C "$DIR/appimage/build"
DESTDIR="$DIR/appimage/AppDir" ninja -C "$DIR/appimage/build" install

cp "$DIR/appimage/openxr-playground.desktop" "$DIR/appimage/AppDir"
cp "$DIR/appimage/openxr-playground.png" "$DIR/appimage/AppDir/openxr-playground.png"

"$DIR/appimage/linuxdeploy-x86_64.AppImage" --appdir "$DIR/appimage/AppDir" --output appimage --desktop-file openxr-playground.desktop -i "$DIR/appimage/AppDir/openxr-playground.png"
