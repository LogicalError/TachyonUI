# Adding the library to a build

Two static libraries, `gui_core` and `gui_controls`. Link `TachyonUI::gui_controls`; it
brings in `gui_core`. Both methods below give the same target names.

## As a subproject

```cmake
add_subdirectory(external/TachyonUI)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

As a subproject only the libraries build; showcase, examples, tests and install rules
default to off. Nothing is written into the TachyonUI source tree; resource copies
(themes, fonts, icons) go to `<TachyonUI build dir>/resources`.

Options (set before `add_subdirectory`):

| Option | Default | |
| --- | --- | --- |
| `TACHYON_BUILD_SHOWCASE` | on when top-level | Builds the showcase (`examples/showcase`). |
| `TACHYON_BUILD_EXAMPLES` | on when top-level | Builds the small examples in `examples/`. |
| `TACHYON_BUILD_TESTS` | on when top-level | Builds the unit and GPU tests. |
| `TACHYON_INSTALL` | on when top-level | Adds install rules and the CMake package. |
| `TACHYON_INSTALL_THIRD_PARTY` | on | Bundles the third-party libraries into the install. |
| `TACHYON_SDL` | on | Builds the SDL window backend and clipboard. Off builds without SDL. |
| `TACHYON_SYSTEM_SDL` | off | Uses the host's SDL3. |
| `TACHYON_SYSTEM_HARFBUZZ` | off | Uses the host's HarfBuzz. |
| `TACHYON_SYSTEM_VMA` | off | Uses the host's VulkanMemoryAllocator. |
| `TACHYON_VMA_IMPLEMENTATION` | on | Compiles VMA's implementation into `gui_core`. Turn off if the host links its own. |
| `TACHYON_FALLBACK_FONTS` | off | Copies (and installs) the Noto fallback fonts with the resources. |
| `TACHYON_EMBED_RESOURCES` | on | Builds the default themes and fonts into `gui_core` (about 3 MB). When off, every program (examples included) must call `SetResourceRoot` with a folder containing `themes/` and `fonts/`. |

`TACHYON_SYSTEM_*` uses the host's existing target (`SDL3::SDL3`, `harfbuzz::harfbuzz`,
`GPUOpen::VulkanMemoryAllocator`), else whatever `find_package` finds.

```cmake
find_package(SDL3 CONFIG REQUIRED)
set(TACHYON_SYSTEM_SDL      ON)
set(TACHYON_SYSTEM_HARFBUZZ ON)
add_subdirectory(external/TachyonUI)
```

The bundled SDL3 is static. If yours is a DLL, on Windows it must sit next to the
executable, e.g. via a post-build copy:

```cmake
add_custom_command(TARGET my_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:my_app> $<TARGET_FILE_DIR:my_app>
  COMMAND_EXPAND_LISTS)
```

Minimal host: `tests/subproject_consumer` (`CONSUMER_HOST_DEPENDENCIES=ON` brings its own
SDL3, HarfBuzz and VMA).

## As an installed package

`cmake --install` produces a self-contained package: both libraries, public headers,
optional resources, bundled third-party libraries, licenses and a CMake package config.
Debug and Release can share a prefix:

```text
cmake --install build --config Debug   --prefix <prefix>
cmake --install build --config Release --prefix <prefix>
```

The consumer then needs no TachyonUI source or build tree:

```cmake
find_package(TachyonUI 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

- Dependencies (Vulkan, SDL3, HarfBuzz, other bundled libraries) are looked up in the same
  prefix.
- The consumer must use the package's toolchain.
- Only Debug and Release are installed; other configurations need
  `CMAKE_MAP_IMPORTED_CONFIG_<CONFIG>`.
- Minimal consumer: `tests/package_consumer`.

## Resources

No files are needed at run time. Built into the library:

- the Vulkan backend's shaders, always;
- the default themes (`dark.css`, `light.css`, `styles.css`) and the Inter and
  JetBrains Mono fonts, unless `TACHYON_EMBED_RESOURCES` is off.

`GUIApplication::SetResourceRoot(folder)`, before `Initialize`, sets where files on disk
load from. Relative theme and font paths resolve against it. A file there replaces the
built-in one of the same name (e.g. a modified `themes/styles.css`).

| Folder | Contents |
| --- | --- |
| `themes/` | Your palettes and style files (see `SetStyleFiles`). |
| `fonts/` | Fonts your style files name in `@font-face`. |
| `fonts/Noto-otf/` | Optional fallback fonts for other scripts, emoji and symbols, loaded with `InitializeOptions::fallbackFonts`. |
| `icons/` | The showcase's SVG icons. Free to use, not required. |

Linking `gui_controls` defines `TACHYON_RESOURCE_DIR`, the folder with copies of these:

| Build | Folder |
| --- | --- |
| TachyonUI built on its own | `workdirectory/` in the source tree |
| As a subproject | `resources/` in TachyonUI's build directory |
| Installed package | `share/TachyonUI/resources` in the prefix |

CMake code using the package gets the same folder as `TachyonUI_RESOURCE_DIR`, e.g. to
copy fallback fonts next to a shipped program (which then sets its resource root there).

A custom graphics backend brings its own shaders; the library only hands it primitives.
See `PrimitiveSpec` in `gui_core/graphics/gui_render_primitives.h`, and the Vulkan
backend's GLSL in `workdirectory/shaders`.

---

[Integration](../integration.md) | [Next: Using your own Vulkan device](vulkan-device.md)
