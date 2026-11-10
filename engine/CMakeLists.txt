cmake_minimum_required(VERSION 3.25)
if(POLICY CMP0218)
    cmake_policy(SET CMP0218 NEW)
endif()
set(CMAKE_WARN_DEPRECATED OFF)

set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

project(LucidEngine CXX)

if(MSVC)
    add_compile_options(/EHa)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# EnTT (ECS)
FetchContent_Declare(entt URL https://github.com/skypjack/entt/archive/refs/tags/v3.13.0.tar.gz)
FetchContent_MakeAvailable(entt)

# GLM (Math)
FetchContent_Declare(glm URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz)
FetchContent_MakeAvailable(glm)

# nlohmann/json
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

# GLFW (Interactive Window)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip)
FetchContent_MakeAvailable(glfw)

# IXWebSocket (JSON-RPC)
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ixwebsocket URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v11.4.4.tar.gz)
FetchContent_MakeAvailable(ixwebsocket)

# QuickJS-ng (Sandbox)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_QJS OFF CACHE BOOL "" FORCE)
set(BUILD_QJSC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(qjs URL https://github.com/quickjs-ng/quickjs/archive/refs/tags/v0.8.0.tar.gz)
FetchContent_MakeAvailable(qjs)

if(MSVC)
    foreach(qjs_target qjs qjsc qjs_exe run-test262 test_conv function_source)
        if(TARGET ${qjs_target})
            target_compile_options(${qjs_target} PRIVATE /w)
        endif()
    endforeach()
endif()

# Jolt Physics
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)
FetchContent_Declare(jolt URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.0.0.tar.gz)
FetchContent_MakeAvailable(jolt)
add_subdirectory(${jolt_SOURCE_DIR}/Build ${jolt_BINARY_DIR})

# fastgltf
set(FASTGLTF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(fastgltf URL https://github.com/spnda/fastgltf/archive/refs/tags/v0.8.0.tar.gz)
FetchContent_MakeAvailable(fastgltf)

# ASIO (UDP server)
FetchContent_Declare(asio URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-28-0.tar.gz)
FetchContent_MakeAvailable(asio)

# VMA (Vulkan Memory Allocator)
FetchContent_Declare(vma URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.1.0.tar.gz)
FetchContent_MakeAvailable(vma)

# Dear ImGui
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.8.tar.gz)
FetchContent_MakeAvailable(imgui)

find_package(Vulkan REQUIRED)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS "include/*.hpp")

set(IMGUI_SRCS
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

# Shaders: dynamically collect compilable shader stages and track glsl includes
file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*.comp"
)
file(GLOB_RECURSE GLSL_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*.glsl"
)

set(SPV_FILES "")
foreach(SHADER ${SHADER_FILES})
    get_filename_component(FILE_NAME ${SHADER} NAME)
    set(SPV_FILE "${CMAKE_CURRENT_BINARY_DIR}/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.3 ${SHADER} -o ${SPV_FILE}
        DEPENDS ${SHADER} ${GLSL_HEADERS}
        COMMENT "Compiling ${FILE_NAME} to SPIR-V"
    )
    list(APPEND SPV_FILES ${SPV_FILE})
endforeach()

add_custom_target(CompileShaders DEPENDS ${SPV_FILES})

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/include/network/DualTransportContracts.hpp"
               "${CMAKE_CURRENT_SOURCE_DIR}/../src/transport/TelemetrySnapshot.ts"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_binary_schema.py"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_binary_schema.py"
        COMMENT "Generating Dual-Transport UI Shell Binary Contracts"
    )
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/include/network/EmbeddedAssets.hpp"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_assets.py"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_assets.py"
        COMMENT "Embedding Web Assets into C++ Header"
    )
    add_custom_target(GenerateBinarySchema DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/network/DualTransportContracts.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/transport/TelemetrySnapshot.ts"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/network/EmbeddedAssets.hpp"
    )
endif()

add_executable(engine ${SRC_FILES} ${HEADER_FILES} ${IMGUI_SRCS})
add_dependencies(engine CompileShaders)
if(Python3_FOUND)
    add_dependencies(engine GenerateBinarySchema)
endif()

foreach(SPV_FILE ${SPV_FILES})
    add_custom_command(TARGET engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPV_FILE}
        $<TARGET_FILE_DIR:engine>
    )
endforeach()

add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
    $<TARGET_FILE_DIR:engine>/assets/fonts
)

add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures
    $<TARGET_FILE_DIR:engine>/assets/textures
)

# Copy catalogs on every build (including Visual Studio builds where LINK_DEPENDS is not portable).
add_custom_target(CopyLanguageCatalogs ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/language
    $<TARGET_FILE_DIR:engine>/language
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/language/en.json
        ${CMAKE_CURRENT_SOURCE_DIR}/language/pl.json
    VERBATIM
)
add_dependencies(CopyLanguageCatalogs engine)

target_include_directories(engine PRIVATE include)
target_include_directories(engine PRIVATE ${vma_SOURCE_DIR}/include)
target_include_directories(engine PRIVATE ${jolt_SOURCE_DIR})
target_include_directories(engine PRIVATE ${asio_SOURCE_DIR}/asio/include)
target_include_directories(engine PRIVATE ${qjs_SOURCE_DIR})
target_include_directories(engine PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(engine PRIVATE ASIO_STANDALONE USE_GLFW)
if(WIN32)
    # ASIO/IXWebSocket use the Windows SDK target to select available APIs.
    target_compile_definitions(engine PRIVATE _WIN32_WINNT=0x0A00)
endif
target_link_libraries(engine PRIVATE
    EnTT::EnTT
    glm::glm
    nlohmann_json::nlohmann_json
    glfw
    ixwebsocket
    qjs
    Jolt
    fastgltf::fastgltf
    Vulkan::Vulkan
)
