# The showcase: an editor-style app with most of the library in its panels. Its own files
# (showcase.css, icons, the Noto fallback fonts) load from workdirectory/ through
# SetResourceRoot(TACHYON_RESOURCE_DIR), so it runs from any folder.

# The 3D scene's shaders, compiled to SPIR-V and built into the showcase, as gui_core's are.
set(_showcase_glsl "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.vert" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.frag")
set(_showcase_spirv "")
set(_showcase_shader_entries "")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
foreach(_glsl IN LISTS _showcase_glsl)
  get_filename_component(_name "${_glsl}" NAME)
  set(_spv "${CMAKE_CURRENT_BINARY_DIR}/shaders/${_name}.spv")
  add_custom_command(
    OUTPUT "${_spv}"
    COMMAND ${CMAKE_COMMAND} -DVALIDATOR=${GLSL_VALIDATOR} -DSOURCE=${_glsl} -DOUTPUT=${_spv}
            -P "${PROJECT_SOURCE_DIR}/cmake/CompileShader.cmake"
    DEPENDS "${_glsl}"
    COMMENT "Compiling the showcase's ${_name}"
    VERBATIM)
  list(APPEND _showcase_spirv "${_spv}")
  list(APPEND _showcase_shader_entries "${_name}.spv=${_spv}")
endforeach()
set(_showcase_shader_source "${CMAKE_CURRENT_BINARY_DIR}/generated/showcase_shaders.cpp")
string(REPLACE ";" "|" _showcase_shader_arg "${_showcase_shader_entries}")
add_custom_command(
  OUTPUT "${_showcase_shader_source}"
  COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${_showcase_shader_source}" -DFUNCTION=ShowcaseShaderFiles
          "-DFILES=${_showcase_shader_arg}" -P "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
  DEPENDS ${_showcase_spirv} "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
  COMMENT "Building the showcase's shaders in"
  VERBATIM)

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
  scene_renderer.h
  scene_renderer.cpp
  ${_showcase_glsl}
  "${_showcase_shader_source}"

  # The theme files it uses, for editing them in the IDE
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/dark.css"
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/light.css"
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/styles.css"
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/showcase.css")

tachyon_precompile_headers(showcase <gui_controls/gui_application.h>)
