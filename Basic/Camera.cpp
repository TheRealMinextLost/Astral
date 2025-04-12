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

void Camera::GetBasisVectors(vec3 &outRight, vec3 &outUp, vec3 &outForward) const {
    outForward = normalize(Target - Position);

    // Recalculate Right vector robustly
    vec3 tempWorldUp = WorldUp;
    // Handle case where looking straight up or down
    if (abs(dot(outForward, tempWorldUp)) > 0.999f) {
        // Use Z-axis if looking straight up/down
        tempWorldUp = vec3(0.0f,0.0f,(outForward.y > 0) ? -1.0f : 1.0f);
    }

    outRight = normalize(cross(outForward, tempWorldUp));
    // Recalculate Up vector
    outUp = normalize(cross(outRight, outForward));
}

mat3 Camera::GetBasisMatrix() const {
    vec3 right, up, forward;
    GetBasisVectors(right, up, forward);
    // GLSL mat3 constructor takes columns. We need rows for basis vectors.
    // Remember camera looks down -Z, so use -forward
    return mat3(right, up, -forward);
}

void Camera::ProcessOrbit(double xoffset, double yoffset) {
    vec3 direction = Position - Target;
    float distance = length(direction);
    if (distance < 1e-4f) return;

    vec3 normalized_direction = normalize(direction);

    vec3 camRight, camUp, camForward;
    GetBasisVectors(camRight, camUp, camForward); // Use the derived 'up' for vertical orbit

    // Horizontal rotation
    float yawAngle = -static_cast<float>(xoffset) * OrbitSensitivity;
    float pitchAngle = -static_cast<float>(yoffset) * OrbitSensitivity;


    mat4 yawRotation = rotate(mat4(1.0f), yawAngle, camUp);
    mat4 pitchRotation = rotate(mat4(1.0f), pitchAngle, camRight);


    // Combine rotations
    vec3 rotatedNormalizedDir = normalize(vec3(pitchRotation * yawRotation * vec4(normalized_direction, 0.0f)));

    // Prevent flipping over the top/bottom
    vec3 potentialNewForward = -rotatedNormalizedDir;
    vec3 potentialNewRight = normalize(cross(potentialNewForward, WorldUp));
    vec3 potentialNewUp = normalize(cross(potentialNewRight, potentialNewForward));
    float dotWorldUp = dot(potentialNewUp, normalize(WorldUp));

    vec3 finalNormalizedDirection;

    if (abs(dotWorldUp) < 0.998f) {
        finalNormalizedDirection = rotatedNormalizedDir;
    } else {
        finalNormalizedDirection = normalize(vec3(yawRotation * vec4(normalized_direction, 0.0f)));
    }


    if (length(finalNormalizedDirection) > 1e-5f) {
        Position = Target + finalNormalizedDirection * distance;
    } else {
        std::cerr << "Orbit Warning: Final direction normalized failed! Resetting position." << std::endl;
        Position = Target + normalized_direction * distance;
    }
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

void Camera::MouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) { return; }

    Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    if (!camera) return;

    // --- Reset firstMouse on ANY button press ---
    if (action == GLFW_PRESS) {
        camera->firstMouse = true;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            camera->LeftMouseDown = true;
            if (useGizmo && ImGuizmo::IsOver()) { /* Gizmo handles click */ }
            else {
                // --- Picking logic will go here (GPU version later) ---
                // Placeholder: Deselect for now
                // selectedObjectId = -1;
                // useGizmo = false;
                // std::cout << "Clicked, but picking not implemented yet." << std::endl;

                 // --- Initiate GPU Picking ---
                 double xpos, ypos;
                 glfwGetCursorPos(window, &xpos, &ypos);
                 requestPicking((int)xpos, (int)ypos); // We'll define this global function/flag
            }
        } else if (action == GLFW_RELEASE) {
            camera->LeftMouseDown = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            camera->RightMouseDown = true;
            glfwGetCursorPos(window, &camera->LastMouseX, &camera->LastMouseY);
        } else if (action == GLFW_RELEASE) {
            camera->RightMouseDown = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
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
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
    Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    if (!camera) return;
    ImGuiIO& io = ImGui::GetIO();

    // Check if Gizmo is active *and* being used or hovered
    bool gizmoConsumingMouse = useGizmo && (ImGuizmo::IsUsing() || ImGuizmo::IsOver());

    if (io.WantCaptureMouse || gizmoConsumingMouse) {
        camera->firstMouse = true; // Reset on interrupt
        return;
    }

    if (camera->firstMouse) {
        camera->LastMouseX = xpos;
        camera->LastMouseY = ypos;
        camera->firstMouse = false;
    }

    double xoffset = xpos - camera->LastMouseX;
    double yoffset = ypos - camera->LastMouseY;
    camera->LastMouseX = xpos;
    camera->LastMouseY = ypos;

    // Process movement based on WHICH mouse button is down
    if (camera->IsOrbiting) { // Middle mouse drag (no shift)
        camera->ProcessOrbit(xoffset, yoffset);
    } else if (camera->IsPanning) { // Middle mouse drag (with shift)
        camera->ProcessPan(xoffset, yoffset);
    }
    // Add right-mouse drag orbit/pan if desired
    // else if (camera->RightMouseDown) { camera->ProcessOrbit(xoffset, yoffset); }

}

void Camera::ScrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);

    // Check if ImGui wants mouse capture first!
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) { // Don't process zoom if ImGui use the scroll
        return;
    }

    Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    if (!camera) return;
    // Process zoom based on vertical scroll (yoffset)
    camera->ProcessZoom(yoffset);
}









