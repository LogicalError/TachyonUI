# Adding the library to a build

TachyonUI is made up of two static libraries, `gui_core` and `gui_controls`. Your
program links `gui_controls`, and that brings in `gui_core` with it. Both of the ways
below give you the same target names.

## As a subproject

```cmake
add_subdirectory(external/TachyonUI)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

When TachyonUI is a subproject, only the libraries get built. The showcase, the
examples, the tests and the install rules are off by default. Nothing is written into
the TachyonUI source tree. Copies of the optional resources (themes, fonts and icons)
go into the build tree instead, in `<TachyonUI build dir>/resources`.

You can set these options before `add_subdirectory`:

| Option | Default | |
| --- | --- | --- |
| `TACHYON_BUILD_SHOWCASE` | on when top-level | Builds the showcase application (`examples/showcase`). |
| `TACHYON_BUILD_EXAMPLES` | on when top-level | Builds the small examples in `examples/`. |
| `TACHYON_BUILD_TESTS` | on when top-level | Builds the unit and GPU tests. |
| `TACHYON_INSTALL` | on when top-level | Adds the install rules and the CMake package. |
| `TACHYON_INSTALL_THIRD_PARTY` | on | Bundles the third-party libraries into the install. |
| `TACHYON_SDL` | on | Builds the SDL window backend and clipboard. Turn it off to build without SDL. |
| `TACHYON_SYSTEM_SDL` | off | Uses the host's SDL3. |
| `TACHYON_SYSTEM_HARFBUZZ` | off | Uses the host's HarfBuzz. |
| `TACHYON_SYSTEM_VMA` | off | Uses the host's VulkanMemoryAllocator. |
| `TACHYON_VMA_IMPLEMENTATION` | on | Compiles VMA's implementation into `gui_core`. Turn it off if the host links its own. |
| `TACHYON_FALLBACK_FONTS` | off | Copies (and installs) the Noto fallback fonts along with the resources. |
| `TACHYON_EMBED_RESOURCES` | on | Builds the default themes and fonts into `gui_core` (about 3 MB). When it's off, they're only loaded from the resource root, so every program, the examples included, has to call `SetResourceRoot` with a folder that contains `themes/` and `fonts/`. |

Each `TACHYON_SYSTEM_*` option uses a target the host has already defined
(`SDL3::SDL3`, `harfbuzz::harfbuzz` or `GPUOpen::VulkanMemoryAllocator`). If there
isn't one, it uses whatever `find_package` finds.

```cmake
find_package(SDL3 CONFIG REQUIRED)
set(TACHYON_SYSTEM_SDL      ON)
set(TACHYON_SYSTEM_HARFBUZZ ON)
add_subdirectory(external/TachyonUI)
```

The bundled SDL3 is a static library, so there's no SDL DLL for you to ship. Your own
SDL3 might be a DLL, though. On Windows it then has to sit next to the executable, and
a post-build step can copy it there:

```cmake
add_custom_command(TARGET my_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:my_app> $<TARGET_FILE_DIR:my_app>
  COMMAND_EXPAND_LISTS)
```

For a minimal host, look at `tests/subproject_consumer`. With
`CONSUMER_HOST_DEPENDENCIES=ON` it brings its own SDL3, HarfBuzz and VMA.

## As an installed package

`cmake --install` produces a self-contained package. It holds both libraries, their
public headers, the optional resources, the bundled third-party libraries, the license
files and a CMake package configuration. You can install Debug and Release side by side
into the same prefix:

```text
cmake --install build --config Debug   --prefix <prefix>
cmake --install build --config Release --prefix <prefix>
```

After that, a consumer doesn't need the TachyonUI source or build tree at all:

```cmake
find_package(TachyonUI 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

The package looks for its dependencies (Vulkan, SDL3, HarfBuzz and the other bundled
libraries) in the same prefix. The consumer has to use the same toolchain as the
package. Only Debug and Release are installed, so for any other configuration the
consumer needs `CMAKE_MAP_IMPORTED_CONFIG_<CONFIG>`. For a minimal consumer, see
`tests/package_consumer`.

## Resources

Your program doesn't need any files. Linking `gui_controls` is enough, because these are
built into the library:

- the Vulkan backend's shaders, always;
- the default themes (`dark.css`, `light.css` and `styles.css`) and the Inter and
  JetBrains Mono fonts, unless you build with `TACHYON_EMBED_RESOURCES` off.

Files on disk are optional. Call `GUIApplication::SetResourceRoot(folder)` before
`Initialize` to say where they load from. Relative theme and font paths are resolved
against that folder. A file there replaces the built-in file with the same name, so a
modified `themes/styles.css` takes over from the built-in one. Anything else in the
folder is your own:

| Folder | Contents |
| --- | --- |
| `themes/` | Your own palettes and style files (see `SetStyleFiles`). |
| `fonts/` | Fonts that your style files name in `@font-face`. |
| `fonts/Noto-otf/` | Optional fallback fonts for other scripts, emoji and symbols, loaded with `InitializeOptions::fallbackFonts`. |
| `icons/` | The showcase's SVG icons. You're welcome to use them, but you don't need them. |

Linking `gui_controls` defines `TACHYON_RESOURCE_DIR`, the folder that holds copies of
these for that build or package:

| Build | Folder |
| --- | --- |
| TachyonUI built on its own | `workdirectory/` in the source tree |
| As a subproject | `resources/` in TachyonUI's build directory |
| Installed package | `share/TachyonUI/resources` in the prefix |

CMake code that uses the package gets the same folder as `TachyonUI_RESOURCE_DIR`. You
can use it to copy the fallback fonts next to a program you ship, for example. The
program then sets its resource root to wherever you put them.

A custom graphics backend brings its own shaders. The library only hands it the
primitives to draw. See `PrimitiveSpec` in `gui_core/graphics/gui_render_primitives.h`,
and the GLSL in `workdirectory/shaders` for the Vulkan backend's version.

---

[Integration](../integration.md) | [Next: Using your own Vulkan device](vulkan-device.md)
