#version 460 core

layout (location = 0) out int out_ObjectID;

in vec2 fragCoordScreen;

// -- UNIFORMS --
uniform vec2 u_resolution;
uniform vec3 u_cameraPos;
uniform mat3 u_cameraBasis;
uniform float u_fov;
uniform int u_sdfCount;


const int MAX_SDF_OBJECTS = 10;


// MUST match C++ struct definition and std140 alignment rules
struct SDFObjectGPUData {
    mat4 inverseModelMatrix;
    vec4 color;          // vec4(rgb, unused)
    vec4 params1_3_type; // vec4(radius/halfX, halfY, halfZ, float(type))
    // Add other members matching C++ struct
};

// --- MODIFIED UBO DEFINITION ---
layout (std140, binding = 0) uniform SDFBlock {
    SDFObjectGPUData objects[MAX_SDF_OBJECTS];
} sdfBlockInstance;


// Ray Marching Parameters
const int MAX_STEPS = 200;
const float MAX_DIST = 100.0;
const float HIT_THRESHOLD = 0.01;

/// --- SDF Functions ---
float sdSphereLocal(vec3 p, float radius) {
    return length(p) - radius;
}

float sdBoxLocal(vec3 p, vec3 b) {
    // Assumes p is already in local space, centered at origin
    vec3 q = abs(p) -b ;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

struct PickSDFResult{
    float dist;
    int objectIndex; // index (0 to N-1)
};

PickSDFResult mapTheWorldPicking(vec3 p){
    float finalDist = MAX_DIST;
    int closestObjectIndex = -1;

    for (int i = 0; i < u_sdfCount; ++i){
        // --- FIX: Access data using the INSTANCE NAME ---
        mat4 invTransform = sdfBlockInstance.objects[i].inverseModelMatrix;
        float param1 = sdfBlockInstance.objects[i].params1_3_type.x;
        float param2 = sdfBlockInstance.objects[i].params1_3_type.y;
        float param3 = sdfBlockInstance.objects[i].params1_3_type.z;
        int objType = int(sdfBlockInstance.objects[i].params1_3_type.w);
        // --- END FIX ---

        vec4 pLocal4 = invTransform * vec4(p, 1.0);
        vec3 pLocal = pLocal4.xyz / pLocal4.w;

        float currentObjDist = MAX_DIST;
        vec3 invScaleApprox = vec3(length(invTransform[0].xyz), length(invTransform[1].xyz), length(invTransform[2].xyz));
        float avgInvScale = length(invScaleApprox) / sqrt(3.0) + 1e-6;
        float approxFwdScale = (avgInvScale > 1e-5) ? (1.0 / avgInvScale) : 1.0;

        float localDist = MAX_DIST;
        if (objType == 0) { // Sphere
            localDist = sdSphereLocal(pLocal, param1);
        } else if (objType == 1) { // Box
            localDist = sdBoxLocal(pLocal, vec3(param1, param2, param3));
        }
        currentObjDist = localDist * approxFwdScale;

        if (currentObjDist < finalDist) {
            finalDist = currentObjDist;
            closestObjectIndex = i;
        }
    }
    return PickSDFResult(finalDist, closestObjectIndex);
}

int rayMarchPicking(vec3 ro, vec3 rd) {
    float totalDist = 0.0;
    for (int i = 0; i < MAX_STEPS; i++) {
        vec3 p = ro + rd * totalDist;
        PickSDFResult scene = mapTheWorldPicking(p);

        if (scene.dist < HIT_THRESHOLD) {
            return scene.objectIndex; // Hit! return the index.
        }
        if (totalDist > MAX_DIST) {
            return -1; // Missed max distance
        }
        totalDist += max(HIT_THRESHOLD * 0.1, scene.dist); // ensure progress
    }
    return -1;
}


// -- Calcualte Ray Direction --
vec3 getRayDir(vec2 screenPos, float fov){
    vec2 uv = screenPos; // Already in -1 to 1 range

    float aspectRatio = u_resolution.x / u_resolution.y;
    float tanHalfFov = tan(radians(fov * 0.5));

    vec3 viewDir = vec3(uv.x * aspectRatio * tanHalfFov, uv.y * tanHalfFov, -1.0);

    // Convert view direction to world space using camera basis
    return normalize(u_cameraBasis * viewDir);
}



void main() {
    vec3 ro = u_cameraPos;
    vec3 rd = getRayDir(fragCoordScreen, u_fov);

    // Perform simplified raymarch just to get the object index
    int hitObjectIndex = rayMarchPicking(ro, rd);

    // Output the object index(or -1 for background)
    out_ObjectID = hitObjectIndex;
}