# The showcase: an editor-style app with most of the library in its panels. Its own files
# (showcase.css, icons, the Noto fallback fonts) load from workdirectory/ through
# SetResourceRoot(TACHYON_RESOURCE_DIR), so it runs from any folder.

# The 3D scene's renderer and shaders, with the Vulkan or Direct3D 12 backend: the shaders
# compiled to SPIR-V and DXIL as the backends built in need, and built into the showcase, as
# gui_core's are. Without either the Viewport panel has no scene drawn.
set(_showcase_scene_files "")
if(TACHYON_VULKAN OR TACHYON_D3D12)
  set(_showcase_slang "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.slang")
  set(_showcase_shaders "")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  if(TACHYON_VULKAN)
    tachyon_compile_slang("${_showcase_slang}" "${CMAKE_CURRENT_BINARY_DIR}/shaders" _showcase_shaders spirv)
  endif()
  if(TACHYON_D3D12)
    tachyon_compile_slang("${_showcase_slang}" "${CMAKE_CURRENT_BINARY_DIR}/shaders" _showcase_shaders dxil)
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
if(TACHYON_D3D12)
  # The scene renderer's root signature (D3D12SerializeRootSignature).
  target_link_libraries(showcase PRIVATE d3d12)
endif()
