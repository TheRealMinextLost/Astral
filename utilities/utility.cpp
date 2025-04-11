//
// Created by bysta on 10/04/2025.
//

#include "utility.h"
#include <fstream>
#include <sstream>
#include <iostream> // error reporting
#include "glm/fwd.hpp"
#include "glm/vec3.hpp"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h> // For GetProcessMemoryInfo
#endif

// Loads the shader from Path
std::string utility::loadShaderSource(const std::string &filePath) {

    std::ifstream shaderFile(filePath);
    if (!shaderFile.is_open()) {
        std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" <<  filePath << std::endl;
        return ""; // return empty string on failure
    }
    std::stringstream shaderStream;
    shaderStream << shaderFile.rdbuf(); // Read file's buffer contents into streams
    shaderFile.close();                 // Close file handle
    return shaderStream.str();          // Convert stream into string
}

// Helper function to create camera basis matrix (same as before)
glm::mat3 utility::createCameraBasis(const glm::vec3 &cameraPos, const glm::vec3 &cameraTarget) {
    glm::vec3 forward = glm::normalize(cameraTarget - cameraPos);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(forward, worldUp)) > 0.999f) {
        worldUp = glm::vec3(0.0f, 0.0f, forward.y > 0 ? -1.0f : 1.0f);
    }
    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));
    return glm::mat3(right,up, -forward);
}

size_t utility::getCurrentRSS() {
#ifdef _WIN32
    // Window implementation
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize; // Physical memory usage
    } else {
        return 0; // Failed to get info
    }
#else
    return 0;
#endif
    return 0;

}
