//
// Cpp Class, handles player input for the camera
//

#include "Camera.h"
#include <iostream> // For debugging
#include <algorithm>
#include <imgui_impl_glfw.h>
#include "Basic/SDFObject.h"
#include "ImGizmo/ImGuizmo.h"
#include "utilities/utility.h"
#include <imgui.h>
#include "utilities/utility.h"
#include "Basic/SDFObject.h"
#include "vector"
#include "ImGizmo/ImGuizmo.h"


using namespace glm;

void requestPicking(int mouseX, int mouseY);

extern std::vector<SDFObject> sdfObjects;
extern int selectedObjectId;
extern bool useGizmo;



Camera::Camera(vec3 position, vec3 target, vec3 worldUp, float fov)
                : Position(position), Target(target), WorldUp(worldUp), Fov(fov) {}

mat4 Camera::GetViewMatrix() const {
    return lookAt(Position, Target, WorldUp);
}

glm::mat4 Camera::GetProjectionMatrix(float aspectRatio, float nearPlane, float farPlane) const {
    return perspective(radians(Fov), aspectRatio, nearPlane, farPlane);
}

// Inside Camera.cpp
void Camera::GetBasisVectors(vec3 &outRight, vec3 &outUp, vec3 &outForward) const {
    // Forward vector points FROM position TOWARDS target
    outForward = Target - Position;
    float forwardLenSq = dot(outForward, outForward);
    if (forwardLenSq < 1e-10f) { // Position and Target are too close
        // Fallback: Assume looking down -Z from current position
        outForward = vec3(0.0f, 0.0f, -1.0f);
        outRight = vec3(1.0f, 0.0f, 0.0f);
        outUp = vec3(0.0f, 1.0f, 0.0f);
        // std::cerr << "Warning: Camera Position and Target are nearly identical!" << std::endl;
        return;
    }
    outForward = normalize(outForward); // Normalize valid forward vector

    // Calculate Right vector using cross product with a reliable Up direction
    vec3 referenceUp = WorldUp; // Start with world up
    if (abs(dot(outForward, referenceUp)) > 0.9999f) { // If Forward is aligned with WorldUp
        // Use world X axis as reference up instead
        referenceUp = vec3(1.0f, 0.0f, 0.0f);
        // If Forward is ALSO aligned with world X (extremely unlikely unless looking along Y at origin)
        if (abs(dot(outForward, referenceUp)) > 0.9999f) {
            referenceUp = vec3(0.0f, 0.0f, 1.0f); // Use world Z axis
        }
    }

    outRight = normalize(cross(outForward, referenceUp));
    // Check if cross product resulted in zero vector (shouldn't with the referenceUp logic)
    if(length(outRight) < 1e-6f) {
        std::cerr << "Error: Failed to calculate Right vector!" << std::endl;
        // Provide a default orthogonal basis
        outRight = vec3(1.0f, 0.0f, 0.0f);
        outUp = vec3(0.0f, 1.0f, 0.0f);
        return;
    }

    // Recalculate the true Up vector
    outUp = normalize(cross(outRight, outForward));
}

mat3 Camera::GetBasisMatrix() const {
    vec3 right, up, forward; GetBasisVectors(right, up, forward); return mat3(right, up, -forward);
}

// Refined Orbit Logic
void Camera::ProcessOrbit(double xoffset, double yoffset) {
    vec3 directionToCamera = Position - Target; float distance = length(directionToCamera);

    if (distance < 1e-5f) return; vec3 normDirToCamera = normalize(directionToCamera);

    vec3 camRight, camUp, camForward; GetBasisVectors(camRight, camUp, camForward);
    float yawAngle = -static_cast<float>(xoffset) * OrbitSensitivity;
    float pitchAngle = -static_cast<float>(yoffset) * OrbitSensitivity;
    float currentPitch = asin(clamp(dot(normDirToCamera, normalize(WorldUp)), -1.0f, 1.0f));
    float maxPitch = radians(89.9f);

    if (currentPitch + pitchAngle > maxPitch) { pitchAngle = maxPitch - currentPitch; }
    else if (currentPitch + pitchAngle < -maxPitch) { pitchAngle = -maxPitch - currentPitch; }
    mat4 yawRotation = rotate(mat4(1.0f), yawAngle, WorldUp);

    mat4 pitchRotation = rotate(mat4(1.0f), pitchAngle, camRight);
    vec3 finalNormDir = normalize(vec3(yawRotation * pitchRotation * vec4(normDirToCamera, 0.0f)));
    if (isnan(finalNormDir.x) || isinf(finalNormDir.x)) { std::cerr << "Orbit Warning: NaN/Inf detected!" << std::endl; return; }
    Position = Target + finalNormDir * distance;
}

void Camera::ProcessPan(double xoffset, double yoffset) {
    vec3 right, up, forward;
    GetBasisVectors(right, up, forward); // use derived vectors

    // Calculate displacement based on sensitivity
    // Movement amount can depend on distance to keep pan speed consistent-ish
    float distance = max(0.1f, length(Position - Target)); // Ensure distance is positive
    vec3 translation = (-right * static_cast<float>(xoffset) * PanSensitivity * distance) +
                            (up * static_cast<float>(yoffset) * PanSensitivity * distance);

    Position += translation;
    Target += translation;
}

void Camera::ProcessZoom(double yoffset) {
    vec3 direction = Target - Position;
    float distance = length(direction);
    if (distance < 1e-3f && yoffset > 0) return;

    // Calculate new distance, prevent zooming too close or past target
    float newDistance = max(0.1f, distance - static_cast<float>(yoffset) * ZoomSensitivity * distance * 0.1f);

    Position = Target - (normalize(direction) * newDistance);
}

void Camera::ProcessKeyboardMovement(GLFWwindow *window, float deltaTime) {
    float velocity = 5.0f * deltaTime;

    vec3 right, up, forward;
    GetBasisVectors(right, up, forward);

    vec3 moveInput(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        moveInput += forward; // Move forward (along -forward axis)
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        moveInput -= forward; // Move backward
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        moveInput -= right; // Strafe Left
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        moveInput += right; // Strafe Right
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        moveInput += WorldUp; // Move up globally
    if (glfwGetKey(window,GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window,GLFW_KEY_Q) == GLFW_PRESS)
        moveInput -= WorldUp;

    if (length(moveInput) > 1e-6f) {
        vec3 moveDir = normalize(moveInput) * velocity;
        Position += moveDir;
        Target += moveDir;
    }
}

// --- Simplified MouseButtonCallback ---
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

    // Reset firstMouse flag for camera movement delta calculation on any press
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
            std::cout << "Left Mouse Press: Picking requested (ImGui did not capture mouse)" << std::endl;
        } else if (action == GLFW_RELEASE) {
            camera->LeftMouseDown = false;
            std::cout << "Left Mouse Release" << std::endl;
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

    // Check AFTER ImGui processed AND check Gizmo state
    // *** THIS CONDITION IS ESSENTIAL ***
    bool gizmoConsumingMouse = useGizmo && (ImGuizmo::IsUsing() || ImGuizmo::IsOver());
    if (io.WantCaptureMouse || gizmoConsumingMouse) {
        camera->firstMouse = true; // Reset delta calculation on interrupt
        return; // <<<--- MUST return here to prevent camera movement stealing input
    }
    // *** END ESSENTIAL CHECK ***

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