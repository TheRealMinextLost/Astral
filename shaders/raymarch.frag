#version 330 core
out vec4 FragColor;
in vec2 fragCoordScreen; // Input: Screen coords from vertex shader (-1 to 1)

// Uniforms from CPU
uniform vec2 u_resolution;          // Viewport resolution (width, height)
uniform float u_time;               // Time (optional, for animations)
uniform vec3 u_cameraPos;           //
uniform mat3 u_cameraBasis;         // Stores camera's Right, Up, Forward vectors
uniform float u_fov;                // Vertical field of view in degrees

uniform vec3 u_spherePos;
uniform float u_sphereRadius;
uniform vec3 u_sphereColor;

uniform vec3 u_boxCenter;           // Box Center
uniform vec3 u_boxHalfSize;         // Box Size
uniform vec3 u_boxColor;            // Box Color

uniform float u_blendSmoothness;    // 'k' for smin


uniform vec3 u_clearColor;          // Background color
uniform int u_debugMode;

// Ray Marching Parameters
const int MAX_STEPS = 1100;
const float MAX_DIST = 100.0;
const float HIT_THRESHOLD = 0.001;

// --- SDF Function (Sphere) ---
float sdSphere(vec3 p, vec3 center, float radius) {
    return length(p - center) - radius;
}

float sdPlane(vec3 p, float height) {
    // the plane normal is 0,1,0
    // dot(p, vec3(0,1,0) - height = p.y - height
    return p.y - height;
}


// p: point to sample
// center: center position of the box
// b: half-dimensions (size/2) of the box
float sdBox(vec3 p, vec3 center,vec3 b){
    vec3 p_centered = p - center; // Translate point relative to box center
    vec3 q = abs(p_centered) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// NEW: Smooth Minimum function
float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b-a) / k, 0.0, 1.0);
    return mix(b,a,h) - k * h * (1.0 - h);
}

// -- Scene Definition Result
struct SDFResult {
    float dist; // signed distance to the combine scene
    vec3 color; // Color of the closest surface
    // Add other material properties here if needed
};


// -- Scene SDF (can combine multiple shapes here later) --
SDFResult mapTheWorld(vec3 p) {
    // Calculate distance to each object
    float sphereDist = sdSphere(p, u_spherePos, u_sphereRadius);
    float boxDist = sdBox(p,u_boxCenter, u_boxHalfSize); // use sdBox

    // Determine which object is closer (for material properties)
    vec3 color = (sphereDist < boxDist) ? u_sphereColor : u_boxColor;

    // Combine disances smoothly
    float finalDist = smin(sphereDist, boxDist, u_blendSmoothness);

    return SDFResult(finalDist, color);
}

// -- Calculate Normal using Gradient of SDF -- (UPDATED)
// -- Calculate Normal using Gradient of SDF --
// -- Calculate Normal --
vec3 calcNormal(vec3 p) {

    vec2 e = vec2(HIT_THRESHOLD * 0.5, 0.0);
    return normalize(vec3(
    mapTheWorld(p + e.xyy).dist - mapTheWorld(p - e.xyy).dist, // Corrected previous typo attempt too
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

// -- Simple Lambertian Diffuse lighting --
vec3 applyLighting(vec3 hitPos, vec3 normal, vec3 baseColor){
    vec3 lightDir = normalize(vec3(0.8,1.0,-0.5)); // Hardcoded light direction
    float diffuse = max(0.0, dot(normal, lightDir));
    vec3 ambient = vec3(0.1) * baseColor; // Small ambient term

    return ambient + baseColor * diffuse;
}


struct RayMarchResult {
    vec3 color;         // Final Color
    int steps;          // Number of Steps Taken
    bool hit;           // Did the ray hit anything?
    float finalDist;    // Distance at the end
    vec3 hitColor;      // Color of the object hit (or background)
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
            vec3 litColor = applyLighting(p, normal, scene.color);

            // Fog effect (optional)
            //float fogAmount = 1.0 - exp(-totalDist * 0.05);
            //litcolor = mix(lit);
            return RayMarchResult(litColor, i + 1, true, totalDist + scene.dist, scene.color);
        }

        if (totalDist > MAX_DIST){
            // ray missed everything within max distance
            return RayMarchResult(u_clearColor, i, false, totalDist, u_clearColor);
        }

        // Advance ray by the disance to nearest object
        totalDist += max(HIT_THRESHOLD * 0.1, scene.dist);
    }
    return RayMarchResult(u_clearColor, MAX_STEPS, false, totalDist, u_clearColor);
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

        default: // Case 0 : normal rendering
        finalColor = result.color;
        break;
    }

    // Output final color
    FragColor = vec4(finalColor, 1.0);
    // Gamma correction (simple version)
    // FragColor = vec4(pow(color, vec3(1.0/2.2)), 1.0);
}


