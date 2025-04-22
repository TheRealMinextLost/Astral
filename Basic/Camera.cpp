//
// Cpp Class, handles player input for the camera
//
#define GLM_ENABLE_EXPERIMENTAL

#include "Camera.h"
#include <iostream> // For debugging
#include <imgui_impl_glfw.h>
#include "Basic/SDFObject.h"
#include "utilities/utility.h"
#include <imgui.h>
#include "vector"
#include "GLFW/glfw3.h"
#include <glm/gtx/quaternion.hpp> // Include quaternion helpers like lookAt, angleAxis




// External global referenced by callbacks
extern std::vector<SDFObject> sdfObjects;
extern int selectedObjectId;
extern bool useGizmo;
extern void requestPicking(int mouseX, int mouseY);

using namespace glm;

Camera::Camera(vec3 position, vec3 target, vec3 worldUp, float fov)
                : Target(target), WorldUp(worldUp), Fov(fov) {
    Distance = distance(position, target);
    if (Distance < 1e-5f) {
        Distance = 5.0f; // Default distance if too close
        position = target + vec3(0.0f, -Distance, 0.0f);
    }
    Position = position;

    // Calculate intial orientation quaternion
    vec3 lookDirection = normalize(Target - Position);
    Orientation = quatLookAt(lookDirection,WorldUp); // Look from position toward target

    firstMouse = true;
    LeftMouseDown = false;
    RightMouseDown = false;
    IsOrbiting = false;
    IsPanning = false;

    UpdatePositionFromOrientation();
}

void Camera::UpdatePositionFromOrientation() {
    // Calculate the forward vector relative to the orientation
    // The position offset should be along this direction, scaled by distance
    vec3 offsetDirection = Orientation * vec3(0.0f, 0.0f, 1.0f);
    Position = Target + offsetDirection * Distance;
}

// Core methods
mat4 Camera::GetViewMatrix() const {
    vec3 currentUp = Orientation * vec3(0.0f, 1.0f, 0.0f);
    return lookAt(Position, Target, currentUp);
}

glm::mat4 Camera::GetProjectionMatrix(float aspectRatio, float nearPlane, float farPlane) const {
    return perspective(radians(Fov), aspectRatio, nearPlane, farPlane);
}

// Inside Camera.cpp
void Camera::GetBasisVectors(vec3 &outRight, vec3 &outUp, vec3 &outForward) const {
    // Derive basis vectors directly from the orientation quaternion
    outRight = Orientation * vec3(0.0f, 0.0f, -1.0f); // Camera looks down -Z
    outUp = Orientation * vec3(0.0f, 1.0f, 0.0f);       // Camera local up is +Y
    outRight = Orientation * vec3(1.0f, 0.0f, 0.0f);    // Camera local right is +X
}

mat3 Camera::GetBasisMatrix() const {
    return mat3_cast(Orientation);
}

// Quaternion-based Orbit
void Camera::ProcessOrbit(double xoffset, double yoffset) {
    if (abs(xoffset) < 1e-6f && abs(yoffset) < 1e-6f) return;

    float yawAngle = -static_cast<float>(xoffset) * OrbitSensitivity;
    float pitchAngle = -static_cast<float>(yoffset) * OrbitSensitivity;

    // Create Yaw rotation around the world Up axis
    quat yawRotation = angleAxis(yawAngle, WorldUp);

    // Create Pitch rotation around the Camera's Local Right axis
    vec3 localRight = Orientation * vec3(1.0f, 0.0f, 0.0f);
    quat pitchRotation = angleAxis(pitchAngle, localRight);

    // Check if applying pitch would violate the limit
    quat potentialOrientationAfterPitch = normalize(pitchRotation * Orientation);
    vec3 potentialUpAfterPitch = potentialOrientationAfterPitch * vec3(0.0f, 1.0f, 0.0f);

    // Define the limit
    float minAngleWithWorldUp = radians(0.5f);
    float minAllowedDotUp = sin(minAngleWithWorldUp);


    bool pitchBlocked = false;
    if (dot(potentialUpAfterPitch, WorldUp) < minAllowedDotUp && pitchAngle < 0.0f) {
        pitchBlocked = true;
        std::cout << "Pitch Down Clamped" << std::endl;
    } else if (dot(potentialUpAfterPitch, WorldUp) < minAllowedDotUp && pitchAngle > 0.0f) {
        pitchBlocked = true;
        std::cout << "Pitch Up Clamped" << std::endl;
    }

    // Combine rotations: Apply pitch locally, then yaw globally
    quat finalDeltaRotation;
    if (pitchBlocked) {
        finalDeltaRotation = yawRotation;
    } else {
        finalDeltaRotation = yawRotation * pitchRotation;
    }

    // Apply the calculated rotation
    Orientation = normalize(finalDeltaRotation * Orientation);

    // Update Position
    UpdatePositionFromOrientation();


}

