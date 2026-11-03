# 1. Set up the project

## The folder

Make a folder for your app and add TachyonUI to it as a Git submodule:

```text
mkdir my_app
cd my_app
git init
git submodule add https://github.com/LogicalError/TachyonUI.git external/TachyonUI
git submodule update --init --recursive
```

`--recursive` also fetches TachyonUI's own dependencies (SDL, HarfBuzz and the rest), so
the build doesn't need a network connection. Make sure Git LFS is installed before you
run this, because the fonts are stored in LFS.

Your app is three source files plus a `CMakeLists.txt`, all next to `external/`:

```text
my_app/
  CMakeLists.txt
  my_app.h        the panels and the app class
  my_app.cpp      what the panels and the app draw
  main.cpp
  external/TachyonUI/
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(my_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(external/TachyonUI)

add_executable(my_app main.cpp my_app.cpp my_app.h)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

That's all the setup you need. The shaders, the default theme and the fonts are built
into the library, and SDL is linked in, so your program doesn't need any files or DLLs
next to it. When TachyonUI is a subproject, it only builds its libraries, not its
examples and tests.

If you'd rather use an installed TachyonUI than a submodule, replace the
`add_subdirectory` line with `find_package(TachyonUI 0.1 CONFIG REQUIRED)`.
[Adding the library to a build](../integration/build.md) has the details.

## Building

```text
cmake -S . -B build
cmake --build build --config Debug
```

On Linux and macOS, add `-G "Ninja Multi-Config"` to the first line. The first configure
builds TachyonUI's third-party libraries, which takes a few minutes, but it only happens
once.

The build won't succeed until the three source files exist. The next steps write them.

---

[Tutorial](../tutorial.md) | [Next: 2. Buttons](buttons.md)
