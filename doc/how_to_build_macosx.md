# Setting Up the Development Environment on macOS

## Necessary Software

- git
- brew
- Xcode
- cmake (3.10 or later)
- Qt 5.x (5.15 or later)
- boost (1.55.0 or later)

## Building on macOS

### Download and install Xcode from Apple

When downloading Xcode, you should use the appropriate version for your OS version.  You can refer to the Version Comparison Table on https://en.wikipedia.org/wiki/Xcode to find out which version you should use.

Apple Store usually provides for the most recent macOS version.  For older versions, you will need to go to the Apple Developer site.

After installing the application, you will need to start it in order to complete the installation.

### Install Homebrew from https://brew.sh

Check site for any changes in installation instructions, but they will probably just be this:

1. Open a Terminal window
2. Execute the following statement:
```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
```

### Install required software using brew

In a Terminal window, execute the following statements:
```sh
brew install glew lz4 libjpeg libpng lzo pkg-config libusb cmake git-lfs libmypaint boost jpeg-turbo opencv
git lfs install
```

### Install a Qt 5 SDK

Install a macOS Qt 5 SDK (5.15 or later), including `Qt Script (Deprecated)`,
using the Qt installer from https://www.qt.io/download or your own Qt build.
Set its installation prefix, for example:
```sh
export QT_ROOT_DIR="$HOME/Qt/5.15.2/clang_64"
```

The Intel macOS CI temporarily uses the public Qt 5.15.2 SDK with `qtscript`
via `install-qt-action`, and a cached source build of OpenCV 5.0.0. It no longer
installs Homebrew `qt@5` (disabled on 2027-05-19) or `opencv@4` (disabled on
2027-07-09). This is an installation bridge; Qt 5 still needs to be replaced by
Qt 6. The older Qt SDK requires runtime regression testing before release.
The CI's OpenCV build keeps AVFoundation camera capture and excludes Qt/VTK GUI
modules and FFmpeg/GStreamer backends; OpenToonz's own FFmpeg support is separate.

### Remove incompatible symbolic directory
Check to see if this symbolic glew directory exists. If so, remove it:
```sh
ls -l /usr/local/lib/cmake/glew
rm /usr/local/lib/cmake/glew
```

### Set up OpenToonz repository

These steps will put the OpenToonz repository under /Users/yourlogin/Documents.
```sh
cd ~/Documents   #or where you want to store the repository#
git clone https://github.com/opentoonz/opentoonz
cd opentoonz
git lfs pull
cd thirdparty/lzo
cp -r 2.03/include/lzo driver
cd ../tiff-4.0.3
./configure --disable-lzma && make
```

If you downloaded and installed boost from https://boost.org instead of homebrew, move the package under `thirdparty/boost` as follows: 
```sh
cd thirdparty/boost
mv ~/Downloads/boost_1_72_0.tar.bz2 .   #or whatever the boost filename you downloaded is#
tar xvjf boost_1_72_0.tar.bz2
```

### Configure environment and Build OpenToonz

1. Create the build directory with the following:
```sh
cd ~/Documents/opentoonz/toonz
mkdir build
cd build
```
2. Include libjpeg-turbo path to PKG_CONFIG_PATH

```sh
export PKG_CONFIG_PATH="/opt/homebrew/opt/jpeg-turbo/lib/pkgconfig:$PKG_CONFIG_PATH"
```

3. Set up build environment

To build from command line, do the following:
```sh
cmake ../sources -DQT_PATH="$QT_ROOT_DIR/lib"
make
```
- `QT_PATH` points to the SDK's `lib` directory. CMake accepts OpenCV 4.1+ and OpenCV 5; if it cannot find your installation, also pass `-DOpenCV_DIR=/path/to/lib/cmake/opencv5` (or `opencv4` for OpenCV 4).

To build using Xcode, do the following:
```sh
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
cmake -G Xcode ../sources -B. -DQT_PATH="$QT_ROOT_DIR/lib" -DWITH_TRANSLATION=OFF
```
- Note that the option `-DWITH_TRANSLATION=OFF` is needed to avoid error when using XCode 12+ which does not allow to add the same source to multiple targets.
- Open Xcode app and open project /Users/yourlogin/Documents/opentoonz/toonz/build/OpenToonz.xcodeproj
- Change `ALL_BUILD` to `OpenToonz`
- Start build with: Product -> Build

    - NOTE about rebuilding in Xcode: The initial build should succeed without any errors.  There after, the build will succeed but the following 3 errors can be ignored:

```
/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/install_name_tool: for: /Users/yourlogin/Documents/opentoonz/toonz/build/toonz/Debug/OpenToonz.app/Contents/MacOS/OpenToonz (for architecture x86_64) option "-add_rpath @executable_path/." would duplicate path, file already has LC_RPATH for: @executable_path/.
/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/install_name_tool: for: /Users/yourlogin/Documents/opentoonz/toonz/build/toonz/Debug/OpenToonz.app/Contents/MacOS/OpenToonz (for architecture x86_64) option "-add_rpath /usr/local/Cellar/qt/5.12.2/lib/" would duplicate path, file already has LC_RPATH for: /usr/local/Cellar/qt/5.12.2/lib/
Command /bin/sh emitted errors but did not return a nonzero exit code to indicate failure
```

Side note: If you want the option to build by command line and Xcode, create a separate build directory for each.

### Create the stuff Directory

If you have installed OpenToonz on the machine already, you can skip this.  Otherwise, you need to create the stuff folder with the following:
```sh
cd ~/Documents/opentoonz
sudo mkdir /Applications/OpenToonz
sudo cp -r stuff /Applications/OpenToonz/OpenToonz_stuff
sudo chmod -R 777 /Applications/OpenToonz
```

### Running the build

- If built using command line, run the following:
```sh
open ~/Documents/opentoonz/build/toonz/OpenToonz.app
```

- If built using Xcode, do the following:

    - Open Scheme editor for OpenToonz: Product -> Scheme -> Edit Scheme
    - Uncheck: Run -> Options -> Document Versions
    - Run in Debug mode: Product -> Run

    - To open with command line or from Finder window, the application is found in `/Users/yourlogin/Documents/opentoonz/toonz/build/Debug/OpenToonz.app`

## Translation workflow / 翻訳

See [Contributing and maintaining translations](../toonz/sources/translations/README.md)
for contributor and maintainer workflows. Keep the Xcode `WITH_TRANSLATION=OFF`
workaround above until translation-enabled Xcode builds are verified. That option
disables this build's translation generation; do not assume bundled catalogs were
updated. Qt Linguist and manual `lrelease` remain available for catalog testing.
