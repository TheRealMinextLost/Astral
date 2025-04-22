// TransformManager.cpp
// Corrected version

#define GLM_ENABLE_EXPERIMENTAL // Keep if needed for gtx includes

#include "TransformManager.h"

#include <iostream> // Keep for potential future debugging
#include <ostream>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <glm/gtx/quaternion.hpp>
#include "GLFW/glfw3.h"

using namespace glm;

TransformManager::TransformManager(Camera *camera, GLFWwindow *window)
    : cameraPtr(camera), windowPtr(window) {
    if (!cameraPtr || !windowPtr) {
        // Consider throwing an exception or setting an error state
        std::cerr << "ERROR: TransformManager requires valid Camera and GLFWwindow pointers!" << std::endl;
    }
}

// Find Helper
SDFObject* TransformManager::findObjectById(std::vector<SDFObject> &objects, int id) {
    if (id == -1) return nullptr;
    for (auto& obj : objects) { // Iterate by reference is correct
        if (obj.id == id) {
            return &obj;
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
    initialOrientation = quat(radians(objPtr->rotation)); // Store as quaternion

    if (mode == TransformMode::SCALING) {
        initialParameters = objPtr->parameters; // Store initial parameters for scaling
    }

    // Record starting mouse position
    modalStartX = mouseX;
    modalStartY = mouseY;
    lastModalMouseX = mouseX; // Initialize last positions to start
    lastModalMouseY = mouseY;

    isAxisConstrained = false; // Reset constraint state
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
    // Check if the object pointer is valid and matches the transforming ID
    if (!objPtr || objPtr->id != transformingObjectId) return;

    TransformMode cancelledMode = currentTransformMode; // Store mode before resetting

    // Reset state variables first
    currentTransformMode = TransformMode::NONE;
    transformingObjectId = -1;
    isAxisConstrained = false;
    constrainedAxis = GizmoAxis::NONE;

    // Restore object's state based on the mode that was just active
    objPtr->position = initialPosition;
    objPtr->rotation = initialRotation;

    // Restore parameters only if we were scaling
    if (cancelledMode == TransformMode::SCALING) {
        objPtr->parameters = initialParameters;
    }
}

TransformManager::InputResult TransformManager::update(std::vector<SDFObject> &objects, int &selectedObjectId) {
    InputResult result;
    if (!cameraPtr || !windowPtr) return result; // Early exit if dependencies are missing

    ImGuiIO& io = ImGui::GetIO();
    int display_w, display_h;
    glfwGetFramebufferSize(windowPtr, &display_w, &display_h);

    SDFObject* selectedObjPtr = findObjectById(objects, selectedObjectId);
    SDFObject* transformingObjPtr = findObjectById(objects, transformingObjectId);

    // --- Modal Mode Input Handling ---
    if (isModalActive() && transformingObjPtr) {
        result.consumedKeyboard = true; // Assume modal mode consumes keyboard inputs generally

        // --- Confirmation / Cancellation ---
        bool confirmPressed = (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.WantCaptureMouse);
        confirmPressed |= (ImGui::IsKeyPressed(ImGuiKey_Enter) && !io.WantCaptureKeyboard);

        bool cancelPressed = (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse);
        cancelPressed |= (ImGui::IsKeyPressed(ImGuiKey_Escape) && !io.WantCaptureKeyboard);

        if (confirmPressed) {
            confirmTransform();
            result.consumedMouse = true; // Consume the confirming left click
        } else if (cancelPressed) {
            TransformMode modeBeforeCancel = currentTransformMode;
            cancelTransform(transformingObjPtr);
            // Decide if cancelling should consume the input (e.g., right-click might be for camera elsewhere)
            // For now, let's say Escape key press is consumed, but Right Mouse isn't always
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                 result.consumedKeyboard = true;
            } else if(ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse) {
                 // Let parent decide if right-click is consumed based on context
                 // result.consumedMouse = true; // Optional: consume right-click if desired
            }
        } else {
            // --- Constraint / Space Toggles (Only if not confirmed/cancelled) ---
            bool constraintChanged = false;
            GizmoAxis newlyPressedAxis = GizmoAxis::NONE;
            if (ImGui::IsKeyPressed(ImGuiKey_X)) newlyPressedAxis = GizmoAxis::X;
            else if (ImGui::IsKeyPressed(ImGuiKey_Y)) newlyPressedAxis = GizmoAxis::Y;
            else if (ImGui::IsKeyPressed(ImGuiKey_Z)) newlyPressedAxis = GizmoAxis::Z;

            if (newlyPressedAxis != GizmoAxis::NONE) {
                if (isAxisConstrained && constrainedAxis == newlyPressedAxis) {
                    isAxisConstrained = false; // Toggle off if same axis pressed again
                    constrainedAxis = GizmoAxis::NONE;
                } else {
                    isAxisConstrained = true; // Enable constraint on new axis press
                    constrainedAxis = newlyPressedAxis;
                }
                constraintChanged = true;
                result.consumedKeyboard = true; // Axis keys are consumed
            }

            if (ImGui::IsKeyPressed(ImGuiKey_L)) {
                 currentGizmoSpace = (currentGizmoSpace == GizmoSpace::LOCAL) ? GizmoSpace::WORLD : GizmoSpace::LOCAL;
                 constraintChanged = true; // Trigger update on space change
                 result.consumedKeyboard = true; // 'L' key is consumed
            }

            // --- Apply Transform based on Mouse Movement or Constraint Change ---
            double currentMouseX, currentMouseY;
            glfwGetCursorPos(windowPtr, &currentMouseX, &currentMouseY);
            // Check if mouse moved significantly since last update
            bool mouseMoved = abs(currentMouseX - lastModalMouseX) > 1e-4 || abs(currentMouseY - lastModalMouseY) > 1e-4;

            // Apply transform if constraint changed OR mouse moved
            if (constraintChanged || mouseMoved) {
                double totalDeltaX = currentMouseX - modalStartX; // Delta from the beginning of modal op
                double totalDeltaY = currentMouseY - modalStartY;

                // Revert object state to initial state *before* calculating the new state based on total delta
                // This prevents accumulating deltas incorrectly frame-to-frame
                transformingObjPtr->position = initialPosition;
                transformingObjPtr->rotation = initialRotation;
                // Note: Parameters don't need reverting here, applyModalScaling works from initialParameters

                // Apply the transform based on the current mode
                if (currentTransformMode == TransformMode::TRANSLATING) {
                    applyModalTranslation(transformingObjPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                } else if (currentTransformMode == TransformMode::ROTATING) {
                    applyModalRotation(transformingObjPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                } else if (currentTransformMode == TransformMode::SCALING) {
                    applyModalScaling(transformingObjPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                }

                // Update the last known mouse position if the mouse actually moved
                if (mouseMoved) {
                    lastModalMouseX = currentMouseX;
                    lastModalMouseY = currentMouseY;
                    result.consumedMouse = true; // Consumed mouse movement for transforming
                }
            }
        }
    }
    // --- Normal Mode Input Handling (Not Modal) ---
    else if (!isModalActive() && !io.WantCaptureKeyboard) { // Ensure ImGui doesn't want keyboard
        bool actionTaken = false;

        // --- Entering Modal Mode ---
        if (selectedObjPtr) { // Can only enter modal if an object is selected
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
                actionTaken = true; // Ensure scaling also sets actionTaken
            }
        }

        // --- Other Normal Mode Actions ---
        if (ImGui::IsKeyPressed(ImGuiKey_D)) { // Example: Deselect Key
            if (selectedObjectId != -1) {
                selectedObjectId = -1; // Deselect
            }
            actionTaken = true;
        }

        // Allow toggling space even when not transforming (if desired)
        if (ImGui::IsKeyPressed(ImGuiKey_L)) {
            currentGizmoSpace = (currentGizmoSpace == GizmoSpace::LOCAL) ? GizmoSpace::WORLD : GizmoSpace::LOCAL;
            actionTaken = true;
        }

        // If any G/R/S/D/L key was pressed, consume keyboard
        if (actionTaken) {
            result.consumedKeyboard = true;
        }

    } // End Normal Input Handling

    // --- Mouse Click for Picking (Example Logic) ---
    // Handle left click for selection/picking only if not modal and ImGui doesn't want the mouse
    // AND if the mouse wasn't consumed by starting a modal operation this frame.
    // This logic might need adjustment based on how picking is triggered in main.cpp
    if (!isModalActive() && !result.consumedMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.WantCaptureMouse) {
         // Set flag or call function to request picking at io.MousePos.x, io.MousePos.y
         // Example: requestPicking(io.MousePos.x, io.MousePos.y);
         result.consumedMouse = true; // Consume the click so it's not used for other things
    }


    return result; // Return consumed status
}


void TransformManager::applyModalTranslation(SDFObject* objPtr, double totalDeltaX, double totalDeltaY, int display_w, int display_h) {
    if (!objPtr || !cameraPtr) return;

    vec3 camRight, camUp, camForward;
    cameraPtr->GetBasisVectors(camRight, camUp, camForward);

    // Sensitivity calculation based on depth from camera to object's initial position
    float depth = distance(cameraPtr->Position, initialPosition);
    depth = max(depth, 0.1f); // Avoid zero or negative depth
    float sensitivityScale = 0.0008f; // Adjust sensitivity as needed
    float adjustedSensitivity = sensitivityScale * depth;

    // Calculate delta in the camera's view plane based on total mouse delta
    vec3 viewPlaneDelta = (camRight * (float)totalDeltaX * adjustedSensitivity) -
                          (camUp * (float)totalDeltaY * adjustedSensitivity); // Invert Y delta for typical screen coords

    vec3 finalDelta = viewPlaneDelta; // Start with unconstrained delta

    // Apply axis constraints if active
    if (isAxisConstrained) {
        vec3 axisVec;
        switch (constrainedAxis) {
            case GizmoAxis::X: axisVec = vec3(1.0f, 0.0f, 0.0f); break;
            case GizmoAxis::Y: axisVec = vec3(0.0f, 1.0f, 0.0f); break;
            case GizmoAxis::Z: axisVec = vec3(0.0f, 0.0f, 1.0f); break;
            default:           axisVec = vec3(0.0f); break; // Should not happen if isAxisConstrained is true
        }

        // Transform axis to world space if in local mode, using initial orientation for consistency
        if (currentGizmoSpace == GizmoSpace::LOCAL) {
            axisVec = initialOrientation * axisVec;
        }

        // Project the calculated viewPlaneDelta onto the constrained axis
        if (length(axisVec) > 1e-6) { // Avoid division by zero
            finalDelta = axisVec * dot(viewPlaneDelta, axisVec) / dot(axisVec, axisVec);
        } else {
            finalDelta = vec3(0.0f); // No movement if axis is invalid
        }
    }

    // Apply the final calculated delta to the initial position
    objPtr->position = initialPosition + finalDelta;
}

void TransformManager::applyModalRotation(SDFObject* objPtr, double totalDeltaX, double totalDeltaY, int display_w, int display_h) {
     if (!objPtr || !cameraPtr) return;

    float angleSensitivity = 0.005f; // Radians per pixel delta (adjust sensitivity)
    float angle = 0.0f;
    vec3 axis = vec3(0.0f, 0.0f, 1.0f); // World Z

    // Determine axis and angle based on constraint
    if (isAxisConstrained) {
         switch (constrainedAxis) {
             // Use horizontal mouse delta for X, Y, Z axis rotation (common convention)
             case GizmoAxis::X: axis = vec3(1.0f, 0.0f, 0.0f); angle = (float)totalDeltaX * angleSensitivity; break;
             case GizmoAxis::Y: axis = vec3(0.0f, 1.0f, 0.0f); angle = (float)totalDeltaX * angleSensitivity; break;
             case GizmoAxis::Z: axis = vec3(0.0f, 0.0f, 1.0f); angle = (float)totalDeltaX * angleSensitivity; break;
             default:           axis = vec3(0.0f); angle = 0.0f; break; // No axis
         }
         // Transform axis if in local space
         if (currentGizmoSpace == GizmoSpace::LOCAL) {
             axis = initialOrientation * axis; // Rotate the local axis to world space
         }
     } else {
         axis = vec3(0.0f, 0.0f, 1.0f); // World Z

         // Unconstrained: Use horizontal mouse movement to rotate around camera view axis
         angle = (float)totalDeltaX * angleSensitivity;
     }

    // Apply rotation if axis is valid
    if (length(axis) > 1e-6) {
        quat deltaRotation = angleAxis(angle, normalize(axis));
        quat finalOrientation = deltaRotation * initialOrientation; // Apply delta cumulatively to initial orientation
        objPtr->rotation = degrees(eulerAngles(finalOrientation)); // Convert final orientation back to Euler degrees
    } else {
        objPtr->rotation = initialRotation; // No rotation if axis is invalid
    }
}

void TransformManager::applyModalScaling(SDFObject *objPtr, double totalDeltaX, double totalDeltaY, int display_w, int display_h) {
    if (!objPtr || !cameraPtr) return;

    // --- Calculate Scale Factor ---
    float scaleSensitivity = 0.008f; // Adjust sensitivity
    float deltaDist = static_cast<float>(totalDeltaX); // Use total horizontal delta from start
    // Calculate a multiplicative factor based on delta
    float scaleFactor = 1.0f + deltaDist * scaleSensitivity;
    // Clamp the factor to prevent zero or negative scale
    scaleFactor = max(0.01f, scaleFactor);

    // --- Determine Scale Vector ---
    glm::vec3 scaleVector(1.0f); // Start with no change

    if (isAxisConstrained) {
        // Apply scale factor only along the constrained axis
        switch (constrainedAxis) {
            case GizmoAxis::X:
                scaleVector.x = scaleFactor;
                break;
            case GizmoAxis::Y:
                scaleVector.y = scaleFactor;
                break;
            case GizmoAxis::Z:
                scaleVector.z = scaleFactor;
                break;
            default: // Should not happen if constrained, but default to uniform
                scaleVector = glm::vec3(scaleFactor);
                break;
        }
        // Note: Scaling usually occurs along local object axes,
        // so GizmoSpace::LOCAL/WORLD typically doesn't change the axis vector here.
    } else {
        // No constraint: Apply uniformly to all axes
        scaleVector = glm::vec3(scaleFactor);
    }

    // --- Apply Scaling ---
    // Multiply the initial parameters by the calculated scaleVector
    objPtr->parameters = initialParameters * scaleVector;

    // Ensure parameters don't become zero or negative (safety clamp)
    objPtr->parameters = max(objPtr->parameters, glm::vec3(1e-6f));
}