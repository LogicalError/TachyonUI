# Adding the library to a build

TachyonUI is two static libraries:

- `gui_controls`: `GUIApplication` and the controls
- `gui_core`: what they are built on, such as layout, styles, text, and the graphics and
  window backends

Link `TachyonUI::gui_controls`; it brings in `gui_core`. There are two ways to add the
library, below, and both give the same target names.

## As a subproject

A subproject is a copy of the TachyonUI source inside your project, added with
`add_subdirectory`:

```cmake
add_subdirectory(external/TachyonUI)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

- As a subproject, only the libraries build:
  - the showcase, examples, tests and install rules default to off
  - they default to on only when TachyonUI is the top-level project, the folder you ran
    CMake on
- Nothing is written into the TachyonUI source tree.
  - Copies of the resources (themes, fonts, icons) go to `<TachyonUI build dir>/resources`.
- Set options before `add_subdirectory`:

  ```cmake
  set(TACHYON_FALLBACK_FONTS ON)   # also copy the Noto fallback fonts
  add_subdirectory(external/TachyonUI)
  ```

Options (set before `add_subdirectory`):

| Option | Default | |
| --- | --- | --- |
| `TACHYON_BUILD_SHOWCASE` | on when top-level | Builds the showcase (`examples/showcase`). |
| `TACHYON_BUILD_EXAMPLES` | on when top-level | Builds the small examples in `examples/`. |
| `TACHYON_BUILD_TESTS` | on when top-level | Builds the unit and GPU tests. |
| `TACHYON_INSTALL` | on when top-level | Adds install rules and the CMake package. |
| `TACHYON_INSTALL_THIRD_PARTY` | on | Bundles the third-party libraries into the install. |
| `TACHYON_SDL` | on | Builds the SDL window backend and clipboard. Off builds without SDL. |
| `TACHYON_VULKAN` | on (off on macOS) | Builds the Vulkan graphics backend (on macOS through MoltenVK). Off builds without Vulkan (no Vulkan SDK needed): the app runs on another backend built in, or on the null backend, which draws nothing. |
| `TACHYON_METAL` | on (macOS only) | Builds the Metal backend, the default on macOS: the UI in OS and render-to-image windows, and the app's scenes. Needs a Metal 3 GPU and macOS 13.3 or newer; the shaders are built as Metal source and compiled when the app starts, so no Xcode is needed. |
| `TACHYON_D3D12` | off | Windows only. Builds the Direct3D 12 backend: an app picks it with `InitializeOptions::graphicsAPI`. Brings D3D12 Memory Allocator. |
| `TACHYON_SDLGPU` | off | Needs `TACHYON_SDL`. Builds the SDL GPU backend (SDL3's GPU API, over Vulkan, Direct3D 12 or Metal): the UI in OS and render-to-image windows, and the app's scenes. Needs no Vulkan SDK: without one it takes the Vulkan headers SDL carries. |
| `TACHYON_GPU_TESTS_ON_D3D12` | off | With `TACHYON_D3D12`: runs the whole-app GPU tests (`gui_gpu_tests`) a second time, on Direct3D 12, as `D3D12.<test>`. |
| `TACHYON_GPU_TESTS_ON_SDLGPU` | off | With `TACHYON_SDLGPU`: runs the whole-app GPU tests a second time, on SDL GPU, as `SDLGPU.<test>`. SDL picks the driver; set `SDL_GPU_DRIVER` to pick another. |
| `TACHYON_SYSTEM_SDL` | off | Uses the host's SDL3. |
| `TACHYON_SYSTEM_HARFBUZZ` | off | Uses the host's HarfBuzz. |
| `TACHYON_SYSTEM_VMA` | off | Uses the host's VulkanMemoryAllocator. |
| `TACHYON_VMA_IMPLEMENTATION` | on | Compiles VMA's implementation into `gui_core`. Turn off if the host links its own. |
| `TACHYON_FALLBACK_FONTS` | off | Copies (and installs) the Noto fallback fonts with the resources. |
| `TACHYON_EMBED_RESOURCES` | on | Builds the default themes and fonts into `gui_core` (about 3 MB). When off, every program (examples included) must call `SetResourceRoot` with a folder containing `themes/` and `fonts/`. |

- The `TACHYON_SYSTEM_*` options use the host's copy of a library instead of the bundled
  one. CMake looks for it in this order:
  - a target the host already has: `SDL3::SDL3`, `harfbuzz::harfbuzz` or
    `GPUOpen::VulkanMemoryAllocator`
  - else whatever `find_package` finds

```cmake
find_package(SDL3 CONFIG REQUIRED)
set(TACHYON_SYSTEM_SDL      ON)
set(TACHYON_SYSTEM_HARFBUZZ ON)
add_subdirectory(external/TachyonUI)
```

- Code can check which graphics backends were built in:
  - `gui_core` defines `TACHYON_VULKAN`, `TACHYON_D3D12`, `TACHYON_METAL` and `TACHYON_SDLGPU`,
    each `0` or `1`
  - for example, `#if TACHYON_VULKAN` around code that uses `VulkanGraphicsContext`
  - the CMake package sets `TachyonUI_VULKAN`, `TachyonUI_D3D12`, `TachyonUI_METAL` and
    `TachyonUI_SDLGPU` the same way

