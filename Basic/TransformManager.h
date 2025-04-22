//
// Created by bysta on 21/04/2025.
//

#pragma once

#include <glm/glm.hpp>
#include <vector>
#include "Basic/SDFObject.h" // Include SDFObject definition
#include "Basic/Camera.h"    // Include Camera definition

struct GLFWwindow;

// Enums can be global or nested within the class
enum class TransformMode { NONE, TRANSLATING, ROTATING, SCALING };
enum class GizmoAxis { NONE, X, Y, Z };
enum class GizmoSpace { WORLD, LOCAL };

class TransformManager {
public:
    struct InputResult {
        bool consumedKeyboard = false;
        bool consumedMouse = false;
    };

    TransformManager(Camera* camera, GLFWwindow* window);

    // Call this once per frame in the main loop
    InputResult update(std::vector<SDFObject>& objects, int& selectedObjectId);

    // Check if a model operation is active
    bool isModalActive() const;

    // Public getters for state (optional, for UI display)
    TransformMode getCurrentMode() const { return currentTransformMode; }
    GizmoAxis getConstrainedAxis() const { return constrainedAxis; }
    GizmoSpace getCurrentSpace() const { return currentGizmoSpace; }
    bool isAxisConstrainedActive() const {return isAxisConstrained; }

private:
    // State Variables
    TransformMode currentTransformMode = TransformMode::NONE;
    GizmoAxis constrainedAxis = GizmoAxis::NONE;
    GizmoSpace currentGizmoSpace = GizmoSpace::WORLD;
    bool isAxisConstrained = false;
    int transformingObjectId = -1; // ID of the object being transformed

    // Initial state for cancellation/delta calculation
    glm::vec3 initialPosition;
    glm::vec3 initialRotation;
    glm::quat initialOrientation;
    glm::vec3 initialParameters;

    // Mouse tracking for modal ops
    double modalStartX = 0.0, modalStartY = 0.0;
    double lastModalMouseX = 0.0, lastModalMouseY = 0.0;

    // Dependencies
    Camera* cameraPtr = nullptr;
    GLFWwindow* windowPtr = nullptr;

    // Private helper methods
    SDFObject* findObjectById(std::vector<SDFObject>& objects, int id);
    void startModalTransform(TransformMode mode, SDFObject* objPtr, double mouseX, double mouseY);
    void confirmTransform();
    void cancelTransform(SDFObject* objPtr);

    // Transformation application logic (moved from main)
    void applyModalTranslation(SDFObject* objPtr, double totalDeltaX, double totalDeltaY, int display_w, int display_h);
    void applyModalRotation(SDFObject* objPtr, double totalDeltaX, double totalDeltaY, int display_w, int display_h);
    void applyModalScaling(SDFObject* objPtr, double totalDeltaX, double totalDeltaY, int display_w, int display_h);


};



