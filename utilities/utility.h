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
}


