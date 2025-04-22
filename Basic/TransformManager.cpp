//
// Handle Input of SDF's
//
#define GLM_ENABLE_EXPERIMENTAL

#include "TransformManager.h"

#include <iostream>
#include <ostream>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <glm/gtx/quaternion.hpp>
#include "GLFW/glfw3.h"

using namespace glm;

TransformManager::TransformManager(Camera *camera, GLFWwindow *window)
    : cameraPtr(camera), windowPtr(window) {
    if (!cameraPtr || !windowPtr) {
        std::cerr << "ERROR: TransformManager requires valid Camera and GLFWwindow pointers!" << std::endl;
    }
}

// Find Helper
SDFObject* TransformManager::findObjectById(std::vector<SDFObject> &objects, int id) {
    if (id == -1) return nullptr;
    for (auto& objPtr : objects) {
        if (objPtr.id == id) {
            return &objPtr;
        }
    }
    return nullptr;
}

bool TransformManager::isModalActive() const {
    return currentTransformMode != TransformMode::NONE;
}

// Start a transformation
void TransformManager::startModalTransform(TransformMode mode, SDFObject *objPtr, double mouseX, double mouseY) {
    if (!objPtr) return;

    currentTransformMode = mode;
    transformingObjectId = objPtr->id;

    // Store initial state
    initialPosition = objPtr->position;
    initialRotation = objPtr->rotation;
    initialScale = objPtr->scale;
    initialOrientation = quat(radians(objPtr->rotation)); // Store as quaternion

    // Record starting mouse position
    modalStartX = mouseX;
    modalStartY = mouseY;
    lastModalMouseX = mouseX;
    lastModalMouseY = mouseY;

    isAxisConstrained = false;
    constrainedAxis = GizmoAxis::NONE;
}

// Confirm the transformation
void TransformManager::confirmTransform() {
    currentTransformMode = TransformMode::NONE;
    transformingObjectId = -1;
    // Reset constraint state for next time
    isAxisConstrained = false;
    constrainedAxis = GizmoAxis::NONE;
}

// Cancel the transformation
void TransformManager::cancelTransform(SDFObject* objPtr) {
    if (!objPtr || objPtr->id != transformingObjectId) return;

    objPtr->position = initialPosition;
    objPtr->rotation = initialRotation;
    objPtr->scale = initialScale;

    currentTransformMode = TransformMode::NONE;
    transformingObjectId = -1;
    isAxisConstrained = false;
    constrainedAxis = GizmoAxis::NONE;
}

