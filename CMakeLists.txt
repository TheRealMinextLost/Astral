cmake_minimum_required(VERSION 3.30)
project(Astral)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include FetchContent module
include(FetchContent)

# Fetch GLFW first (since ImGui depends on it)
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4 # Use a specific tag/version for stability
)
# Configure GLFW build options
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# Fetch ImGui
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG docking  # Using the docking branch instead of a specific version
)

FetchContent_MakeAvailable(imgui)

# Create ImGui library
add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

# Link ImGui with GLFW (so it can find the GLFW headers)
target_link_libraries(imgui PUBLIC glfw)

# Fetch GLM
FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1 # Use a specific tag/version
)
FetchContent_MakeAvailable(glm)

# For GLAD we'll need to handle it separately
# Add GLAD source file
set(GLAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/glad")
add_library(glad "${GLAD_DIR}/src/glad.c")
target_include_directories(glad PUBLIC "${GLAD_DIR}/include")

# Find OpenGL
find_package(OpenGL REQUIRED)

#[[set (UI_SOURCES UI/AstralUI.cpp)
set(UI_HEADERS UI/AstralUI.h)]]




# Add the executable
add_executable(Astral
        main.cpp
        UI/AstralUI.cpp
        UI/AstralUI.h
        utilities/utility.cpp
        utilities/utility.h
        Basic/Camera.cpp
        Basic/Camera.h
        Basic/SDFObject.h
        ImGizmo/ImGuizmo.h
        ImGizmo/ImGuizmo.cpp
)

# Optionally specify runtime output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/)

# Add include directories for Astral executable
target_include_directories(Astral PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${glm_SOURCE_DIR}
        ${GLAD_DIR}/include)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# Link libraries
target_link_libraries(Astral PRIVATE
        OpenGL::GL
        glfw
        glm
        glad
        imgui
)

set(CMAKE_EXE_LINKER_FLAGS -static)
