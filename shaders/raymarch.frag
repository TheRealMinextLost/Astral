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
float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b-a) / k, 0.0, 1.0);
    return mix(b,a,h) - k * h * (1.0 - h);
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
    float finalDist = MAX_DIST;
    vec3 finalColor = u_clearColor;
    int closestObjectId = -1;

    for (int i = 0; i < u_sdfCount; ++i) {
        // --- MODIFY ACCESS TO USE THE INSTANCE NAME ---
        mat4 invTransform = sdfBlockInstance.objects[i].inverseModelMatrix;
        vec3 objColor = sdfBlockInstance.objects[i].color.rgb;
        float param1 = sdfBlockInstance.objects[i].params1_3_type.x;
        float param2 = sdfBlockInstance.objects[i].params1_3_type.y;
        float param3 = sdfBlockInstance.objects[i].params1_3_type.z;
        int objType = int(sdfBlockInstance.objects[i].params1_3_type.w);
        // --- END MODIFICATION ---

        // ... rest of the function using objColor, param1, etc. ...
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

        if (i == 0) {
            finalDist = currentObjDist;
            finalColor = objColor;
            closestObjectId = i;
        } else {
            if (currentObjDist < finalDist) {
                finalColor = objColor;
                closestObjectId = i;
            }
            finalDist = smin(finalDist, currentObjDist, u_blendSmoothness);
        }
    }
    bool isSelected = (closestObjectId != -1 && closestObjectId == u_selectedObjectID);
    return SDFResult(finalDist, finalColor, closestObjectId, isSelected);
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
        finalColor += vec3(0.4, 0.4, 0.0); // Slightly stronger yellow highlight
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


