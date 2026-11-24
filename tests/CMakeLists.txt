list(APPEND GUI_CORE_TEST_FILES
  gui_text_system_stub.h
  controls_basic_test.cpp
  dictionary_field_test.cpp
  skribidi_text_measurement_test.cpp
  skribidi_glyph_quad_test.cpp
  controls_grid_view_test.cpp
  file_system_test.cpp
  file_thumbnails_test.cpp
  controls_menu_test.cpp
  controls_settings_test.cpp
  transition_manager_test.cpp
  layered_fade_test.cpp
  controls_scrollbar_test.cpp
  controls_slider_integration_test.cpp
  controls_tabs_test.cpp
  controls_tooltip_test.cpp
  controls_tree_list_test.cpp
  default_handlers_test.cpp
  tracking_allocator_test.cpp
  library_allocator_test.cpp
  progress_test.cpp
  progress_composites_test.cpp
  node_view_frame_test.cpp
  node_view_state_machine_test.cpp
  timeline_test.cpp
  timeline_editor_test.cpp
  theme_variables_test.cpp
  box_fields_test.cpp
  curve_test.cpp
  curve_field_test.cpp
  curve_editor_test.cpp
  accent_test.cpp
  null_backends_test.cpp
  redraw_test.cpp
  breadcrumb_test.cpp
  node_view_zoom_test.cpp
  vector_field_test.cpp
  command_search_test.cpp
  svg_gradient_test.cpp
  svg_stroke_test.cpp
  title_bar_test.cpp
  theme_metrics_test.cpp
  table_row_heights_test.cpp
  switch_test.cpp
  number_field_edit_test.cpp
  color_field_hex_test.cpp
  gradient_editor_dialog_test.cpp
  message_dialog_test.cpp
  undo_history_test.cpp
  misuse_test.cpp
  begin_end_test.cpp
  id_stability_test.cpp
  control_reporting_test.cpp
  frame_cost_bench.cpp
  frame_alloc_bench.cpp
  units_test.cpp
  result_test.cpp
  measured_sizes_test.cpp
  ui_state_test.cpp
  gpu_choice_test.cpp
  view_state_test.cpp
  layout_presets_test.cpp
  clipboard_image_test.cpp
  gradient_float_test.cpp
  icon_names_test.cpp
  fuzz_test.cpp
  node_view_drop_test.cpp
  node_view_lane_offset_test.cpp
  node_view_wire_edit_test.cpp
  node_view_edit_test.cpp
  edit_id_release_test.cpp
  timeline_internal_test.cpp
  popup_escape_test.cpp
  label_group_alignment_test.cpp
  drag_drop_test.cpp
  file_drop_test.cpp
  in_app_drag_test.cpp
  pen_input_test.cpp
  plot_test.cpp
  navigation_test.cpp
  math3d_test.cpp
  image_batcher_test.cpp
  viewport_test.cpp
  harness_self_test.cpp
  frame_pipeline_test.cpp
  frame_clock_test.cpp
  ui_scale_test.cpp
  version_test.cpp
  profiler_test.cpp
  subsystem_test.cpp
  input_manager_test.cpp
  command_manager_test.cpp
  dock_node_test.cpp
  element_storage_test.cpp
  gui_base_types_test.cpp
  color_conversions_test.cpp
  gradient_manager_test.cpp
  image_set_text_atlas_test.cpp
  gradient_test.cpp
  icon_text_layout_test.cpp
  icon_text_placement_test.cpp
  id_manager_test.cpp
  layout_engine_test.cpp
  logging_test.cpp
  layout_flex_test.cpp
  layout_grid_test.cpp
  layout_text_wrapping_test.cpp
  os_window_manager_test.cpp
  image_window_backend_test.cpp
  label_handler_test.cpp
  numeric_field_format_test.cpp
  reorderable_focus_test.cpp
  controls_toolbar_dock_test.cpp
  sizer_handler_test.cpp
  sizer_render_test.cpp
  slider_handler_test.cpp
  state_manager_focus_test.cpp
  tab_chain_manager_test.cpp
  focus_ring_test.cpp
  from_chars_test.cpp
  state_manager_test.cpp
  style_geometry_test.cpp
  style_manager_test.cpp
  style_sheet_test.cpp
  table_helpers_test.cpp
  table_integration_test.cpp
  tree_utils_test.cpp
  controls_reorderable_test.cpp
  color_field_test.cpp
  controls_docking_test.cpp
  static_id_release_test.cpp
  controls_documents_test.cpp
  node_view_two_views_test.cpp
  timeline_two_views_test.cpp
  curve_plot_two_views_test.cpp
  controls_notifications_test.cpp
  skribidi_shaping_test.cpp
  font_set_test.cpp
  built_in_resources_test.cpp
  resource_path_case_test.cpp
  controls_dropdown_test.cpp
  controls_autocomplete_test.cpp
  keyboard_focus_test.cpp
  tutorial_test.cpp
  tutorial_snippets_test.cpp
  skribidi_editor_test.cpp
  text_shadow_test.cpp
  text_paint_test.cpp
  pointer_drag_test.cpp
  gesture_test.cpp
  link_cursor_test.cpp
  table_scrollbar_test.cpp
  controls_table_frozen_test.cpp
  controls_table_cells_test.cpp
  controls_table_virtual_test.cpp
  node_view_control_test.cpp
  node_view_stress_test.cpp
  node_view_hit_test.cpp
  node_view_marquee_test.cpp
  node_view_wire_route_test.cpp
  headless_gui_app.h
  misuse_reports.h
  scoped_temp_dir.h
  fake_clock.h
  skribidi_test_fixture.h
  headless_gui_app.cpp
  mock_window_backend.h
  window_registry_test.cpp
  text_editor_coordinate_test.cpp
  ui_cache_test.cpp
  primitive_cache_test.cpp
  primitive_batching_test.cpp
  id_map_test.cpp
  pixel_snap_test.cpp
  main.cpp
)

