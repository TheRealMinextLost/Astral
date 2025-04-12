//
// Created by bysta on 10/04/2025.
//
#pragma once
#include <string>
#include <vector>
#include <glm/gtc/type_ptr.hpp>

// Forward declaration
struct SDFObject;
class Camera;

namespace utility {
    std::string loadShaderSource(const std::string& filePath);
    size_t getCurrentRSS(); // Platform-specific RAM usage

    // CPU Picking function
    // Takes dependencies as arguments for better design
    int performPickingRaymarch(const Camera& cam, // Use const reference
                               const std::vector<SDFObject>& objects, // Use const reference
                               double mouseX, double mouseY,
                               int screenWidth, int screenHeight);
}