### Choosing the graphics backend

With more than one backend built in, an app picks one when it starts:

- Set `graphicsAPI` in the options you pass to `Initialize`:
  - empty, the default: Metal on macOS, Vulkan elsewhere
  - `GraphicsAPI::Vulkan`: Vulkan (on macOS through MoltenVK, with `TACHYON_VULKAN` on)
  - `GraphicsAPI::D3D12`: Direct3D 12
  - `GraphicsAPI::Metal`: Metal, on macOS
  - `GraphicsAPI::SDLGPU`: SDL3's GPU API, on the driver SDL picks (`SDLGPUGraphicsContext::SetDriver`
    picks one: `"vulkan"`, `"direct3d12"` or `"metal"`)
  - `GraphicsAPI::None`: no GPU, the null backend, which draws nothing
- A backend that isn't built in makes `Initialize` fail, naming the option that builds it.
  - For example, `D3D12` without `TACHYON_D3D12`: "Direct3D 12 isn't built in: build with
    TACHYON_D3D12 on".
- A backend whose device can't start gives way to the default one, with a warning in the log.
  - For example, Direct3D 12 needs Shader Model 6.6, which Windows 11's D3D12 runtime has.
- A backend set with `SetGraphicsBackend` takes the place of the option.

```cpp
GUIApplication::InitializeOptions options;
options.graphicsAPI = GraphicsAPI::D3D12;
GUIApplication::Initialize(options);
```

- The bundled SDL3 is a static library.
- If your SDL3 is a DLL instead, on Windows it must sit next to the executable, for
  example through a copy after each build:

```cmake
add_custom_command(TARGET my_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:my_app> $<TARGET_FILE_DIR:my_app>
  COMMAND_EXPAND_LISTS)
```

- A minimal host project: `tests/subproject_consumer`.
  - With `CONSUMER_HOST_DEPENDENCIES=ON` it brings its own SDL3, HarfBuzz and VMA.

## As an installed package

- `cmake --install` produces a package that has everything a program needs:
  - both libraries and the public headers
  - the resources, if enabled
  - the bundled third-party libraries and their licenses
  - a CMake package config, for `find_package`
- Debug and Release can be installed into the same prefix:

```text
cmake --install build --config Debug   --prefix <prefix>
cmake --install build --config Release --prefix <prefix>
```

A program using the package then needs no TachyonUI source or build tree:

