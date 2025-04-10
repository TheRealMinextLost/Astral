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
uniform vec3 u_clearColor;          // Background color

uniform int u_debugMode;

// Ray Marching Parameters
const int MAX_STEPS = 100;
const float MAX_DIST = 100.0;
const float HIT_THRESHOLD = 0.001;

// --- SDF Function (Sphere) ---
float sdSphere(vec3 p, vec3 center, float radius) {
    return length(p - center) - radius;
}

// -- Scene SDF (can combine multiple shapes here later) --
float mapTheWorld(vec3 p) {
    // For now, just our single sphere
    return sdSphere(p, u_spherePos, u_sphereRadius);
}

// -- Calculate Normal using Gradient of SDF --
vec3 calcNormal(vec3 p) {
    vec2 e = vec2(HIT_THRESHOLD * 0.5, 0.0); // Small offset
    return normalize(vec3(
        mapTheWorld(p + e.xxy) - mapTheWorld(p- e.xyy),
        mapTheWorld(p + e.yxy) - mapTheWorld(p - e.yxy),
        mapTheWorld(p + e.yyx) - mapTheWorld(p - e.yyx)
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
};


// -- Ray Marching Function --
RayMarchResult rayMarch(vec3 ro, vec3 rd){
    float totalDist = 0.0;
    for (int i =0; i < MAX_STEPS; i++){
        vec3 p = ro + rd * totalDist;
        float distToScene = mapTheWorld(p);

        if (distToScene < HIT_THRESHOLD){
            // Hit! Calculate Lighting
            vec3 normal = calcNormal(p);
            vec3 finalColor = applyLighting(p, normal, u_sphereColor);

            // Fog effect (optional)
            float fogAmount = 1.0 - exp(-totalDist * 0.05);
            vec3 MixedColor = mix(finalColor, u_clearColor, fogAmount);
            return RayMarchResult(MixedColor, i + 1, true, distToScene);
        }

        if (totalDist > MAX_DIST){
            // ray missed everything within max distance
            return RayMarchResult(u_clearColor, i, false, distToScene);
        }

        // Advance ray by the disance to nearest object
        totalDist += distToScene;
    }
    return RayMarchResult(u_clearColor, MAX_STEPS, false, mapTheWorld(ro + rd * totalDist));
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
            vec3 normal = calcNormal(ro + rd * (length(ro-u_spherePos)-u_sphereRadius)); // Recalc normal approx
            finalColor = normal * 0.5 + 0.5; // Map normal range [-1,1] to [0,1] for color
        } else {
            finalColor = vec3(0.0);
        }
        break;

        default: // Case 0
        finalColor = result.color;
        break;
    }

    // Output final color
    FragColor = vec4(finalColor, 1.0);
    // Gamma correction (simple version)
    // FragColor = vec4(pow(color, vec3(1.0/2.2)), 1.0);
}


