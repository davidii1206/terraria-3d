cmake_minimum_required(VERSION 3.28)
project(terraria_3d_game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# Dependencies (all fetched from source, only the Vulkan SDK/headers and the
# platform windowing dev packages need to be installed on the system).
# ---------------------------------------------------------------------------

# Vulkan headers: Daxa needs recent (1.4.x) headers; distro packages are often older.
# The loader library itself still comes from the system / Vulkan SDK via find_package(Vulkan).
FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.4.340
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(vulkan_headers)
set(Vulkan_INCLUDE_DIR ${vulkan_headers_SOURCE_DIR}/include CACHE PATH "" FORCE)

# GLFW must be declared *before* Daxa: Daxa's ImGui target only gets the GLFW
# backend (imgui_impl_glfw) if a `glfw` target already exists.
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glfw)

# glslang (runtime shader compilation inside Daxa's pipeline manager).
# Prefer an installed glslang (Vulkan SDK / vcpkg / distro package). Otherwise build the
# revision Daxa is tested with. Daxa includes <glslang/SPIRV/GlslangToSpv.h>, which matches the
# *installed* header layout but not glslang's source tree, so a small include shim is created.
find_package(glslang CONFIG QUIET)
if(NOT TARGET glslang::glslang)
    message(STATUS "glslang not found on the system, building it from source")
    set(ENABLE_OPT              OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL             OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER      OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS           OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG        5398d55e33dff7d26fecdd2c35808add986c558c)
    FetchContent_MakeAvailable(glslang)

    set(GLSLANG_SHIM_DIR ${CMAKE_BINARY_DIR}/glslang_include_shim)
    file(MAKE_DIRECTORY ${GLSLANG_SHIM_DIR}/glslang)
    if(NOT EXISTS ${GLSLANG_SHIM_DIR}/glslang/SPIRV)
        file(CREATE_LINK ${glslang_SOURCE_DIR}/SPIRV ${GLSLANG_SHIM_DIR}/glslang/SPIRV SYMBOLIC)
    endif()
    # Directory property: inherited by the daxa target created below.
    include_directories(SYSTEM ${GLSLANG_SHIM_DIR})
endif()

# Daxa (+ Dear ImGui through Daxa's own dependency handling).
set(DAXA_ENABLE_TESTS                          OFF CACHE BOOL "" FORCE)
set(DAXA_ENABLE_TOOLS                          OFF CACHE BOOL "" FORCE)
set(DAXA_ENABLE_UTILS_IMGUI                    ON  CACHE BOOL "" FORCE)
set(DAXA_ENABLE_UTILS_PIPELINE_MANAGER_GLSLANG ON  CACHE BOOL "" FORCE)
set(DAXA_ENABLE_UTILS_MEM                      OFF CACHE BOOL "" FORCE)
set(DAXA_ENABLE_UTILS_TASK_GRAPH               OFF CACHE BOOL "" FORCE)
FetchContent_Declare(daxa
    GIT_REPOSITORY https://github.com/Ipotrick/Daxa.git
    GIT_TAG        3a561af0119873a53ed6deed988c06ec0aeb1e77
    PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_daxa.cmake)
FetchContent_MakeAvailable(daxa)

# cgltf is a single header library.
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(cgltf)
add_library(cgltf INTERFACE)
target_include_directories(cgltf SYSTEM INTERFACE ${cgltf_SOURCE_DIR})

# GLM for the camera / transform math.
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glm)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
file(GLOB_RECURSE TERRARIA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(terraria_3d ${TERRARIA_SOURCES})

target_include_directories(terraria_3d PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders)

target_compile_definitions(terraria_3d PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

target_link_libraries(terraria_3d PRIVATE
    daxa::daxa
    imgui::imgui
    glfw
    cgltf
    glm::glm)
