//
// Cpp Class, handles player input for the camera
//

#include "Camera.h"
#include <iostream> // For debugging
#include <algorithm>
#include <imgui_impl_glfw.h>

Camera::Camera(glm::vec3 position, glm::vec3 target, glm::vec3 worldUp, float fov)
                : Position(position), Target(target), WorldUp(worldUp), Fov(fov) {}

glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(Position, Target, WorldUp);
}

void Camera::GetBasisVectors(glm::vec3 &outRight, glm::vec3 &outUp, glm::vec3 &outForward) const {
    outForward = glm::normalize(Target - Position);

    // Recalculate Right vector robustly
    glm::vec3 tempWorldUp = WorldUp;
    // Handle case where looking straight up or down
    if (glm::abs(glm::dot(outForward, tempWorldUp)) > 0.999f) {
        // Use Z-axis if looking straight up/down
        tempWorldUp = glm::vec3(0.0f,0.0f,(outForward.y > 0) ? -1.0f : 1.0f);
    }

    outRight = glm::normalize(glm::cross(outForward, tempWorldUp));
    // Recalculate Up vector
    outUp = glm::normalize(glm::cross(outRight, outForward));
}

glm::mat3 Camera::GetBasisMatrix() const {
    glm::vec3 right, up, forward;
    GetBasisVectors(right, up, forward);
    // GLSL mat3 constructor takes columns. We need rows for basis vectors.
    // Remember camera looks down -Z, so use -forward
    return glm::mat3(right, up, -forward);
}

void Camera::ProcessOrbit(double xoffset, double yoffset) {
    glm::vec3 direction = Position - Target;
    float distance = glm::length(direction);
    direction = glm::normalize(direction);

    glm::vec3 right, up, forward;
    GetBasisVectors(right, up, forward); // Use the derived 'up' for vertical orbit

    // Horizontal rotation
    float yawAngle = -xoffset * OrbitSensitivity;
    glm::mat4 yawRotation = glm::rotate(glm::mat4(1.0f), yawAngle, up);

    // Vertical rotation (around derived 'right')
    float pitchAngle = -yoffset * OrbitSensitivity;
    glm::mat4 pitchRotation = glm::rotate(glm::mat4(1.0f), pitchAngle, right);

    // Combine rotations
    direction = glm::vec3(pitchRotation * yawRotation * glm::vec4(direction, 0.0f));

    // Prevent flipping over the top/bottom
    // Check angel between new direction and world up
    float angleWithUp = glm::degrees(acos(glm::dot(glm::normalize(direction), WorldUp)));
    if (angleWithUp < 1.0f || angleWithUp > 179.0f) {
        // Revert pitch if it causes flip
        direction = glm::vec3(yawRotation * glm::vec4(glm::normalize(Position - Target), 0.0f));
    }


    Position = Target + glm::normalize(direction) * distance;

    // Keep camera oriented correctly after orbit, maybe not necessary
    //glm::vec3 newForward = glm::normalize(Target - Position);
    //glm::vec3 newRight = glm::normalize(glm::cross(newForward, WorldUp));
    //WorldUp = glm::normalize(glm::cross(newRight, newForward)); // Update WorldUp based on new orientation
}

void Camera::ProcessPan(double xoffset, double yoffset) {
    glm::vec3 right, up, forward;
    GetBasisVectors(right, up, forward); // use derived vectors

    // Calculate displacement based on sensitivity
    // Movement amount can depend on distance to keep pan speed consistent-ish
    float distance = glm::length(Position - Target);
    glm::vec3 translation = (-right * (float)xoffset * PanSensitivity * distance) +
                            (up * (float)yoffset * PanSensitivity * distance);

    Position += translation;
    Target += translation;
}

void Camera::ProcessZoom(double yoffset) {
    glm::vec3 forward = glm::normalize(Target - Position);
    float distance = glm::length(Position - Target);

    // Calculate new distance, prevent zooming too close or past target
    distance = std::max(0.1f, distance -(float)yoffset * ZoomSensitivity);

    Position = Target - (forward * distance);
}

void Camera::MouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
    Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    if (!camera) return;

    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &camera->LastMouseX, &camera->LastMouseY); // Capture position on press
            if (glfwGetKey(window,GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
                camera->IsPanning = true;
                camera->IsOrbiting = false;

            } else {
                camera->IsOrbiting = true;
                camera->IsPanning = false;

            }
            // Optional: Change cursor shape
            //
        } else if (action == GLFW_RELEASE) {
            camera->IsPanning = false;
            camera->IsOrbiting = false;
            // Optional: Restore cursor shape
            // maybe later
        }
    }
    // Allow ImGui to process mouse buttons too
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
}

void Camera::CursorPosCallback(GLFWwindow *window, double xpos, double ypos) {
    Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    if (!camera) return;

    // Check if ImGui wants mouse capture first!
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        // If ImGui is using the mouse, reset our camera interaction state and don't process camera movement.
        camera->IsPanning = false;
        camera->IsOrbiting = false;
        // Make sure ImGui gets the event
        ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
        return; // Early exit, don't process camera movement.
    }

    // Calculate offset from last frame
    double xoffset = xpos - camera->LastMouseX;
    double yoffset = ypos - camera->LastMouseY; // Y increase downwards typically

    // Update last positions for next frame
    camera->LastMouseX = xpos;
    camera->LastMouseY = ypos;


    if (camera->IsOrbiting) {
        camera->ProcessOrbit(xoffset, yoffset);
    } else if (camera->IsPanning) {
        camera->ProcessPan(xoffset, yoffset);
    }

    // Pass event to ImGui AFTER processing (or before if io.WantCaptureMouse was true)
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
}

void Camera::ScrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
    Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    if (!camera) return;

    // Check if ImGui wants mouse capture first!
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
        return; // early exit, don't process camera zoom
    }

    // Process zoom based on vertical scroll (yoffset)
    camera->ProcessZoom(yoffset);

    // Pass event to ImGui AFTER processing
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
}








