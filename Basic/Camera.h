//
// Camera controls of the engine
//
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp> // Include quaternion header

struct ImGuiIO;

struct GLFWwindow;

class Camera {
public:
    // Camera attributes
    glm::vec3 Target;
    glm::vec3 WorldUp;
    float Fov; //

    // State for quaternion orbit
    glm::quat Orientation;
    float Distance;

    // Derived attributes (Target, Orientation, Distance)
    glm::vec3 Position;


    // Camera Settings
    float OrbitSensitivity = 0.005f;
    float PanSensitivity = 0.001f;
    float ZoomSensitivity = 0.5f;

    // Input state
    bool LeftMouseDown = false;
    bool RightMouseDown = false;
    bool IsOrbiting = false;
    bool IsPanning = false;
    bool firstMouse = true;
    double LastMouseX = 0.0;
    double LastMouseY = 0.0;



    Camera(glm::vec3 position,
            glm::vec3 target = glm::vec3(0.0f),
            glm::vec3 worldUp = glm::vec3(0.0f, 0.0f, 1.0f),
            float fov = 45.0f);


    // Core methods
    // Calculates the view matrix
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspectRatio, float nearPlane = 0.1f, float farPlane = 100.0f) const;


    // Calculates the basis vectors (from Orientation)
    void GetBasisVectors(glm::vec3& outRight, glm::vec3& outUp, glm::vec3& outForward) const;
    // Calculates the 3x3 basis matrix for the shader
    glm::mat3 GetBasisMatrix() const;

    // Processes input received from GLFW callbacks
    void ProcessOrbit(double xoffset, double yoffset);
    void ProcessPan(double xoffset, double yoffset);
    void ProcessZoom(double yoffset);
    void ProcessKeyboardMovement(GLFWwindow* window, float deltaTime);

    // Static callback functions
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

private:
    // Helper
    void UpdatePositionFromOrientation();

};