TransformManager::InputResult TransformManager::update(std::vector<SDFObject> &objects, int &selectedObjectId) {
    InputResult result;
    if (!cameraPtr || !windowPtr) return result;

    ImGuiIO& io = ImGui::GetIO();
    int display_w, display_h;
    glfwGetFramebufferSize(windowPtr, &display_w, &display_h);

    SDFObject* selectedObjPtr = findObjectById(objects, selectedObjectId);
    SDFObject* transformingObjPtr = findObjectById(objects, transformingObjectId);

    // -- Modal Mode input handling --
    if (isModalActive() && transformingObjPtr) {
        result.consumedKeyboard = true; // Assume the keyboard is consumed if modal

        bool confirmPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.WantCaptureMouse;
        confirmPressed |= ImGui::IsKeyPressed(ImGuiKey_Enter) && !io.WantCaptureKeyboard;

        bool cancelPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse;
        cancelPressed |= ImGui::IsKeyPressed(ImGuiKey_Escape) && !io.WantCaptureKeyboard;


        if (confirmPressed) {
            confirmTransform();
            result.consumedKeyboard = true; // Consumed the click
        } else if (cancelPressed) {
            cancelTransform(transformingObjPtr);
            // Don't consume right-click/escape if it wasn't ImGui capturing it, allow other actions
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && !(ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse)) {
                result.consumedKeyboard = true; // Consumed the click
            }

        } else if (!io.WantCaptureMouse) {
            GizmoAxis newlyPressedAxis = GizmoAxis::NONE;
            if (ImGui::IsKeyPressed(ImGuiKey_X)) newlyPressedAxis = GizmoAxis::X;
            else if (ImGui::IsKeyPressed(ImGuiKey_Y)) newlyPressedAxis = GizmoAxis::Y;
            else if (ImGui::IsKeyPressed(ImGuiKey_Z)) newlyPressedAxis = GizmoAxis::Z;

            bool constraintChanged = false;
            if (newlyPressedAxis != GizmoAxis::NONE) {
                if (isAxisConstrained && constrainedAxis == newlyPressedAxis) {
                    isAxisConstrained = false;
                    constrainedAxis = GizmoAxis::NONE;

                } else {
                    isAxisConstrained = true;
                    constrainedAxis = newlyPressedAxis;

                }
                constraintChanged = true;
            }

            // Local/World Space Toggle (L)
            if (ImGui::IsKeyPressed(ImGuiKey_L)) {
                currentGizmoSpace = (currentGizmoSpace == GizmoSpace::LOCAL) ? GizmoSpace::WORLD : GizmoSpace::LOCAL;
                constraintChanged = true;
            }

            // Apply Transform Immediately on Constraint/Space Change
            if (constraintChanged) {
                double currentMouseX, currentMouseY;
                glfwGetCursorPos(windowPtr, &currentMouseX, &currentMouseY);
                double totalDeltaX = currentMouseX - modalStartX;
                double totalDeltaY = currentMouseY - modalStartY;

                // Revert to inital state FIRST
                transformingObjPtr->position = initialPosition;
                transformingObjPtr->rotation = initialRotation;
                transformingObjPtr->scale = initialScale;

                // Apply based on the current mode
                if (currentTransformMode == TransformMode::TRANSLATING) {
                    applyModalTranslation(transformingObjPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                } else if (currentTransformMode == TransformMode::ROTATING) {
                    applyModalRotation(transformingObjPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                } else if (currentTransformMode == TransformMode::SCALING) {
                    applyModalScaling(transformingObjPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                }
                lastModalMouseX = currentMouseX;
                lastModalMouseY = currentMouseY;
            }

        } // End keyboard checks

        // Mouse movement Transformation
        if (isModalActive()) {
            double currentMouseX, currentMouseY;
            glfwGetCursorPos(windowPtr, &currentMouseX, &currentMouseY);

            // Check if mouse actually moved significantly
            if (abs(currentMouseX - lastModalMouseX) > 1e-4 || abs(currentMouseY - lastModalMouseY) > 1e-4) {
                double totalDeltaX = currentMouseX - modalStartX;
                double totalDeltaY = currentMouseY - modalStartY;

                // Revert to initial state FIRST
                transformingObjPtr->position = initialPosition;
                transformingObjPtr->rotation = initialRotation;
                transformingObjPtr->scale = initialScale;

                // Apply based on the current mode
                if (currentTransformMode == TransformMode::TRANSLATING) {
                    applyModalTranslation(transformingObjPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                } else if (currentTransformMode == TransformMode::ROTATING) {
                    applyModalRotation(transformingObjPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                } else if (currentTransformMode == TransformMode::SCALING) {
                    applyModalScaling(transformingObjPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                }
                lastModalMouseX = currentMouseX;
                lastModalMouseY = currentMouseY;
            }
        }

    } // end Modal input handling


    // Normal Mode Input Handling (Entering Modal, Deselect, Space Toggle)
    else if (!isModalActive() && !io.WantCaptureKeyboard) {
        bool actionTaken = false;
        // Entering modal
        if (selectedObjPtr) {
            double currentMouseX, currentMouseY;
            glfwGetCursorPos(windowPtr, &currentMouseX, &currentMouseY);

            if (ImGui::IsKeyPressed(ImGuiKey_G)) {
                startModalTransform(TransformMode::TRANSLATING, selectedObjPtr, currentMouseX, currentMouseY);
                actionTaken = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                startModalTransform(TransformMode::ROTATING, selectedObjPtr, currentMouseX, currentMouseY);
                actionTaken = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_S)) {
                startModalTransform(TransformMode::SCALING, selectedObjPtr, currentMouseX, currentMouseY);
            }
        }

        // Other actions
        if (ImGui::IsKeyPressed(ImGuiKey_D)) {
            if (selectedObjectId != -1) {
                selectedObjectId = -1;
            }
            actionTaken = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_L)) {
            currentGizmoSpace = (currentGizmoSpace == GizmoSpace::LOCAL) ? GizmoSpace::WORLD : GizmoSpace::LOCAL;
            actionTaken = true;
        }
        if (actionTaken) {
            result.consumedKeyboard = true;
        }

    } // End Normal Input Handling

    return result;
}


void TransformManager::applyModalTranslation(SDFObject* objPtr, double totalDeltaX, double totalDeltaY, int display_w, int display_h) {
    if (!objPtr || !cameraPtr) return;

    // Get camera basis
    vec3 camRight, camUp, camForward;
    cameraPtr->GetBasisVectors(camRight, camUp, camForward); // Assumes camera object is accessible

    // Calculate World Delta Based on screen delta
    float depth = distance(cameraPtr->Position, initialPosition);
    depth = max(depth, 0.1f);
    float sensitivityScale = 0.0008f; // Adjust the magic number
    float adjustedSensitivity = sensitivityScale * depth;

    vec3 viewPlaneDelta = (camRight * (float)totalDeltaX * adjustedSensitivity) -
                                (camUp * (float)totalDeltaY * adjustedSensitivity);

    vec3 finalDelta = viewPlaneDelta;

    if (isAxisConstrained) {
        vec3 axisVec;
        switch (constrainedAxis) {
            case GizmoAxis::X: axisVec = vec3(1.0f, 0.0f, 0.0f); break;
            case GizmoAxis::Y: axisVec = vec3(0.0f, 1.0f, 0.0f); break;
            case GizmoAxis::Z: axisVec = vec3(0.0f, 0.0f, 1.0f); break;
            default:           axisVec = vec3(0.0f); break;
        }

        if (currentGizmoSpace == GizmoSpace::LOCAL) {
            axisVec = initialOrientation * axisVec;
        }

        // Project the viewPlaneDelta onto the constrained axis
        if (length(axisVec) > 1e-6) {
            finalDelta = axisVec * dot(viewPlaneDelta, axisVec) / dot(axisVec, axisVec);
        } else {
            finalDelta = vec3(0.0f);
        }
    }

    objPtr->position = initialPosition + finalDelta;

}

void TransformManager::applyModalRotation(SDFObject* objPtr, double totalDeltaX, double totalDeltaY, int display_w, int display_h) {
    if (!objPtr || !cameraPtr) return;
    float angleSensitivity = 0.005f;
    float angle = (float)totalDeltaX * angleSensitivity;
    vec3 axis = vec3(0.0f, 0.0f, 1.0f); // World Z

    if (isAxisConstrained) {
        switch (constrainedAxis) {
            case GizmoAxis::X: axis = vec3(1.0f, 0.0f, 0.0f); angle = (float)totalDeltaX * angleSensitivity; break;
            case GizmoAxis::Y: axis = vec3(0.0f, 1.0f, 0.0f); angle = (float)totalDeltaX * angleSensitivity; break;
            case GizmoAxis::Z: axis = vec3(0.0f, 0.0f, 1.0f); angle = (float)totalDeltaX * angleSensitivity; break;
            default:           axis = vec3(0.0f); angle = 0.0f; break;
        }
        if (currentGizmoSpace == GizmoSpace::LOCAL) {
            axis = initialOrientation * axis;
        }
    } else {
        axis = vec3(0.0f, 0.0f, 1.0f); // World Z
        angle = (float)totalDeltaX * angleSensitivity;
    }

    if (length(axis) > 1e-6) {
        quat deltaRotation = angleAxis(angle, normalize(axis));
        quat finalOrientation = deltaRotation * initialOrientation;
        objPtr->rotation = degrees(eulerAngles(finalOrientation)); // Store as degrees
    } else {
        objPtr->rotation = initialRotation;
    }
}

void TransformManager::applyModalScaling(SDFObject *objPtr, double totalDeltaX, double totalDeltaY, int display_w, int display_h) {
    if (!objPtr || !cameraPtr) return;
    float scaleSensitivity = 0.005f;
    auto deltaDist = static_cast<float>(totalDeltaX);
    float scaleFactor = 1.0f + deltaDist * scaleSensitivity;
    scaleFactor = max(0.01f, scaleFactor);

    vec3 finalScale = initialScale;

    if (isAxisConstrained) {
        vec3 scaleMask(0.0f);
        switch (constrainedAxis) {
            case GizmoAxis::X: scaleMask.x = 1.0f; break;
            case GizmoAxis::Y: scaleMask.y = 1.0f; break;
            case GizmoAxis::Z: scaleMask.z = 1.0f; break;
            default: break;
        }

        finalScale = initialScale * (vec3(1.0f) + (scaleFactor - 1.0f) * scaleMask);
    } else {
        finalScale = initialScale * scaleFactor;
    }

    objPtr->scale = finalScale;
}




