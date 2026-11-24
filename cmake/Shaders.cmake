# Compiles the Slang shaders under workdirectory/shaders for each graphics backend built in, to
# ${PROJECT_BINARY_DIR}/shaders (SHADER_BINARY_FILES), which gui_core compiles in: SPIR-V
# (.spv) for Vulkan, DXIL (.dxil) for Direct3D 12, Metal Shading Language source (.metal) for Metal,
# and again for SDL GPU (.sdl.spv, .sdl.dxil, .sdl.metal). Creates the `tachyon_shaders` custom target
# that gui_core depends on; inside another project also `tachyon_resources`, which copies the
# optional resources (themes, fonts, icons) into the build tree.

set(TACHYON_SHADER_TARGETS "")
if(TACHYON_VULKAN)
  list(APPEND TACHYON_SHADER_TARGETS spirv)
endif()
if(TACHYON_D3D12)
  list(APPEND TACHYON_SHADER_TARGETS dxil)
endif()
if(TACHYON_METAL)
  list(APPEND TACHYON_SHADER_TARGETS metal)
endif()
if(TACHYON_SDLGPU)
  list(APPEND TACHYON_SHADER_TARGETS sdl-spirv)
  if(WIN32)
    list(APPEND TACHYON_SHADER_TARGETS sdl-dxil)
  endif()
  if(APPLE)
    list(APPEND TACHYON_SHADER_TARGETS sdl-metal)
  endif()
endif()

if(TACHYON_SHADER_TARGETS)
  # Older Slang has no SV_VulkanVertexID, and its SV_VertexID differs between versions.
  set(TACHYON_SLANG_MIN_VERSION 2026.1)
  find_program(SLANGC slangc
    HINTS $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/bin/ /usr/bin /usr/local/bin /opt/homebrew/bin)
  if(NOT SLANGC)
    message(FATAL_ERROR
      "slangc not found. Install the Vulkan SDK (and set VULKAN_SDK) or a Slang release "
      "(https://github.com/shader-slang/slang/releases), or point SLANGC at the executable.")
  endif()
  execute_process(COMMAND "${SLANGC}" -version OUTPUT_VARIABLE _slang_version ERROR_VARIABLE _slang_version)
  string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" _slang_version "${_slang_version}")
  if(NOT _slang_version OR _slang_version VERSION_LESS TACHYON_SLANG_MIN_VERSION)
    message(FATAL_ERROR
      "${SLANGC} is Slang ${_slang_version}; the shaders need ${TACHYON_SLANG_MIN_VERSION} or newer. "
      "Point SLANGC at a newer slangc.")
  endif()
endif()

# tachyon_compile_slang(<module.slang> <output dir> <list var> <spirv|dxil|metal|sdl-spirv|sdl-dxil|sdl-metal>
#                       [DEPENDS <include>...])
# Compiles the module's vertexMain and fragmentMain for a backend, to <output dir>/<name>.vert.spv
# and <name>.frag.spv (.dxil, .metal, .sdl.spv, .sdl.dxil, .sdl.metal), and appends both to <list var>.
function(tachyon_compile_slang SOURCE OUTPUT_DIR LIST_VAR SHADER_TARGET)
  cmake_parse_arguments(PARSE_ARGV 4 ARG "" "" "DEPENDS")
  if(SHADER_TARGET STREQUAL "spirv")
    set(_extension spv)
  elseif(SHADER_TARGET STREQUAL "sdl-spirv")
    set(_extension sdl.spv)
  elseif(SHADER_TARGET STREQUAL "sdl-dxil")
    set(_extension sdl.dxil)
  elseif(SHADER_TARGET STREQUAL "sdl-metal")
    set(_extension sdl.metal)
  else()
    set(_extension ${SHADER_TARGET})
  endif()
  get_filename_component(_name "${SOURCE}" NAME_WE)
  set(_outputs "${OUTPUT_DIR}/${_name}.vert.${_extension}" "${OUTPUT_DIR}/${_name}.frag.${_extension}")
  set(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CompileShader.cmake")
  string(REPLACE ";" "|" _includes "${ARG_DEPENDS}")
  add_custom_command(
    OUTPUT ${_outputs}
    COMMAND ${CMAKE_COMMAND} -DSLANGC=${SLANGC} -DSOURCE=${SOURCE} -DOUTPUT_DIR=${OUTPUT_DIR} -DNAME=${_name}
            -DSHADER_TARGET=${SHADER_TARGET} "-DINCLUDES=${_includes}" -P ${_script}
    DEPENDS ${SOURCE} ${ARG_DEPENDS} ${_script}
    COMMENT "  > > >  BUILDING SHADER > > >  ${SOURCE} (${SHADER_TARGET})"
    VERBATIM)
  set(${LIST_VAR} ${${LIST_VAR}} ${_outputs} PARENT_SCOPE)
endfunction()

# A module per primitive kind (*_shader.slang); the other files are what they include.
# Every module depends on every include, so a changed include recompiles them all.
file(GLOB SLANG_MODULE_FILES
  CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/*_shader.slang"
)
file(GLOB SLANG_INCLUDE_FILES
  CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/*.slang"
)
list(REMOVE_ITEM SLANG_INCLUDE_FILES ${SLANG_MODULE_FILES})

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/shaders")
set(SHADER_BINARY_FILES "")
foreach(_target IN LISTS TACHYON_SHADER_TARGETS)
  foreach(_module IN LISTS SLANG_MODULE_FILES)
    tachyon_compile_slang("${_module}" "${PROJECT_BINARY_DIR}/shaders" SHADER_BINARY_FILES ${_target}
                          DEPENDS ${SLANG_INCLUDE_FILES})
  endforeach()
endforeach()

add_custom_target(tachyon_shaders ALL
  SOURCES ${SLANG_MODULE_FILES} ${SLANG_INCLUDE_FILES}
  DEPENDS ${SHADER_BINARY_FILES}
  COMMAND ${CMAKE_COMMAND} -E echo "...Building Shaders..."
)

set_source_files_properties(${SHADER_BINARY_FILES} PROPERTIES GENERATED TRUE)
set_target_properties(tachyon_shaders PROPERTIES FOLDER "Examples")

if(NOT PROJECT_IS_TOP_LEVEL)
  # file(COPY) skips files that haven't changed, so this is cheap after the first build.
  add_custom_target(tachyon_resources ALL
    COMMAND ${CMAKE_COMMAND} "-DSOURCE=${PROJECT_SOURCE_DIR}/workdirectory"
            "-DDESTINATION=${TACHYON_BUILD_RESOURCE_DIR}" "-DFALLBACK_FONTS=${TACHYON_FALLBACK_FONTS}"
            -P ${CMAKE_CURRENT_LIST_DIR}/CopyResources.cmake
    VERBATIM)
  set_target_properties(tachyon_resources PROPERTIES FOLDER "Examples")
endif()
