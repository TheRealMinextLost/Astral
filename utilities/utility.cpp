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

int utility::performPickingRaymarch(const Camera& cam, // Use const reference
                               const std::vector<SDFObject>& objects, // Use const reference
                               double mouseX, double mouseY,
                               int screenWidth, int screenHeight) {
    if (objects.empty() || screenWidth <= 0 || screenHeight <= 0) return -1;

    // 1. Calculate Ray
    float ndcX = (2.0f * static_cast<float>(mouseX) / static_cast<float>(screenWidth)) -1.0f;
    float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY) / static_cast<float>(screenHeight)); // Flip Y

    float aspectRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    float tanHalfFov = tan(radians(cam.Fov * 0.5f));

    vec3 viewDir = vec3(ndcX * aspectRatio * tanHalfFov, ndcY * tanHalfFov, -1.0f);
    vec3 rayDir  = normalize(cam.GetBasisMatrix() * viewDir);
    vec3 rayOrigin = cam.Position;


    // 2. Simplified Raymarch Logic (CPU side - less accurate than shader)
    const int PICK_MAX_STEPS = 100;
    const float PICK_MAX_DIST = 100.0f;
    const float PICK_HIT_THRESHOLD = 0.01f;
    float totalDist = 0.0f;

    for (int i = 0; i < PICK_MAX_STEPS; i++) {
        vec3 p = rayOrigin + rayDir * totalDist;
        float minDist = PICK_MAX_DIST;
        int closestObjId = -1;

        // Evaluate distance to all objects
        for (size_t objIdx = 0; objIdx < objects.size(); ++objIdx) {
            const auto& obj = objects[objIdx];
            mat4 invModel = obj.getInverseModelMatrix();
            vec4 pLocal4 = invModel * vec4(p, 1.0f);
            vec3 pLocal = vec3(pLocal4) / pLocal4.w;

            float objDist = PICK_MAX_DIST;
            vec3 fwdScaleApprox = vec3(length(obj.scale));
            float avgScale = length(fwdScaleApprox) / sqrt(3.0f) + 1e-6f;

            if (obj.type == SDFType::SPHERE) {;
                objDist = (length(pLocal) - obj.radius) * avgScale;
            } else if (obj.type == SDFType::BOX) {
                // Box distance needs scaling after calculation
                vec3 q = abs(pLocal) - obj.halfSize;
                float boxDistUnscaled = length(max(q, vec3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0f);
                objDist = boxDistUnscaled * avgScale;
            }

            if (objDist < minDist) {
                minDist = objDist;
                closestObjId =  static_cast<int>(objIdx);
            }
        }

        if (minDist < PICK_HIT_THRESHOLD && closestObjId != -1) {
            return objects[closestObjId].id; // Hit! Return the ID
        }

        if (totalDist > PICK_MAX_DIST) {
            return -1; // missed
        }

        totalDist += glm::max(PICK_HIT_THRESHOLD * 0.1f, minDist); // March forward
    }
    return -1;
}
