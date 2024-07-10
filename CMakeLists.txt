# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()



project ("Scimulator")

add_subdirectory ("src")

message("Compiling Shaders")
if (WIN32)
set(GLSL_COMPILER "$ENV{VULKAN_SDK}/Bin/glslc.exe")
endif()
file(GLOB_RECURSE GLSL_SRC_FILES "./shaders/*.comp" "./shaders/*.task" "./shaders/*.mesh" "./shaders/*.vert" "./shaders/*.frag")
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/shaders/")

source_group("Shader Files" FILES ${GLSL_SRC_FILES})

foreach(GLSL_FILE ${GLSL_SRC_FILES})
	get_filename_component(GLSL_FILE_NAME ${GLSL_FILE} NAME)
	message(${GLSL_FILE_NAME})
	set(SPIRV_FILE "${PROJECT_BINARY_DIR}/shaders/${GLSL_FILE_NAME}.spv")
	execute_process(COMMAND ${GLSL_COMPILER} "-g" ${GLSL_FILE} -o ${SPIRV_FILE} "--target-env=vulkan1.3")
endforeach(GLSL_FILE)
message("Done") 