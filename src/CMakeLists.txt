
add_executable (Scimulator "Main.cpp" "Engine.cpp" "Engine.h" "Types.h" "Initializers.h" "Initializers.cpp" "Images.h" "Images.cpp" "Descriptors.cpp" "Descriptors.h" "Pipelines.h" "Pipelines.cpp" "Mesh.h" "Mesh.cpp" "Materials.h" "Materials.cpp" "Render.h" "Render.cpp" "Camera.h" "Camera.cpp" )
target_include_directories(Scimulator PRIVATE ../include)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Scimulator PROPERTY CXX_STANDARD 20)
endif()

target_compile_definitions(Scimulator
    PRIVATE
        $<$<CONFIG:Debug>:DEBUG>
        GLM_FORCE_DEPTH_ZERO_TO_ONE)


# Function to simplify adding vcpkg libraries
function(add_vcpkg_library LIBRARY_NAME)
    find_package(${LIBRARY_NAME} CONFIG REQUIRED)
    target_link_libraries(Scimulator PRIVATE ${LIBRARY_NAME}::${LIBRARY_NAME})
endfunction()


add_vcpkg_library(SDL2)
add_vcpkg_library(SDL2_image)
add_vcpkg_library(imgui)
add_vcpkg_library(glm)
add_vcpkg_library(vk-bootstrap)
add_vcpkg_library(fmt)
add_vcpkg_library(fastgltf)


find_package(Vulkan) # https://cmake.org/cmake/help/latest/module/FindVulkan.html
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(VulkanUtilityLibraries CONFIG REQUIRED)
target_link_libraries(Scimulator PRIVATE Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator Vulkan::UtilityHeaders SDL2::SDL2main)