# The Vulkan backend's own parts, without a device.
if(TACHYON_VULKAN)
  list(APPEND GUI_CORE_TEST_FILES
    upload_worker_test.cpp
    frame_waits_test.cpp)
endif()

add_executable(gui_core_tests ${GUI_CORE_TEST_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${GUI_CORE_TEST_FILES})
# The tutorial's app (docs/tutorial.md), whose panels tutorial_test.cpp drives.
set(TACHYON_TUTORIAL_SOURCES "${PROJECT_SOURCE_DIR}/examples/tutorial/my_app.cpp" "${PROJECT_SOURCE_DIR}/examples/tutorial/my_app.h")
target_sources(gui_core_tests PRIVATE ${TACHYON_TUTORIAL_SOURCES})
source_group("Tutorial" FILES ${TACHYON_TUTORIAL_SOURCES})
# resource_path_case_test.cpp checks the paths the source names against workdirectory/.
target_compile_definitions(gui_core_tests PRIVATE "TACHYON_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
# frame_alloc_bench.cpp counts every allocation by replacing operator new for the whole
# executable: built in only when asked for.
option(TACHYON_FRAME_ALLOC_BENCH "Build the per-frame allocation bench into gui_core_tests" OFF)
target_compile_definitions(gui_core_tests PRIVATE TACHYON_FRAME_ALLOC_BENCH=$<BOOL:${TACHYON_FRAME_ALLOC_BENCH}>)

# gui_controls gives us the widget layer (GUIApplication, BeginTable, etc.)
# that integration tests drive through the headless harness. It transitively
# exposes gui_core, so unit tests that only touch the lower layer still work.
target_link_libraries(gui_core_tests PRIVATE gui_controls GTest::gtest_main GTest::gmock)

# Match gui_core/gui_controls so /GL objects don't force a linker restart.
tachyon_apply_compile_options(gui_core_tests)
tachyon_precompile_headers(gui_core_tests <gtest/gtest.h> <gui_controls/gui_application.h>)

# gmock's templated actions trip a benign C4702 (unreachable code). With /GL
# the test exe is code-generated at LINK time (/LTCG), so that warning is
# emitted during whole-program codegen - past the point where /external:W0 (a
# front-end, per-include mechanism) can see it. Disabling C4702 on the test
# target is what actually reaches the LTCG phase (the flag rides along in the
# /GL objects); the only thing lost is unreachable-code detection in test code.
if(MSVC)
  target_compile_options(gui_core_tests PRIVATE /wd4702)
endif()

include(GoogleTest)


# ctest lists the tests when it runs them, not at build time, so building never
# launches the executables (slow on a cold CI runner). It runs in the build folder
# (discovery leaves a JSON file behind); main.cpp switches to workdirectory itself.
tachyon_copy_runtime_dlls(gui_core_tests)
gtest_discover_tests(gui_core_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)

set_target_properties(gui_core_tests PROPERTIES
  FOLDER "Tests"
  VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/workdirectory"
)


# Tests on a real GPU, in two programs. They drive real windows through SDL, and SKIP
# themselves when no suitable device or display is present, so they're safe to build
# everywhere.
#   gui_gpu_tests: whole apps, on the default graphics backend (CreateDefaultGraphicsContext).
#   gui_vulkan_tests: the Vulkan backend itself. These boot a VulkanGraphicsContext with the
#     validation and synchronization-validation layers recording into a sink, so backend bugs
#     (descriptor overflow, sync hazards, leaks) become test failures.
#   gui_d3d12_tests: the Direct3D 12 backend itself, its debug layer's messages failing them.

# tachyon_add_gpu_tests(<target> <sources...>): a GPU test program.
function(tachyon_add_gpu_tests target)
  set(_files ${ARGN} gpu/gpu_crash_trace.cpp main.cpp)
  add_executable(${target} ${_files})
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_files})
  target_link_libraries(${target} PRIVATE gui_controls ${TACHYON_SDL_TARGET} GTest::gtest_main GTest::gmock)
  # The bundled SDL is a static library; a host's SDL (TACHYON_SYSTEM_SDL) may be a DLL.
  target_compile_definitions(${target} PRIVATE TACHYON_TEST_BUNDLED_SDL=$<NOT:$<BOOL:${TACHYON_SYSTEM_SDL}>>)
  tachyon_apply_compile_options(${target})
  tachyon_precompile_headers(${target} <gtest/gtest.h> <gui_controls/gui_application.h>)
  # Same gmock C4702-at-LTCG situation as gui_core_tests above.
  if(MSVC)
    target_compile_options(${target} PRIVATE /wd4702)
  endif()
  tachyon_copy_runtime_dlls(${target})
  gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
  set_target_properties(${target} PROPERTIES
    FOLDER "Tests"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/workdirectory"
  )
