//
// Created by bysta on 11/04/2025.
//
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>

enum class SDFType : int {
    SPHERE = 0, // Sphere and ellipsoids now
    BOX = 1
    // Other SDF here (make them be added dynamically)
};

struct SDFObject {
    int id; // ID each selection
    std::string name = "Object";
    SDFType type = SDFType::SPHERE;

    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // Euler angles for simplicity

    glm::vec3 color = glm::vec3(1.0f);
    glm::vec3 parameters = glm::vec3(0.5f); // Default size or half-size

    // Helper functions
    glm::mat4 getModelMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = translate(model, position);
        model = rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f)); // Z
        model = rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f)); // Y
        model = rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f)); // X
        return model;
    }

    glm::mat4 getInverseModelMatrix() const {
        return inverse(getModelMatrix());
    }

    // Constructor
    SDFObject(int uniqueId, SDFType t = SDFType::SPHERE) : id(uniqueId), type(t) {
        std::string typeName = (type == SDFType::BOX) ? "box" : "sphere"; // Generate the default name based on type and ID
        name = typeName + "_" +std::to_string(uniqueId);
        if (type == SDFType::SPHERE) {
            parameters = glm::vec3(0.5f);
        } else {
            parameters = glm::vec3(0.5f);
        }
    }

    SDFObject() : id(-1) {};

};


struct SDFObjectGPUData {
    glm::mat4 inverseModelMatrix; // 64 bytes (4x vec4)
    glm::vec4 color;              // 16 bytes (vec4)
    glm::vec4 paramsXYZ_type;     // 16 bytes (radius/halfX, halfY, halfZ, type)
};
// --- END ADDITION ---

inline int findObjectIndex(const std::vector<SDFObject>& objects, int uniqueId) {
    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i].id == uniqueId) {
            return static_cast<int>(i);
        }
    }
    return -1; // Not found
}