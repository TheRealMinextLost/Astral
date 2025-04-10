//
// Handling UI
//

#include "AstralUI.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <imgui_impl_opengl3.h>
#include <iostream>

AstralUI::AstralUI(GLFWwindow* window) : m_window(window) {
    init();
}

AstralUI::~AstralUI() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void AstralUI::init() {
    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // When viewports are enabled we need to configure style
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void AstralUI::newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Create dockspace
    ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0, 0, 0, 0)); // Set background alpha to 0
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::PopStyleColor();
}

void AstralUI::createUI(float& fovRef) {
    // Demo window toggle in menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Tools")) {
            ImGui::MenuItem("ImGui Demo Window", NULL, &m_showDemoWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Show ImGui demo window if enabled
    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }

    renderMainPanel(fovRef);

    /*// Render our custom UI windows
    renderControlPanel();
    renderCameraControls();
    renderObjectManipulation();
    renderInfoPanel();*/
}


void AstralUI::renderMainPanel(float& fovRef) {
    // Keep track of the selected debug mode

    int SelectedDebugMode = m_selectedDebugMode;

    ImGui::Begin("Astral Settings"); // Start single panel

    ImGui::Separator(); // Separate sections

    // Scene settings
    if (ImGui::CollapsingHeader("Scene Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Clear Color", m_params.clearColor);
        ImGui::SliderFloat("Field of View", &fovRef, 10.0f, 120.0f);
    }

    ImGui::Separator(); // Separate sections

    // SDF Sphere Controls
    if (ImGui::CollapsingHeader("Object Manipulation", ImGuiTreeNodeFlags_DefaultOpen)) {
        // sphere scale slider
        ImGui::Text("Sphere Transformation");
        ImGui::SliderFloat("Radius", &m_params.sphereRadius, 0.1f, 3.0f);
        ImGui::SliderFloat3("Position", m_params.spherePosition, -5.0f, 5.0f);

        ImGui::Separator();

        // sphere color
        ImGui::Text("Sphere Appearance");
        ImGui::ColorEdit3("Color", m_params.sphereColor);

        // Presets
        ImGui::Separator();
        ImGui::Text("Presets");
        if (ImGui::Button("Default")) {
            m_params.sphereRadius = 1.0f;
            m_params.spherePosition[0] = 0.0f; m_params.spherePosition[1] = 0.0f; m_params.spherePosition[2] = 0.0f;
            m_params.sphereColor[0] = 1.0f; m_params.sphereColor[1] = 1.0f; m_params.sphereColor[2] = 1.0f;
        }
    }

    ImGui::Separator(); // Separate section

    // New Section Raymarch debug
    if (ImGui::CollapsingHeader("Raymarch Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Visualization Mode");
        ImGui::RadioButton("Normal",&m_selectedDebugMode, 0); ImGui::SameLine();
        ImGui::RadioButton("Steps",&m_selectedDebugMode, 1); ImGui::SameLine();
        ImGui::RadioButton("Hit/Miss", &m_selectedDebugMode, 2);ImGui::SameLine();;
        ImGui::RadioButton("Normals", &m_selectedDebugMode, 3);
        //std::cout << "UI Debug Mode:" << m_selectedDebugMode << std::endl;
    }

    ImGui::Separator(); // Separate section

    // Info Panel
    if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGuiIO& io = ImGui::GetIO(); // Get IO context

        //Update frame time history
        m_frameTimes[m_frameTimeIndex] = io.Framerate > 0 ? (1000.0f / io.Framerate) : 0.0f;
        m_frameTimeIndex = (m_frameTimeIndex + 1) % IM_ARRAYSIZE(m_frameTimes);

        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::Text("Frame Time: %.3f ms", io.Framerate > 0 ? (1000.0f / io.Framerate) : 0.0f);

        // Plot frame times
        ImGui::PlotLines("Frame Times", m_frameTimes, IM_ARRAYSIZE(m_frameTimes), m_frameTimeIndex,
                            "FrameTime (ms)", 0.0f, 33.3f, ImVec2(0,80));

        // Display application information
        ImGui::Separator();
        ImGui::Text("Astral Engine");
        // Ensure glad is included if you don't have access to glGetString otherwise
        const GLubyte* glVersion = glGetString(GL_VERSION);
        const GLubyte* glRenderer = glGetString(GL_RENDERER);
        ImGui::Text("OpenGL Version: %s", glVersion ? (const char*)glVersion : "Unknown");
        ImGui::Text("GPU: %s", glRenderer ? (const char*)glRenderer : "Unknown");

    }

    ImGui::End();
}



void AstralUI::render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Update and Render additional Platform Windows
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
}
