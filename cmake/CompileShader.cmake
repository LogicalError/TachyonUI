# Compiles one Slang module's vertexMain and fragmentMain for a graphics backend, to
# <name>.vert.<ext> and <name>.frag.<ext>, unless both are newer than the module, its includes
# and this script. Visual Studio runs custom build steps once per configuration and CI restores
# compiled shaders from a cache; this keeps both from recompiling.
#   cmake -DSLANGC=<slangc> -DSOURCE=<module.slang> -DOUTPUT_DIR=<dir> -DNAME=<name>
#         -DSHADER_TARGET=<spirv|dxil|metal|sdl-spirv|sdl-dxil|sdl-metal> -DINCLUDES=<a|b|...> -P CompileShader.cmake
#
# spirv (Vulkan): SPIR-V 1.6, Vulkan 1.3's. Warning 41012 says the profile widens to every
#   capability a texture's methods could need (sparse residency, min LOD, ...); only those the
#   shader uses are emitted, so it is off.
# dxil (Direct3D 12): Shader Model 6.6, for its descriptor heap indexing. D3D12 runs only signed
#   DXIL, so an unsigned result (a compiler without a validator) fails the build.
# metal (Metal): Metal Shading Language source, which the backend compiles when it starts
#   (MTLDevice newLibraryWithSource), so the build needs no Xcode, only slangc.
# sdl-spirv, sdl-dxil (SDL GPU, .sdl.spv and .sdl.dxil): the resources where SDL GPU binds them,
#   the fragment stage's apart from the vertex stage's (TACHYON_SDLGPU_FRAGMENT). SPIR-V 1.3,
#   the lowest Slang emits (Vulkan 1.1); Shader Model 6.2, the lowest with 16-bit types.
# sdl-metal (SDL GPU on macOS, .sdl.metal): Metal Shading Language source with the resources in
#   the slots SDL's Metal driver binds them to (TACHYON_SDLGPU_METAL), compiled by SDL when it
#   makes the shader.

if(SHADER_TARGET STREQUAL "spirv")
  set(_extension spv)
  set(_flags -target spirv -profile spirv_1_6 -warnings-disable 41012)
elseif(SHADER_TARGET STREQUAL "dxil")
  set(_extension dxil)
  set(_flags -target dxil -profile sm_6_6 -DTACHYON_D3D12)
elseif(SHADER_TARGET STREQUAL "metal")
  set(_extension metal)
  set(_flags -target metal -DTACHYON_METAL)
elseif(SHADER_TARGET STREQUAL "sdl-spirv")
  set(_extension sdl.spv)
  set(_flags -target spirv -profile spirv_1_3 -warnings-disable 41012 -DTACHYON_SDLGPU -DTACHYON_SDLGPU_SPIRV)
elseif(SHADER_TARGET STREQUAL "sdl-dxil")
  set(_extension sdl.dxil)
  set(_flags -target dxil -profile sm_6_2 -DTACHYON_SDLGPU)
elseif(SHADER_TARGET STREQUAL "sdl-metal")
  set(_extension sdl.metal)
  set(_flags -target metal -DTACHYON_SDLGPU -DTACHYON_SDLGPU_METAL)
else()
  message(FATAL_ERROR "CompileShader.cmake: SHADER_TARGET must be spirv, dxil, metal, sdl-spirv, sdl-dxil or sdl-metal, not '${SHADER_TARGET}'")
endif()

string(REPLACE "|" ";" _includes "${INCLUDES}")
set(_stages vertex fragment)
set(_entries vertexMain fragmentMain)
set(_suffixes vert frag)

set(_stale FALSE)
foreach(_suffix IN LISTS _suffixes)
  set(_output "${OUTPUT_DIR}/${NAME}.${_suffix}.${_extension}")
  if(NOT EXISTS "${_output}")
    set(_stale TRUE)
  else()
    foreach(_input IN LISTS SOURCE _includes CMAKE_CURRENT_LIST_FILE)
      if("${_input}" IS_NEWER_THAN "${_output}")
        set(_stale TRUE)
      endif()
    endforeach()
  endif()
endforeach()
if(NOT _stale)
  return()
endif()

foreach(_i RANGE 1)
  list(GET _stages   ${_i} _stage)
  list(GET _entries  ${_i} _entry)
  list(GET _suffixes ${_i} _suffix)
  set(_output "${OUTPUT_DIR}/${NAME}.${_suffix}.${_extension}")
  set(_stage_define "")
  if(SHADER_TARGET MATCHES "^sdl-" AND _stage STREQUAL "fragment")
    set(_stage_define -DTACHYON_SDLGPU_FRAGMENT)
  endif()
  execute_process(
    COMMAND "${SLANGC}" "${SOURCE}" ${_flags} ${_stage_define} -stage ${_stage} -entry ${_entry} -o "${_output}"
    RESULT_VARIABLE _result)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "slangc failed on ${SOURCE} (${_entry}, ${SHADER_TARGET})")
  endif()
  if(SHADER_TARGET MATCHES "dxil$")
    # The container: "DXBC", then a 16-byte hash, all zeros when unsigned.
    file(READ "${_output}" _header LIMIT 20 HEX)
    string(SUBSTRING "${_header}" 8 32 _hash)
    if(_hash STREQUAL "00000000000000000000000000000000")
      file(REMOVE "${_output}")
      message(FATAL_ERROR "slangc left ${_output} unsigned, which Direct3D 12 refuses. Its DXC "
                          "(dxcompiler.dll) needs a validator: a newer DXC, or dxil.dll beside it.")
    endif()
  endif()
endforeach()