void Camera::ProcessPan(double xoffset, double yoffset) {
    vec3 right, up, forward;
    GetBasisVectors(right, up, forward); // use derived vectors

    // Panning moves the target point perpendicular to the view direction
    float distFactor = max(0.1f, Distance) * PanSensitivity;
    vec3 translation = (-right * static_cast<float>(xoffset) * distFactor) +
                        (up * static_cast<float>(yoffset) * distFactor);

    //Apply translation to the target Point
    Target += translation;

    // Position automatically follows the target when recalculated
    UpdatePositionFromOrientation();
}

void Camera::ProcessZoom(double yoffset) {
    // Adjust distance exponentially or linearly
    float zoomFactor = pow(0.95f, static_cast<float>(yoffset));
    float deltaDist = -static_cast<float>(yoffset) * ZoomSensitivity * zoomFactor;
    float newDistance = max(0.1f, Distance + deltaDist);

    if (abs(newDistance - Distance) > 1e-6f) {
        Distance = newDistance;
        UpdatePositionFromOrientation();
    }
}

void Camera::ProcessKeyboardMovement(GLFWwindow *window, float deltaTime) {
    float velocity = 2.0f * Distance * deltaTime;
    velocity = max(0.5f * deltaTime, velocity);

    vec3 right, up, forward;
    GetBasisVectors(right, up, forward);

    if (ImGui::IsMouseDown(RightMouseDown)) {
        vec3 moveInputWorld(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveInputWorld += forward;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveInputWorld -= forward;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveInputWorld -= right;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveInputWorld += right;
        // Global up/down movement
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) moveInputWorld += WorldUp;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) moveInputWorld -= WorldUp;

        if (length(moveInputWorld) > 1e-6f) {
            vec3 moveDelta= normalize(moveInputWorld) * velocity;
            Target += moveDelta;
            UpdatePositionFromOrientation();
        }
    }

}

// --- Simplified MouseButtonCallback ---
void Camera::MouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
    // Let ImGui process the event first
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    ImGuiIO& io = ImGui::GetIO();

    // If ImGui handled the click (e.g., on a UI window), do nothing further.
    if (io.WantCaptureMouse) {
        // Reset camera drag states just in case they were set before ImGui captured
        Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
        if (camera) {
            camera->LeftMouseDown = false;
            camera->RightMouseDown = false;
            camera->IsPanning = false;
            camera->IsOrbiting = false;
        }
        return; // Exit early, ImGui has priority
    }

    // --- If ImGui did NOT capture the mouse ---
    Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    if (!camera) return;

    // Reset first Mouse flag for camera movement delta calculation on any press
    if (action == GLFW_PRESS) {
        camera->firstMouse = true;
    }

    // Handle Left Mouse Button for Picking
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            camera->LeftMouseDown = true;
            // ALWAYS request picking if ImGui didn't capture the mouse.
            // Selection state (useGizmo=true) will be set by handlePickingRequest.
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            requestPicking((int)xpos, (int)ypos);
        } else if (action == GLFW_RELEASE) {
            camera->LeftMouseDown = false;
        }
    }
    // Handle Right Mouse Button for Camera (Example: Orbit/Pan start)
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            camera->RightMouseDown = true;
            glfwGetCursorPos(window, &camera->LastMouseX, &camera->LastMouseY);
            // Potentially set camera->IsOrbiting or IsPanning here if using right-drag
        } else if (action == GLFW_RELEASE) {
            camera->RightMouseDown = false;
            // Potentially reset camera->IsOrbiting or IsPanning here
        }
    }
    // Handle Middle Mouse Button for Camera (Orbit/Pan)
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
         if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &camera->LastMouseX, &camera->LastMouseY);
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
                camera->IsPanning = true; camera->IsOrbiting = false;
            } else {
                camera->IsOrbiting = true; camera->IsPanning = false;
            }
        } else if (action == GLFW_RELEASE) {
            camera->IsPanning = false; camera->IsOrbiting = false;
        }
    }
}

void Camera::CursorPosCallback(GLFWwindow *window, double xpos, double ypos) {
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos); // ImGui FIRST
    Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window)); if (!camera) return;
    ImGuiIO& io = ImGui::GetIO();

   // Corrected check
    if (io.WantCaptureMouse) {
        camera->firstMouse = true;
        return;
    }

    // --- Camera movement logic (Orbit/Pan) ---
    // Only runs if ImGui AND the gizmo did NOT capture the mouse
    if (camera->firstMouse) { camera->LastMouseX = xpos; camera->LastMouseY = ypos; camera->firstMouse = false; }
    double xoffset = xpos - camera->LastMouseX; double yoffset = ypos - camera->LastMouseY;
    camera->LastMouseX = xpos; camera->LastMouseY = ypos;

    // Process camera movement ONLY if the corresponding mouse buttons/states are active
    // (e.g., middle mouse for orbit/pan)
    if (camera->IsOrbiting) { camera->ProcessOrbit(xoffset, yoffset); }
    else if (camera->IsPanning) { camera->ProcessPan(xoffset, yoffset); }
    // else if (camera->RightMouseDown) { /* Optional right-drag camera */ }
}

// No changes needed for ScrollCallback
void Camera::ScrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset); // ImGui FIRST
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) { return; } // Check AFTER ImGui processed
    Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window)); if (!camera) return;
    camera->ProcessZoom(yoffset);
}

