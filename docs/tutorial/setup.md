# 1. Set up the project

## The folder

Install Git LFS first; the fonts are stored in LFS. Then add TachyonUI as a submodule:

```text
mkdir my_app
cd my_app
git init
git submodule add https://github.com/LogicalError/TachyonUI.git external/TachyonUI
git submodule update --init --recursive
```

`--recursive` fetches TachyonUI's dependencies, such as SDL and HarfBuzz, so the build
needs no network.

The project's layout:

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

- Shaders, theme and fonts are built in, and SDL is linked in. No files or DLLs go next to
  the program.
- As a subproject, TachyonUI builds only its libraries, not its examples or tests.
- For an installed TachyonUI, replace `add_subdirectory` with
  `find_package(TachyonUI 0.1 CONFIG REQUIRED)`; see
  [Adding the library to a build](../integration/build.md).

## Building

```text
cmake -S . -B build
cmake --build build --config Debug
```

- On Linux and macOS, add `-G "Ninja Multi-Config"` to the first line.
- The first configure builds the third-party libraries. This takes a few minutes, once.
- The build fails until the three source files exist; the next steps write them.

---

[Tutorial](../tutorial.md) | [Next: 2. Buttons](buttons.md)
