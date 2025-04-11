//
// UI for the program
//

#pragma once

#include "imgui_impl_glfw.h"



// Structure to hold all the parameters controlled by UI
struct RenderParams {
    // Rotation controls

    // Scene settings
    float clearColor[3] = {0.05f, 0.05f, 0.05f};
    // float fov = 45.0f; // Managed in camera class

    // SDF sphere manipulation
    float sphereRadius = 1.0f;
    float spherePosition[3] = {0.0f, 0.0f, 0.0f};
    float sphereColor[3] = {1.0f, 1.0f, 1.0f};

    // SDF Cube Manipulation
    float boxCenter[3] = {0.0f, -1.5f, 0.0f};
    float boxHalfSize[3] = {5.0f, 0.5, 5.0f};
    float boxColor[3] = {1.0f, 1.0f, 1.0f}; // Light gray

    float blendSmoothness = 0.1f; // Controls 'k' in smin

};

class AstralUI {
public:
    AstralUI(GLFWwindow* window);
    ~AstralUI();

    // Initialize ImGui context and style
    void init();
    // Begin a new ImGui frame
    void newFrame();
    // Render all ImGui windows
    void render();
    // Create all the UI windows and update the render parameters
    void createUI(float& fovRef, double gpuTimeMs, size_t ramBytes);

    // Get the current render parameters
    const RenderParams& getParams() const { return m_params; }

    int getDebugMode() const { return m_selectedDebugMode; } // Getter

private:
    // ImGui windows - each as a separate method for organization

    void renderMainPanel(float& fovRef, double gpuTimeMs, size_t ramBytes); // Pass FOV ref

    GLFWwindow* m_window;
    RenderParams m_params;

    // Performance monitoring
    float m_frameTimes[120] = {};
    int m_frameTimeIndex = 0;

    // UI state
    bool m_showDemoWindow = false;

    int m_selectedDebugMode = 0; // Add a member variable with default

};



