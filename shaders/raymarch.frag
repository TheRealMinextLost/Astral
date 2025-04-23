#version 460 core

// MULTIPLE OUTPUTS
layout (location = 0) out vec4 out_color;
layout (location = 1) out int out_ObjectID;

in vec2 fragCoordScreen; // Input: Screen coords from vertex shader (-1 to 1)

// Uniforms from CPU
uniform vec2 u_resolution;          // Viewport resolution (width, height)
uniform vec3 u_cameraPos;           //
uniform mat3 u_cameraBasis;         // Stores camera's Right, Up, Forward vectors
uniform float u_fov;                // Vertical field of view in degrees

uniform int u_sdfCount;                         // Actual number of objects sent
uniform int u_selectedObjectID;     // ID of the selected object (-1 for none)

uniform float u_blendSmoothness;    // 'k' for smin (Global Blend)

const int MAX_SDF_OBJECTS = 10;

uniform vec3 u_clearColor;          // Background color
uniform int u_debugMode;

// Ray Marching Parameters
const int MAX_STEPS = 500;
const float MAX_DIST = 100.0;
const float HIT_THRESHOLD = 0.001;


// -- SDF FUNCTIONS --
float sdBoxLocal(vec3 p, vec3 b) {
    // Assumes p is already in local space, centered at origin
    vec3 q = abs(p) -b ;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdEllipsoidLocal(vec3 p, vec3 r) {
    r = max(r,vec3(1e-6));
    float k0 = length(p / r);
    float k1 = length(p / (r *r));
    if (k1 < 1e-7) return length(p) - length(r);
    return k0 * (k0 - 1.0) / k1;
}

// Smooth Minimum function
vec2 sminVerbose(float distA, float distB, float k) {
    float h = clamp(0.5 + 0.5 * (distA -distB) / k, 0.0, 1.0);
    // Calculate blended distance
    float blendedDist = mix(distA,distB,h) - k * h * (1.0 - h);
    return vec2(blendedDist,h);
}

// -- Scene Definition Result
struct SDFResult {
    float dist; // signed distance to the combine scene
    vec3 color; // Color of the closest surface
    int objectId; // ID of the object corresponding to 'dist'
    bool isSelected; // Was the closest object the selected one?
};

// --- FIX: UBO Struct Definition ---
struct SDFObjectGPUData {
    mat4 inverseModelMatrix;
    vec4 color;
    vec4 paramsXYZ_type;
};

// --- MODIFIED UBO DEFINITION ---
layout (std140, binding = 0) uniform SDFBlock {
    SDFObjectGPUData objects[MAX_SDF_OBJECTS];
} sdfBlockInstance;


SDFResult mapTheWorld(vec3 p) {
    if (u_sdfCount == 0) { // Handle empty scene
        return SDFResult(MAX_DIST, u_clearColor, -1, false);
    }

    // --- Initialize with the *first* object's data ---
    SDFResult res; // Use the result struct to hold intermediate values
    res.dist = MAX_DIST;
    res.color = u_clearColor;
    res.objectId = - 1;

    float k = u_blendSmoothness; // Get blend factor from uniform

    // --- Loop through the *rest* of the objects (start from i = 1) ---
    for (int i = 0; i < u_sdfCount; ++i) {
        // Get data for object 'i'
        mat4 invTransform_i = sdfBlockInstance.objects[i].inverseModelMatrix; // no scale here
        vec3 objColor_i = sdfBlockInstance.objects[i].color.rgb;
        vec3 params_i = sdfBlockInstance.objects[i].paramsXYZ_type.xyz;
        int objType_i = int(sdfBlockInstance.objects[i].paramsXYZ_type.w);

        // Calculate distance to object 'i'
        vec4 pLocal4_i = invTransform_i * vec4(p, 1.0);
        vec3 pLocal_i = pLocal4_i.xyz / pLocal4_i.w;

        float currentObjDist = MAX_DIST;
        if (objType_i == 0) {
            currentObjDist = sdEllipsoidLocal(pLocal_i, params_i);
        }
        else if (objType_i == 1) {
            currentObjDist = sdBoxLocal(pLocal_i, params_i);
        }

        // Combine with previos result if i > 0
        if (i == 0) {
            res.dist= currentObjDist;
            res.color = objColor_i;
            res.objectId = i;
        } else {
            vec2 blend_result = sminVerbose(res.dist, currentObjDist, k);
            res.dist = blend_result.x;
            res.color = mix(res.color, objColor_i, blend_result.y);
            if (blend_result.y > 0.5) {
                res.objectId = i;
            }
        }
    }

    // Final check for selection highlight using the determined closestObjectId
    res.isSelected = (res.objectId != -1 && res.objectId == u_selectedObjectID);
    return res; // Return the result with blended distance and color
}

// -- Calculate Normal --
vec3 calcNormal(vec3 p, float t) {

    float epsilon = max(t * 0.0005, HIT_THRESHOLD * 0.1); // Don't let epsilon become too small

    vec2 e = vec2(epsilon, 0.0);

    // Use the distance from mapTheWorld directly, which includes the scale factor.
    float dx = mapTheWorld(p + e.xyy).dist - mapTheWorld(p - e.xyy).dist;
    float dy = mapTheWorld(p + e.yxy).dist - mapTheWorld(p - e.yxy).dist;
    float dz = mapTheWorld(p + e.yyx).dist - mapTheWorld(p - e.yyx).dist;

    return normalize(vec3(dx, dy, dz));
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

// -- Simple Lambertian Diffuse lighting + Selection Highlight --
vec3 applyLighting(vec3 hitPos, vec3 normal, vec3 baseColor, bool isSelected) {
    vec3 lightDir = normalize(vec3(0.8, -1.0, 0.5));
    float diffuse = max(0.0, dot(normal, lightDir));
    vec3 ambient = vec3(0.1) * baseColor;

    vec3 litColorWithHighlight  = ambient + baseColor * diffuse;

    // Add selection highlight
    if (isSelected) {
        litColorWithHighlight += vec3(0.2, 0.2, 0.0); // Slightly stronger yellow highlight
    }

    return clamp(litColorWithHighlight , 0.0, 1.0);
}


struct RayMarchResult {
    vec3 color;         // Final Color
    int steps;          // Number of Steps Taken
    bool hit;           // Did the ray hit anything?
    float finalDist;    // Distance from origin along ray to the hit point
    int hitObjectIndex;    // ID of the object hit
    bool hitSelected;   // Was the hit object selected?
};


// -- Ray Marching Function --
RayMarchResult rayMarch(vec3 ro, vec3 rd){
    float totalDist = 0.0;
    for (int i = 0; i < MAX_STEPS; i++){
        vec3 p = ro + rd * totalDist;
        SDFResult scene = mapTheWorld(p); // Get distance and color

        if (scene.dist < HIT_THRESHOLD){
            // Hit! Calculate Lighting
            vec3 normal = calcNormal(p, totalDist);

           vec3 litColor = applyLighting(p, normal, scene.color, scene.isSelected);
            return RayMarchResult(litColor, i + 1, true, totalDist, scene.objectId, scene.isSelected);
        }

        if (totalDist > MAX_DIST){
            break;
        }

        float stepDist = max(HIT_THRESHOLD * 0.1, scene.dist * 0.90);
        totalDist += stepDist;
    }
    // Missed
    return RayMarchResult(u_clearColor, MAX_STEPS, false, totalDist, -1, false);
}

void main()
{
    // Calculate ray origin (ro) and direction (rd)
    vec3 ro = u_cameraPos;
    vec3 rd = getRayDir(fragCoordScreen, u_fov);

    // Perform ray marching
    RayMarchResult result = rayMarch(ro, rd);

    vec3 finalRenderColor;

    switch (u_debugMode) {
        case 1: // Show Steps
        float stepsNormalized = float(result.steps) / float(MAX_STEPS);
        finalRenderColor = vec3(stepsNormalized);
        break;
        case 2: // Show Hit/Miss
        finalRenderColor = result.hit ? vec3(1.0) : vec3(0.0); // White for hit black for miss
        break;
        case 3: // Show Normals
        if (result.hit){
            // Recalculate normal at the approximate hit position
            vec3 hitPos = ro + rd * result.finalDist;
            vec3 normal = calcNormal(hitPos,result.finalDist);
            finalRenderColor = normal * 0.5 + 0.5; // Map normal range [-1,1] to [0,1] for color
        } else {
            finalRenderColor = vec3(0.0);
        }
        break;
        case 4:
        if(result.hit){
            // Simple ha function to get varie colors from ID
            float hue = fract(float(result.hitObjectIndex) * 0.61803398875);
            // imple HSV to RGB approximation
            vec3 hsv = vec3(hue, 0.8, 0.8);
            vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 1.0);
            vec3 p = abs(fract(hsv.xxx + K.xyz) * 6.0 - K.www);
            finalRenderColor = hsv.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), hsv.y);

        } else {
            finalRenderColor = u_clearColor;
        }
        break;

        default: // Case 0 : normal rendering
        finalRenderColor = result.color;
        break;
    }

    // Assign to Outputs
    out_color = vec4(finalRenderColor, 1.0);
    out_ObjectID = result.hitObjectIndex;
}