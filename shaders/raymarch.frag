#version 460 core


out vec4 FragColor;
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
const int MAX_STEPS = 200;
const float MAX_DIST = 100.0;
const float HIT_THRESHOLD = 0.001;

// --- SDF Function (Sphere) ---
float sdSphereLocal(vec3 p, float radius) {
    return length(p) - radius;
}

float sdBoxLocal(vec3 p, vec3 b) {
    // Assumes p is already in local space, centered at origin
    vec3 q = abs(p) -b ;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
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
    vec4 params1_3_type;
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
    mat4 invTransform0 = sdfBlockInstance.objects[0].inverseModelMatrix;
    vec3 objColor0 = sdfBlockInstance.objects[0].color.rgb;
    float param1_0 = sdfBlockInstance.objects[0].params1_3_type.x;
    float param2_0 = sdfBlockInstance.objects[0].params1_3_type.y;
    float param3_0 = sdfBlockInstance.objects[0].params1_3_type.z;
    int objType0 = int(sdfBlockInstance.objects[0].params1_3_type.w);

    vec4 pLocal4_0 = invTransform0 * vec4(p, 1.0);
    vec3 pLocal0 = pLocal4_0.xyz / pLocal4_0.w;
    vec3 invScaleApprox0 = vec3(length(invTransform0[0].xyz), length(invTransform0[1].xyz), length(invTransform0[2].xyz));
    float avgInvScale0 = length(invScaleApprox0) / sqrt(3.0) + 1e-6;
    float approxFwdScale0 = (avgInvScale0 > 1e-5) ? (1.0 / avgInvScale0) : 1.0;

    float localDist0 = MAX_DIST;
    if (objType0 == 0) { localDist0 = sdSphereLocal(pLocal0, param1_0); }
    else if (objType0 == 1) { localDist0 = sdBoxLocal(pLocal0, vec3(param1_0, param2_0, param3_0)); }

    res.dist = localDist0 * approxFwdScale0;
    res.color = objColor0;
    res.objectId = 0; // Initially closest

    float k = u_blendSmoothness; // Get blend factor from uniform

    // --- Loop through the *rest* of the objects (start from i = 1) ---
    for (int i = 1; i < u_sdfCount; ++i) {
        // Get data for object 'i'
        mat4 invTransform_i = sdfBlockInstance.objects[i].inverseModelMatrix;
        vec3 objColor_i = sdfBlockInstance.objects[i].color.rgb;
        float param1_i = sdfBlockInstance.objects[i].params1_3_type.x;
        float param2_i = sdfBlockInstance.objects[i].params1_3_type.y;
        float param3_i = sdfBlockInstance.objects[i].params1_3_type.z;
        int objType_i = int(sdfBlockInstance.objects[i].params1_3_type.w);

        // Calculate distance to object 'i'
        vec4 pLocal4_i = invTransform_i * vec4(p, 1.0);
        vec3 pLocal_i = pLocal4_i.xyz / pLocal4_i.w;
        vec3 invScaleApprox_i = vec3(length(invTransform_i[0].xyz), length(invTransform_i[1].xyz), length(invTransform_i[2].xyz));
        float avgInvScale_i = length(invScaleApprox_i) / sqrt(3.0) + 1e-6;
        float approxFwdScale_i = (avgInvScale_i > 1e-5) ? (1.0 / avgInvScale_i) : 1.0;

        float localDist_i = MAX_DIST;
        if (objType_i == 0) { localDist_i = sdSphereLocal(pLocal_i, param1_i); }
        else if (objType_i == 1) { localDist_i = sdBoxLocal(pLocal_i, vec3(param1_i, param2_i, param3_i)); }
        float currentObjDist = localDist_i * approxFwdScale_i;

        // *** BLENDING LOGIC ***
        // Calculate smin distance AND blend factor 'h' between
        // the current accumulated result (res.dist, res.color) and the new object (currentObjDist, objColor_i)
        vec2 blend_result = sminVerbose(res.dist, currentObjDist, k); // Use the verbose version

        // Update the distance
        res.dist = blend_result.x;

        // Update the color by mixing using the blend factor 'h'
        // mix(x, y, a): result = x*(1-a) + y*a
        // h=0 means distA was smaller -> use res.color
        // h=1 means distB (currentObjDist) was smaller -> use objColor_i
        res.color = mix(res.color, objColor_i, blend_result.y);

        // Update closest object ID based on blend factor (optional, only if needed for other logic)
        // If h > 0.5, object 'i' has more influence on the final distance/shape.
        if (blend_result.y > 0.5) { // Threshold determines which ID "wins" in blend region
            res.objectId = i;
        }
        // *** END BLENDING LOGIC ***
    }

    // Final check for selection highlight using the determined closestObjectId
    res.isSelected = (res.objectId != -1 && res.objectId == u_selectedObjectID);
    return res; // Return the result with blended distance and color
}

