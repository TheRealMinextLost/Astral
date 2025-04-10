//
// Camera controls of the engine
//
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h> // For key codes




class Camera {
public:
    glm::vec3 Position;
    glm::vec3 Target;
    glm::vec3 WorldUp;
    float Fov; // Keep FOV here

    // Input state
    bool IsOrbiting = false;
    bool IsPanning = false;
    double LastMouseX = 0.0, LastMouseY = 0.0;

    // Sensitivity settings
    float OrbitSensitivity = 0.005f;
    float PanSensitivity = 0.002f; // Adjust based on screen size/preference
    float ZoomSensitivity = 0.5f; // Units per scroll tick

    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 5.0f),
            glm::vec3 target = glm::vec3(0.0f,0.0f,0.0f),
            glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f),
            float fov = 45.0f);

    // Calculates the view matrix
    glm::mat4 GetViewMatrix() const;

    // Calculates the basis vectors (Right, Up, Forward)
    void GetBasisVectors(glm::vec3& outRight, glm::vec3& outUp, glm::vec3& outForward) const;

    // Calculates the 3x3 basis matrix for the shader
    glm::mat3 GetBasisMatrix() const;

    // Processes input received from GLFW callbacks
    void ProcessOrbit(double xoffset, double yoffset);
    void ProcessPan(double xoffset, double yoffset);
    void ProcessZoom(double yoffset);

    // Static callback functions
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};