endfunction()

if(TACHYON_SDL AND (TACHYON_VULKAN OR TACHYON_D3D12 OR TACHYON_METAL))
  tachyon_add_gpu_tests(gui_gpu_tests
    gpu/gpu_device.h
    gpu/gpu_injected_input.h
    gpu/gpu_application_init_test.cpp
    gpu/gpu_disabled_opacity_test.cpp
    gpu/gpu_invalid_test.cpp
    gpu/gpu_tooltip_test.cpp
    gpu/gpu_cut_text_test.cpp
    gpu/gpu_message_dialog_test.cpp
    gpu/gpu_node_view_test.cpp
    gpu/gpu_tree_drop_line_test.cpp
    gpu/gpu_table_header_test.cpp
    gpu/gpu_inspector_fields_test.cpp
    gpu/gpu_label_help_test.cpp
    gpu/gpu_node_look_test.cpp
    gpu/gpu_invalid_field_test.cpp
    gpu/gpu_theme_crossfade_test.cpp
    gpu/gpu_sdl_host_event_loop_test.cpp
    gpu/gpu_sdl_single_copy_test.cpp
    gpu/gpu_zero_setup_test.cpp
    gpu/gpu_tutorial_test.cpp)
  target_sources(gui_gpu_tests PRIVATE ${TACHYON_TUTORIAL_SOURCES})
  source_group("Tutorial" FILES ${TACHYON_TUTORIAL_SOURCES})

  # The same tests again on Direct3D 12 (gui_gpu_tests --graphics=d3d12), as D3D12.<test>.
  # Off by default: it doubles the GPU tests' time, for work on that backend and its CI job.
  if(TACHYON_D3D12)
    option(TACHYON_GPU_TESTS_ON_D3D12 "Run gui_gpu_tests a second time, on Direct3D 12" OFF)
    if(TACHYON_GPU_TESTS_ON_D3D12)
      gtest_discover_tests(gui_gpu_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60
                           TEST_PREFIX "D3D12." EXTRA_ARGS "--graphics=d3d12")
    endif()
  endif()
  # And on SDL GPU (gui_gpu_tests --graphics=sdlgpu), as SDLGPU.<test>, on the driver SDL picks
  # (SDL_GPU_DRIVER names another). Off by default, as above.
  if(TACHYON_SDLGPU)
    option(TACHYON_GPU_TESTS_ON_SDLGPU "Run gui_gpu_tests a second time, on SDL GPU" OFF)
    if(TACHYON_GPU_TESTS_ON_SDLGPU)
      gtest_discover_tests(gui_gpu_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60
                           TEST_PREFIX "SDLGPU." EXTRA_ARGS "--graphics=sdlgpu")
    endif()
  endif()