// -- Calculate Normal --
vec3 calcNormal(vec3 p) {
    vec2 e = vec2(HIT_THRESHOLD * 0.5, 0.0);
    return normalize(vec3(
    mapTheWorld(p + e.xyy).dist - mapTheWorld(p - e.xyy).dist,
    mapTheWorld(p + e.yxy).dist - mapTheWorld(p - e.yxy).dist,
    mapTheWorld(p + e.yyx).dist - mapTheWorld(p - e.yyx).dist
    ));
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
    vec3 lightDir = normalize(vec3(0.8, 1.0, -0.5));
    float diffuse = max(0.0, dot(normal, lightDir));
    vec3 ambient = vec3(0.1) * baseColor;

    vec3 finalColor = ambient + baseColor * diffuse;

    // Add selection highlight
    if (isSelected) {
        finalColor += vec3(0.2, 0.2, 0.0); // Slightly stronger yellow highlight
    }

    return clamp(finalColor, 0.0, 1.0);
}


struct RayMarchResult {
    vec3 color;         // Final Color
    int steps;          // Number of Steps Taken
    bool hit;           // Did the ray hit anything?
    float finalDist;    // Distance from origin along ray to the hit point
    int hitObjectID;    // ID of the object hit
    bool hitSelected;   // Was the hit object selected?
};


// -- Ray Marching Function --
RayMarchResult rayMarch(vec3 ro, vec3 rd){
    float totalDist = 0.0;
    for (int i =0; i < MAX_STEPS; i++){
        vec3 p = ro + rd * totalDist;
        SDFResult scene = mapTheWorld(p); // Get distance and color

        if (scene.dist < HIT_THRESHOLD){
            // Hit! Calculate Lighting
            vec3 normal = calcNormal(p);

            vec3 litColor = applyLighting(p, normal, scene.color, scene.isSelected);

            // Fog effect (optional)
            //float fogAmount = 1.0 - exp(-totalDist * 0.05);
            //litcolor = mix(lit);
            return RayMarchResult(litColor, i + 1, true, totalDist, scene.objectId, scene.isSelected);
        }

        if (totalDist > MAX_DIST){
            break;
        }

        // Advance ray by the disance to nearest object
        totalDist += max(HIT_THRESHOLD * 0.1, scene.dist);
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

    vec3 finalColor;

    switch (u_debugMode) {
        case 1: // Show Steps
        float stepsNormalized = float(result.steps) / float(MAX_STEPS);
        finalColor = vec3(stepsNormalized);
        break;
        case 2: // Show Hit/Miss
        finalColor = result.hit ? vec3(1.0) : vec3(0.0); // White for hit black for miss
        break;
        case 3: // Show Normals
        if (result.hit){
            // Recalculate normal at the approximate hit position
            vec3 hitPos = ro + rd * result.finalDist;
            vec3 normal = calcNormal(hitPos);
            finalColor = normal * 0.5 + 0.5; // Map normal range [-1,1] to [0,1] for color
        } else {
            finalColor = vec3(0.0);
        }
        break;
        case 4:
        if(result.hit){
            // Simple ha function to get varie colors from ID
            float hue = fract(float(result.hitObjectID) * 0.61803398875);
            // imple HSV to RGB approximation
            vec3 hsv = vec3(hue, 0.8, 0.8);
            vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 1.0);
            vec3 p = abs(fract(hsv.xxx + K.xyz) * 6.0 - K.www);
            finalColor = hsv.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), hsv.y);

        } else {
            finalColor = u_clearColor;
        }
        break;

        default: // Case 0 : normal rendering
        finalColor = result.color;
        break;
    }

    // Output final color
    FragColor = vec4(finalColor, 1.0);
    // Gamma correction (simple version)
    // FragColor = vec4(pow(color, vec3(1.0/2.2)), 1.0);
}


