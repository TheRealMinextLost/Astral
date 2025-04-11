//
// Created by bysta on 10/04/2025.
//
#pragma once
#include <string>
#include <glm/gtc/type_ptr.hpp>

class utility {
public:
    static std::string loadShaderSource(const std::string& filePath);
    static glm::mat3 createCameraBasis(const glm::vec3& cameraPos, const glm::vec3& cameraTarget);


    // Returns resident set size (physical memory) in bytes, or 0 on failure.
    static size_t getCurrentRSS();

};