```cmake
find_package(TachyonUI 1.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

- Its dependencies (Vulkan, SDL3, HarfBuzz and the other bundled libraries) are looked up
  in the same prefix.
- The program must use the toolchain the package was built with.
- Only Debug and Release are installed. Other configurations need
  `CMAKE_MAP_IMPORTED_CONFIG_<CONFIG>`:

  ```cmake
  set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
  set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL     Release)
  find_package(TachyonUI 1.1 CONFIG REQUIRED)
  ```

- A minimal program using the package: `tests/package_consumer`.

## Resources

Resources are the files the library reads besides your code: shaders, themes, fonts and
icons.

- No files are needed at run time. Built into the library are:
  - the Vulkan backend's shaders, always
  - the default themes (`dark.css`, `light.css`, `styles.css`) and the Inter and
    JetBrains Mono fonts, unless `TACHYON_EMBED_RESOURCES` is off
- The resource root is the folder files on disk load from.
  - Set it with `GUIApplication::SetResourceRoot(folder)`, before `Initialize`.
  - Relative theme and font paths are relative to it.
  - A file there replaces the built-in one of the same name, for example a modified
    `themes/styles.css`.

```cpp
app.SetResourceRoot(TACHYON_RESOURCE_DIR);   // before Initialize
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
```

| Folder | Contents |
| --- | --- |
| `themes/` | Your palettes and style files (see `SetStyleFiles`). |
| `fonts/` | Fonts your style files name in `@font-face`. |
| `fonts/Noto-otf/` | Optional fallback fonts for other scripts, emoji and symbols, loaded with `InitializeOptions::fallbackFonts`. |
| `icons/` | The showcase's SVG icons. Free to use, not required. |

The fallback fonts load when `Initialize` gets their folder, relative to the resource
root:

```cpp
InitializeOptions withFallbacks = options;
withFallbacks.fallbackFonts = "fonts/Noto-otf";
if (tachyon::Result<void> result = GUIApplication::Initialize(withFallbacks); !result)
    return result;
```

Linking `gui_controls` defines `TACHYON_RESOURCE_DIR`, the folder with copies of these
files:

| Build | Folder |
| --- | --- |
| TachyonUI built on its own | `workdirectory/` in the source tree |
| As a subproject | `resources/` in TachyonUI's build directory |
| Installed package | `share/TachyonUI/resources` in the prefix |

- CMake code using the package gets the same folder as `TachyonUI_RESOURCE_DIR`.
- Use it, for example, to copy the fallback fonts next to a program you ship. The program
  then sets its resource root to its own folder.

```cmake
add_custom_command(TARGET my_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${TachyonUI_RESOURCE_DIR}/fonts/Noto-otf $<TARGET_FILE_DIR:my_app>/fonts/Noto-otf)
```

- A custom graphics backend brings its own shaders:
  - a graphics backend is the part that draws; the default one uses Vulkan
  - the library only hands it primitives, such as boxes and glyphs
  - see `PrimitiveSpec` in `gui_core/graphics/gui_render_primitives.h`
  - the shaders are in `workdirectory/shaders`
    - they're written in [Slang](https://shader-slang.org/): one `*_shader.slang` file per
      primitive kind, with its `vertexMain` and `fragmentMain`
    - each backend gets its own build of them: SPIR-V for Vulkan, DXIL for Direct3D 12, Metal
      Shading Language source for Metal (`.metal`, compiled when the app starts), and SDL GPU's
      own builds with the resources where SDL binds them (`.sdl.spv`, `.sdl.dxil`, `.sdl.metal`)
    - for example, `box_shader.slang` builds into `box_shader.vert.spv` and
      `box_shader.frag.spv`, and with `TACHYON_D3D12` also `box_shader.vert.dxil` and
      `box_shader.frag.dxil`
    - `TACHYON_D3D12` and `TACHYON_METAL` pick those APIs' bindings in them (`all_common.slang`,
      `sdf_color.slang`)

---

[Integration](../integration.md) | [Next: Using your own Vulkan device](vulkan-device.md)
