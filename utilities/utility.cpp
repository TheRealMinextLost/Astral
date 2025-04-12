//
//
//

#include "utility.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include "glm/fwd.hpp"
#include "glm/vec3.hpp"
#include "Basic/Camera.h"
#include "Basic/SDFObject.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h> // For GetProcessMemoryInfo
#endif

using namespace glm;

// Loads the shader from Path
std::string utility::loadShaderSource(const std::string &filePath) {
    std::ifstream shaderFile(filePath);
    if (!shaderFile.is_open()) {
        std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ" <<  filePath << std::endl;
        return ""; // return empty string on failure
    }
    std::stringstream shaderStream;
    shaderStream << shaderFile.rdbuf(); // Read file's buffer contents into streams
    shaderFile.close();                 // Close file handle
    return shaderStream.str();          // Convert stream into string
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
#endif
    return 0;

}