endif()

if(TACHYON_SDL AND TACHYON_VULKAN)
  tachyon_add_gpu_tests(gui_vulkan_tests
    gpu/gpu_test_harness.h
    gpu/gpu_descriptor_test.cpp
    gpu/gpu_image_set_test.cpp
    gpu/gpu_window_lifecycle_test.cpp
    gpu/gpu_window_style_test.cpp
    gpu/gpu_primitive_coverage_test.cpp
    gpu/gpu_readback_smoke_test.cpp
    gpu/gpu_external_device_test.cpp
    gpu/gpu_device_loss_test.cpp)
  # gui_core links vma, Vulkan and vk-bootstrap PRIVATE (a static library), so the harness
  # links them itself to reach the backend.
  target_link_libraries(gui_vulkan_tests PRIVATE vma Vulkan::Vulkan vkbootstrap)
endif()

if(TACHYON_SDL AND TACHYON_D3D12)
  # The Direct3D 12 backend itself, with its debug layer's messages recorded.
  tachyon_add_gpu_tests(gui_d3d12_tests
    gpu/d3d12_test_harness.h
    gpu/allocation_count.h
    gpu/gpu_draw_app.h
    gpu/d3d12_device_test.cpp
    gpu/d3d12_draw_test.cpp
    gpu/d3d12_scene_test.cpp
    gpu/d3d12_memory_test.cpp
    gpu/d3d12_external_device_test.cpp)
  # gui_core links D3D12MA PRIVATE, so the tests that look inside the backend link it themselves.
  target_link_libraries(gui_d3d12_tests PRIVATE d3d12ma)
endif()

if(TACHYON_SDL AND TACHYON_SDLGPU)
  # The SDL GPU backend itself, on each of SDL's drivers, its debug messages recorded.
  tachyon_add_gpu_tests(gui_sdlgpu_tests
    gpu/sdlgpu_test_harness.h
    gpu/allocation_count.h
    gpu/sdlgpu_device_test.cpp
    gpu/sdlgpu_draw_test.cpp
    gpu/sdlgpu_scene_test.cpp
    gpu/sdlgpu_external_device_test.cpp)
endif()
