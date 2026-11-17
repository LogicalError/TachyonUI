# Compiles one Slang module's vertexMain and fragmentMain to SPIR-V, <name>.vert.spv and
# <name>.frag.spv, unless both are newer than the module and its includes. Visual Studio
# runs custom build steps once per configuration and CI restores compiled shaders from a
# cache; this keeps both from recompiling.
#   cmake -DSLANGC=<slangc> -DSOURCE=<module.slang> -DOUTPUT_DIR=<dir> -DNAME=<name>
#         -DINCLUDES=<a|b|...> -P CompileShader.cmake

string(REPLACE "|" ";" _includes "${INCLUDES}")
set(_stages vertex fragment)
set(_entries vertexMain fragmentMain)
set(_suffixes vert frag)

set(_stale FALSE)
foreach(_suffix IN LISTS _suffixes)
  set(_output "${OUTPUT_DIR}/${NAME}.${_suffix}.spv")
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

# SPIR-V 1.6 is Vulkan 1.3's. Warning 41012 says the profile widens to every capability
# a texture's methods could need (sparse residency, min LOD, ...); only those the shader
# uses are emitted, so it is off.
foreach(_i RANGE 1)
  list(GET _stages   ${_i} _stage)
  list(GET _entries  ${_i} _entry)
  list(GET _suffixes ${_i} _suffix)
  execute_process(
    COMMAND "${SLANGC}" "${SOURCE}" -target spirv -profile spirv_1_6 -warnings-disable 41012 -stage ${_stage} -entry ${_entry}
            -o "${OUTPUT_DIR}/${NAME}.${_suffix}.spv"
    RESULT_VARIABLE _result)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "slangc failed on ${SOURCE} (${_entry})")
  endif()
endforeach()
