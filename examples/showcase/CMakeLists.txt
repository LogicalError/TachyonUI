# The showcase: an editor-style app with most of the library in its panels. Its own files
# (showcase.css, icons, the Noto fallback fonts) load from workdirectory/ through
# SetResourceRoot(TACHYON_RESOURCE_DIR), so it runs from any folder.

# The 3D scene's renderer and shaders, with the Vulkan, Direct3D 12, Metal or SDL GPU backend:
# the shaders compiled to SPIR-V, DXIL and Metal source as the backends built in need, and built
# into the showcase, as gui_core's are. Without any the Viewport panel has no scene drawn.
set(_showcase_scene_files "")
if(TACHYON_VULKAN OR TACHYON_D3D12 OR TACHYON_SDLGPU OR TACHYON_METAL)
  set(_showcase_slang "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.slang")
  set(_showcase_shaders "")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  if(TACHYON_VULKAN)
    tachyon_compile_slang("${_showcase_slang}" "${CMAKE_CURRENT_BINARY_DIR}/shaders" _showcase_shaders spirv)
  endif()
  if(TACHYON_D3D12)
    tachyon_compile_slang("${_showcase_slang}" "${CMAKE_CURRENT_BINARY_DIR}/shaders" _showcase_shaders dxil)
  endif()
  if(TACHYON_METAL)
    tachyon_compile_slang("${_showcase_slang}" "${CMAKE_CURRENT_BINARY_DIR}/shaders" _showcase_shaders metal)
  endif()
  if(TACHYON_SDLGPU)
    tachyon_compile_slang("${_showcase_slang}" "${CMAKE_CURRENT_BINARY_DIR}/shaders" _showcase_shaders sdl-spirv)
    if(WIN32)
      tachyon_compile_slang("${_showcase_slang}" "${CMAKE_CURRENT_BINARY_DIR}/shaders" _showcase_shaders sdl-dxil)
    endif()
  endif()
  set(_showcase_shader_entries "")
  foreach(_shader IN LISTS _showcase_shaders)
    get_filename_component(_name "${_shader}" NAME)
    list(APPEND _showcase_shader_entries "${_name}=${_shader}")
  endforeach()
  set(_showcase_shader_source "${CMAKE_CURRENT_BINARY_DIR}/generated/showcase_shaders.cpp")
  string(REPLACE ";" "|" _showcase_shader_arg "${_showcase_shader_entries}")
  add_custom_command(
    OUTPUT "${_showcase_shader_source}"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${_showcase_shader_source}" -DFUNCTION=ShowcaseShaderFiles
            "-DFILES=${_showcase_shader_arg}" -P "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    DEPENDS ${_showcase_shaders} "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    COMMENT "Building the showcase's shaders in"
    VERBATIM)
  set(_showcase_scene_files scene_renderer.h scene_renderer.cpp "${_showcase_slang}" "${_showcase_shader_source}")
  if(TACHYON_METAL)
    # Its Metal path is Objective-C++.
    set_source_files_properties(scene_renderer.cpp PROPERTIES LANGUAGE OBJCXX)
  endif()
endif()

tachyon_add_example(showcase
  main.cpp
  application.h
  application.cpp
  panel_box_test.cpp
  panel_console.cpp
  panel_files.cpp
  panel_hierarchy.cpp
  panel_inspector.cpp
  panel_misc.cpp
  progress_demos.cpp
  panel_node_view.cpp
  panel_state_machine.cpp
  capture.cpp
  panel_profiler_tree.cpp
  panel_reorderable.cpp
  panel_settings.cpp
  panel_sketch.cpp
  panel_split_scroll.cpp
  panel_table.cpp
  settings_theme.cpp
  panel_timeline.cpp
  panel_tree_table.cpp
  panel_view.cpp
  scene.h
  scene.cpp
  ${_showcase_scene_files}

  # The theme files it uses, for editing them in the IDE
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/dark.css"
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/light.css"
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/styles.css"
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/showcase.css")

tachyon_precompile_headers(showcase <gui_controls/gui_application.h>)
if(TACHYON_METAL)
  # For the whole target, so the Objective-C++ precompiled header matches scene_renderer.cpp.
  target_compile_options(showcase PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif()
if(TACHYON_D3D12)
  # The scene renderer's root signature (D3D12SerializeRootSignature).
  target_link_libraries(showcase PRIVATE d3d12)
endif()
