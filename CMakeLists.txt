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

# ENet (zpl-c single-header fork). Exactly one translation unit (src/net/enet_implementation.cpp)
# defines ENET_IMPLEMENTATION; everyone else just includes <enet.h>.
FetchContent_Declare(enet
    GIT_REPOSITORY https://github.com/zpl-c/enet.git
    GIT_TAG        v2.6.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(enet)
add_library(enet_header INTERFACE)
target_include_directories(enet_header SYSTEM INTERFACE ${enet_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(enet_header INTERFACE ws2_32 winmm)
endif()

# LZ4 (bulk packet compression: chunk data).
set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)
set(LZ4_BUNDLED_MODE ON CACHE BOOL "" FORCE)
FetchContent_Declare(lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        v1.10.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  build/cmake)
FetchContent_MakeAvailable(lz4)

# Steamworks SDK (optional). Drop the SDK into ./sdk (public/, redistributable_bin/). It is not redistributable,
# so it is git-ignored. Without it the game still builds and uses the ENet transport only.
set(STEAMWORKS_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sdk CACHE PATH "Steamworks SDK root")
set(TERRARIA_STEAM_APP_ID 480 CACHE STRING "Steam App ID (480 = Spacewar test app)")
if(EXISTS ${STEAMWORKS_SDK_DIR}/public/steam/steam_api.h)
    option(TERRARIA_ENABLE_STEAM "Build with Steamworks networking" ON)
else()
    option(TERRARIA_ENABLE_STEAM "Build with Steamworks networking" OFF)
endif()
if(TERRARIA_ENABLE_STEAM)
    if(WIN32)
        set(STEAM_LIB_DIR ${STEAMWORKS_SDK_DIR}/redistributable_bin/win64)
        set(STEAM_LIB_FILE ${STEAM_LIB_DIR}/steam_api64.lib)
        set(STEAM_RUNTIME_FILE ${STEAM_LIB_DIR}/steam_api64.dll)
    elseif(APPLE)
        set(STEAM_LIB_DIR ${STEAMWORKS_SDK_DIR}/redistributable_bin/osx)
        set(STEAM_LIB_FILE ${STEAM_LIB_DIR}/libsteam_api.dylib)
        set(STEAM_RUNTIME_FILE ${STEAM_LIB_FILE})
    else()
        set(STEAM_LIB_DIR ${STEAMWORKS_SDK_DIR}/redistributable_bin/linux64)
        set(STEAM_LIB_FILE ${STEAM_LIB_DIR}/libsteam_api.so)
        set(STEAM_RUNTIME_FILE ${STEAM_LIB_FILE})
    endif()
    add_library(steam_api SHARED IMPORTED)
    set_target_properties(steam_api PROPERTIES
        IMPORTED_LOCATION ${STEAM_RUNTIME_FILE}
        IMPORTED_IMPLIB   ${STEAM_LIB_FILE}
        INTERFACE_INCLUDE_DIRECTORIES ${STEAMWORKS_SDK_DIR}/public)
    message(STATUS "Steamworks enabled (App ID ${TERRARIA_STEAM_APP_ID})")
else()
    message(STATUS "Steamworks disabled (SDK not found in ${STEAMWORKS_SDK_DIR})")
endif()

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
    enet_header
    lz4_static
    glm::glm)

# --- Tests (opt-in: cmake --build build --target net_loopback_test) ------------------------------
set(NET_SOURCES ${TERRARIA_SOURCES})
list(FILTER NET_SOURCES INCLUDE REGEX "src/net/(transport|enet_backend|loopback_backend|sim_backend|session|compression|steam_backend|steam_platform|network_manager|enet_implementation)\\.cpp$")
add_executable(net_loopback_test EXCLUDE_FROM_ALL tests/net_loopback_test.cpp ${NET_SOURCES})
target_include_directories(net_loopback_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(net_loopback_test PRIVATE enet_header lz4_static)

# Manual two-machine Steam relay check: cmake --build build --target net_steam_pair
add_executable(net_steam_pair EXCLUDE_FROM_ALL tests/net_steam_pair.cpp ${NET_SOURCES})
target_include_directories(net_steam_pair PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(net_steam_pair PRIVATE enet_header lz4_static)

# --- Steam runtime files next to the executable ---------------------------------------------------
if(TERRARIA_ENABLE_STEAM)
    target_link_libraries(terraria_3d PRIVATE steam_api)
    target_compile_definitions(terraria_3d PRIVATE T3D_STEAM=1 T3D_STEAM_APP_ID=${TERRARIA_STEAM_APP_ID})
    set_target_properties(terraria_3d net_loopback_test net_steam_pair PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    target_link_libraries(net_loopback_test PRIVATE steam_api)
    target_compile_definitions(net_loopback_test PRIVATE T3D_STEAM=1)
    target_link_libraries(net_steam_pair PRIVATE steam_api)
    target_compile_definitions(net_steam_pair PRIVATE T3D_STEAM=1)
    add_custom_command(TARGET terraria_3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STEAM_RUNTIME_FILE} $<TARGET_FILE_DIR:terraria_3d>
        # Development only: lets the exe run outside Steam. Remove for shipping builds.
        COMMAND ${CMAKE_COMMAND} -E echo ${TERRARIA_STEAM_APP_ID} > $<TARGET_FILE_DIR:terraria_3d>/steam_appid.txt)
endif()
