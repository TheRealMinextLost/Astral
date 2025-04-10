//
// UI for the program
//

#pragma once

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>


// Structure to hold all the parameters controlled by UI
struct RenderParams {
    // Rotation controls
    float rotationSpeed = 0.5f;
    float rotationAxis[3] = {0.0f, 1.0f, 0.0f};

    // Scene settings
    float clearColor[3] = {0.1f, 0.1f, 0.1f};
    float fov = 45.0f;

    // Camera settings
    float cameraPos[3] = {0.0f, 0.0f, 5.0f};
    float cameraTarget[3] = {0.0f, 0.0f, 0.0f};

    // Added a simple animation toggle
    bool animateRotation = true;

    // Cube manipulation
    float cubeScale = 1.0f;
    float cubePosition[3] = {0.0f, 0.0f, 0.0f};
    float cubeColor[3] = {1.0f, 1.0f, 1.0f};
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
    void createUI();

    // Get the current render parameters
    const RenderParams& getParams() const { return m_params; }

private:
    // ImGui windows - each as a separate method for organization
    void renderControlPanel();
    void renderCameraControls();
    void renderObjectManipulation();
    void renderInfoPanel();

    GLFWwindow* m_window;
    RenderParams m_params;

    // Performance monitoring
    float m_frameTimes[120] = {};
    int m_frameTimeIndex = 0;

    // UI state
    bool m_showDemoWindow = false;

};



