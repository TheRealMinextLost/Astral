#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <cstring> //

#include "UI/AstralUI.h"
#include "utilities/utility.h"
#include "Basic/Camera.h"
/*#include "ImGizmo/ImGuizmo.h"*/
#include "Basic/SDFObject.h" // Make sure SDFObjectGPUData is defined here

using namespace glm;
using namespace std;

// -- Shader file paths --
const string VERTEX_SHADER_PATH = "shaders/raymarch.vert";
const string FRAGMENT_SHADER_PATH = "shaders/raymarch.frag";
const string PICKING_SHADER_PATH = "shaders/picking.frag";

// Window dimensions
unsigned int SCR_WIDTH = 1920;
unsigned int SCR_HEIGHT = 1080;

// Constants
const int MAX_SDF_OBJECTS = 10; // Match GLSL
const int UBO_BINDING_POINT = 0;

// OpenGL Handles & VAO/VBO
unsigned int quadVAO = 0;
unsigned int quadVBO = 0;
GLuint shaderProgram = 0;
GLuint pickingShaderProgram = 0;
GLuint sdfDataUBO = 0;
GLuint pickingFBO = 0;
GLuint pickingTexture = 0;

// Global App State
Camera camera(vec3(0.0f, 1.0f, 5.0f));
vector<SDFObject> sdfObjects;
int nextSdfId = 0;
int selectedObjectId = -1;
bool useGizmo = false;

// Modal Transform State
enum class TransformMode { NONE, TRANSLATING, ROTATING, SCALING};
TransformMode currentTransformMode = TransformMode::NONE;

// Store Initial state when entering modal mode for cancellation
vec3 initialPosition;
vec3 initialRotation;
vec3 initialScale;

//Track mouse position
double modalStartX = 0.0, modalStartY = 0.0;
double lastModalMouseX = 0.0, lastModalMouseY = 0.0;
bool modalTransformActive = true; // flag to indicate if mouse should transform

bool isAxisConstrained = false;
enum class GizmoAxis { NONE, X, Y, Z } constrainedAxis = GizmoAxis::NONE;
enum class GizmoSpace {WORLD, LOCAL} currentGizmoSpace = GizmoSpace::WORLD;

// Picking State
std::map<std::string, GLint> pickingUniformLocs; // Cache for non-UBO picking uniforms
bool pickRequested = false;
int pickMouseX = 0;
int pickMouseY = 0;

// Helper function to check and print OpenGL errors
GLenum glCheckError_(const char *file, int line) {
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR) {
        std::string error;
        switch (errorCode) {
            case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
            case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
            case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
            case GL_STACK_OVERFLOW:                error = "STACK_OVERFLOW"; break;
            case GL_STACK_UNDERFLOW:               error = "STACK_UNDERFLOW"; break;
            case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
            default:                               error = "UNKNOWN_ERROR (" + std::to_string(errorCode) + ")"; break;
        }
        std::cerr << "OpenGL Error (" << error << ") | In File: " << file << " (Line: " << line << ")" << std::endl;
    }
    return errorCode; // Return the last code found (or GL_NO_ERROR)
}
#define glCheckError() glCheckError_(__FILE__, __LINE__)

// Helper to find the selected object (you might already have this)
SDFObject* findSelectedObject(int id) {
    if (id == -1) return nullptr;
    for (auto& obj : sdfObjects) {
        if (obj.id == id) {
            return &obj;
        }
    }
    return nullptr;
}

// Helper to apply modal translation based on total mouse delta
void ApplyModalTranslation(SDFObject* objPtr, double totalDeltaX, double totalDeltaY, int display_w, int display_h) {
    if (!objPtr) return;

    // Get camera basis
    vec3 camRight, camUp, camForward;
    camera.GetBasisVectors(camRight, camUp, camForward); // Assumes camera object is accessible

    // --- Calculate World Delta based on Screen Delta ---
    // Use depth relative to camera for sensitivity scaling
    float depth = distance(camera.Position, objPtr->position); // Or initialPosition? Using current for now.
    float tanHalfFovY = tan(radians(camera.Fov * 0.5f));
    float worldUnitsPerPixelY = (depth * tanHalfFovY * 2.0f) / (float)display_h;
    float worldUnitsPerPixelX = worldUnitsPerPixelY * ((float)display_w / (float)display_h) * 0.5f;

    vec3 worldDelta = (camRight * (float)totalDeltaX * worldUnitsPerPixelX) -
                      (camUp * (float)totalDeltaY * worldUnitsPerPixelY); // Invert Y

    // --- Apply Constraints ---
    if (isAxisConstrained) {
        // Project worldDelta onto the constrained axis
        vec3 axisVec;
        if (constrainedAxis == GizmoAxis::X) axisVec = vec3(1.0f, 0.0f, 0.0f);
        else if (constrainedAxis == GizmoAxis::Y) axisVec = vec3(0.0f, 1.0f, 0.0f);
        else axisVec = vec3(0.0f, 0.0f, 1.0f); // Z

        // If LOCAL mode, transform axis into world space first? Needs object orientation.
        // If LOCAL mode, transform axis into world space first? Needs object orientation.
        // For simplicity, let's assume WORLD constraints for now.
        // Local constraints require transforming the axis by the object's initial rotation.
        if (currentGizmoSpace == GizmoSpace::LOCAL) {
            quat initialQuat = quat(radians(initialRotation));
            mat4 initialRotationMatrix = mat4_cast(initialQuat);
            axisVec = vec3(initialRotationMatrix * vec4(axisVec, 0.0));
        }

        worldDelta = axisVec * dot(worldDelta, axisVec); // Project delta onto axis
    }

    // Apply the final delta to the initial position
    objPtr->position = initialPosition + worldDelta;
}
// --- FBO Setup ---
void setupPickingFBO(int width, int height) {
    if (pickingFBO) { glDeleteFramebuffers(1, &pickingFBO); glDeleteTextures(1, &pickingTexture); pickingFBO = 0; pickingTexture = 0; }
    if (width <= 0 || height <= 0) return;
    glGenFramebuffers(1, &pickingFBO); glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);
    glGenTextures(1, &pickingTexture); glBindTexture(GL_TEXTURE_2D, pickingTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, width, height, 0, GL_RED_INTEGER, GL_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pickingTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { cerr << "ERROR::FRAMEBUFFER:: Picking FBO is not complete" << endl; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
     // Check after setup
}

// --- Shader Compile ---
GLuint compileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint success; GLchar infoLog[1024];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    glGetShaderInfoLog(shader, 1024, NULL, infoLog);
    if (!success) {
        cerr << "ERROR::SHADER::COMPILATION_FAILED (" << (type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT") << ")\n" << infoLog << endl;
        glDeleteShader(shader); return 0;
    } else if (strlen(infoLog) > 0) { cout << "Shader Compile Log (" << (type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT") << " - Success with messages):\n" << infoLog << endl;}
    return shader;
}

// --- Link Program ---
GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);  // Check right after link

    GLint success; GLchar infoLog[1024];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    glGetProgramInfoLog(program, 1024, NULL, infoLog);
    if (!success) {
        cerr << "ERROR::PROGRAM::LINKING_FAILED\n" << infoLog << endl;
        glDeleteProgram(program); return 0;
    } else if (strlen(infoLog) > 0) { cout << "Program (ID: " << program << ") Link Log (Success with messages):\n" << infoLog << endl; }

    // Detach shaders immediately after successful link
    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);

    return program;
}

// --- Picking ---
void requestPicking(int mouseX, int mouseY) {
    if (currentTransformMode == TransformMode::NONE) {
        pickRequested = true;
        pickMouseX = mouseX;
        pickMouseY = mouseY;
    } else {
        std::cout << "Picking request ignored (modal mode active)" << std::endl;
    }
}

void handlePickingRequest(int windowWidth, int windowHeight) {
    if (!pickRequested || windowWidth <= 0 || windowHeight <= 0) { return; }
    pickRequested = false;

    if (!pickingFBO || !pickingShaderProgram) { cerr << "Picking system not initialized!" << std::endl; return; }

    // Bind Picking FBO & Set Viewport
    glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);
    glViewport(0, 0, windowWidth, windowHeight);

    // Clear Texture
    GLint clearValue = -1; glClearBufferiv(GL_COLOR, 0, &clearValue);

    // Use Picking Shader & Set Non-UBO Uniforms
    glUseProgram(pickingShaderProgram);
    if (pickingUniformLocs.count("u_resolution")) glUniform2f(pickingUniformLocs["u_resolution"], (float)windowWidth, (float)windowHeight);
    if (pickingUniformLocs.count("u_cameraPos")) glUniform3fv(pickingUniformLocs["u_cameraPos"], 1, value_ptr(camera.Position));
    if (pickingUniformLocs.count("u_cameraBasis")) glUniformMatrix3fv(pickingUniformLocs["u_cameraBasis"], 1, GL_FALSE, value_ptr(camera.GetBasisMatrix()));
    if (pickingUniformLocs.count("u_fov")) glUniform1f(pickingUniformLocs["u_fov"], camera.Fov);
    int numObjectsToSendPick = std::min((int)sdfObjects.size(), MAX_SDF_OBJECTS);
    if (pickingUniformLocs.count("u_sdfCount")) glUniform1i(pickingUniformLocs["u_sdfCount"], numObjectsToSendPick);
     // Check after setting picking uniforms

    // Draw Fullscreen Quad
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Read Pixel Data
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    int pickedIndex = -1;
    int readY = windowHeight - 1 - pickMouseY;
    if (pickMouseX >= 0 && pickMouseX < windowWidth && readY >= 0 && readY < windowHeight) {
        glReadPixels(pickMouseX, readY, 1, 1, GL_RED_INTEGER, GL_INT, &pickedIndex);
    } else { cerr << "Picking coordinates out of bounds!" << std::endl; }

    // Unbind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Process result
    if (pickedIndex >= 0 && pickedIndex < sdfObjects.size()) {
        selectedObjectId = sdfObjects[pickedIndex].id; useGizmo = true;
        std::cout << "GPU Picked Object ID: " << selectedObjectId << " (Index: " << pickedIndex << ")" << std::endl;
    } else {
        selectedObjectId = -1; useGizmo = false;
        if (pickMouseX >= 0 && pickMouseX < windowWidth && readY >= 0 && readY < windowHeight) {
             std::cout << "GPU Picked Background (Index: " << pickedIndex << ")" << std::endl;
        }
    }
}

// --- Framebuffer Resize ---
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    setupPickingFBO(width, height);
}

// --- UBO ---
void updateSDFUBOData() {
    int numObjectsToSend = std::min((int)sdfObjects.size(), MAX_SDF_OBJECTS);
    if (numObjectsToSend <= 0) {
        // Optionally clear the UBO if needed, or just return
        // You might still need to bind and upload zero data if the shader expects something
        // For now, just return if empty.
        return;
    }

    std::vector<SDFObjectGPUData> gpuData(numObjectsToSend);
    for(int i = 0; i < numObjectsToSend; ++i) {
        const auto& obj = sdfObjects[i];

        gpuData[i].inverseModelMatrix = obj.getInverseModelMatrix();
        gpuData[i].color = glm::vec4(obj.color, 1.0f);

        if (obj.type == SDFType::SPHERE) {
            gpuData[i].params1_3_type = glm::vec4(obj.radius, 0.0f, 0.0f, static_cast<float>(obj.type));
        } else if (obj.type == SDFType::BOX) {
            gpuData[i].params1_3_type = glm::vec4(obj.halfSize.x, obj.halfSize.y, obj.halfSize.z, static_cast<float>(obj.type));
        } else {
            gpuData[i].params1_3_type = glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(obj.type));
        }
    }

    glBindBuffer(GL_UNIFORM_BUFFER, sdfDataUBO);
    // Update the buffer - ensure size matches gpuData size correctly
    glBufferSubData(GL_UNIFORM_BUFFER, 0, gpuData.size() * sizeof(SDFObjectGPUData), gpuData.data());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void setupUBO() {
    cout << "Setting up UBO..." << endl;
    glGenBuffers(1, &sdfDataUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, sdfDataUBO);
    glBufferData(GL_UNIFORM_BUFFER, MAX_SDF_OBJECTS * sizeof(SDFObjectGPUData), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_POINT, sdfDataUBO);

    // Link Shader Blocks (Assume shaderProgram and pickingShaderProgram are valid)
    // Main Shader
    cout << "Checking & Binding UBO for Main Shader (Program ID: " << shaderProgram << ")" << endl;
    if (shaderProgram != 0) {
        GLuint blockIndexMain = glGetUniformBlockIndex(shaderProgram, "SDFBlock");
        cout << "  Main Shader - glGetUniformBlockIndex for 'SDFBlock' returned: " << blockIndexMain << endl;
        if (blockIndexMain != GL_INVALID_INDEX) {
            glUniformBlockBinding(shaderProgram, blockIndexMain, UBO_BINDING_POINT);
            cout << "  Main Shader - Bound 'SDFBlock' to binding point " << UBO_BINDING_POINT << "." << endl;
        } else { cerr << "!!!!!! Warning: Uniform block 'SDFBlock' NOT FOUND in main shader program. !!!!!!" << endl; }
    } else { cerr << "Error: Main shader program handle is invalid before UBO setup." << endl; }

    // Picking Shader
    cout << "Checking & Binding UBO for Picking Shader (Program ID: " << pickingShaderProgram << ")" << endl;
    if (pickingShaderProgram != 0) {
        GLuint blockIndexPicking = glGetUniformBlockIndex(pickingShaderProgram, "SDFBlock");
        cout << "  Picking Shader - glGetUniformBlockIndex for 'SDFBlock' returned: " << blockIndexPicking << endl;
        if (blockIndexPicking != GL_INVALID_INDEX) {
            glUniformBlockBinding(pickingShaderProgram, blockIndexPicking, UBO_BINDING_POINT);
            cout << "  Picking Shader - Bound 'SDFBlock' to binding point " << UBO_BINDING_POINT << "." << endl;
        } else { cerr << "!!!!!! Warning: Uniform block 'SDFBlock' NOT FOUND in picking shader program. !!!!!!" << endl; }
    } else { cerr << "Error: Picking shader program handle is invalid before UBO setup." << endl; }
    cout << "Finished UBO setup." << endl;
}

// --- Main ---
int main() {
    // --- Init GLFW, Window, GLAD + Checks ---
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6); // Request 4.6 (Adjust if needed)
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "SDF Raymarcher", NULL, NULL);
    if (window == NULL) { cerr << "Failed to create GLFW window" << endl; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSwapInterval(0);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { cout << "Failed to initialize GLAD" << endl; glfwTerminate(); return -1; }
    else {
        cout << "GLAD Initialized Successfully." << endl;
        cout << "OpenGL Version Reported by Driver: " << glGetString(GL_VERSION) << endl;
        // --- Pointer Checks ---
        bool pointers_ok = true;
        if (glUniformBlockBinding == NULL) { cerr << "CRITICAL ERROR: glUniformBlockBinding NULL!" << endl; pointers_ok = false; }
        if (glGetUniformBlockIndex == NULL) { cerr << "CRITICAL ERROR: glGetUniformBlockIndex NULL!" << endl; pointers_ok = false; }
        if (glBlendFunci == NULL) { cerr << "WARNING: glBlendFunci NULL!" << endl; /* pointers_ok = false; */ } // Less critical
        if (glShaderStorageBlockBinding == NULL) { cerr << "WARNING: glShaderStorageBlockBinding NULL!" << endl; /* pointers_ok = false; */ } // Less critical
        if (!pointers_ok) { cerr << "Critical GLAD pointers missing!" << endl; glfwTerminate(); return -1; }
        else { cout << "Required GLAD function pointers seem to be loaded." << endl; }
    }
     // Initial check

    // --- OpenGL Setup & Timer ---
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


    // --- Init UI & Callbacks ---
    AstralUI ui(window);
    glfwSetWindowUserPointer(window, &camera);
    glfwSetMouseButtonCallback(window, Camera::MouseButtonCallback);
    glfwSetCursorPosCallback(window, Camera::CursorPosCallback);
    glfwSetScrollCallback(window, Camera::ScrollCallback);


    // --- Load ALL Shader Sources ---
    cout << "Loading shaders..." << endl;
    string vertexShaderCode = utility::loadShaderSource(VERTEX_SHADER_PATH);
    string fragmentShaderCode = utility::loadShaderSource(FRAGMENT_SHADER_PATH);
    string pickingFragmentCode = utility::loadShaderSource(PICKING_SHADER_PATH);
    if (vertexShaderCode.empty() || fragmentShaderCode.empty() || pickingFragmentCode.empty()) { cerr << "Failed to load shaders!" << endl; return -1; }
    cout << "Shaders loaded." << endl;


    // --- Compile ALL Shaders ---
    cout << "Compiling shaders..." << endl;
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderCode);
    GLuint mainFragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderCode);
    GLuint pickingFragmentShader = compileShader(GL_FRAGMENT_SHADER, pickingFragmentCode);
    if (!vertexShader || !mainFragmentShader || !pickingFragmentShader) { cerr << "Shader compilation failed." << endl; return -1; }
    cout << "Shaders compiled." << endl;


    // --- Link MAIN Program ---
    cout << "Linking main program..." << endl;
    shaderProgram = linkProgram(vertexShader, mainFragmentShader);
    if (shaderProgram == 0) { cerr << "Main program linking failed." << endl; /* Delete shaders? */ return -1; }
    cout << "Main program linked (ID: " << shaderProgram << ")." << endl;


    // --- Link PICKING Program ---
    cout << "Linking picking program..." << endl;
    pickingShaderProgram = linkProgram(vertexShader, pickingFragmentShader);
    if (pickingShaderProgram == 0) { cerr << "Picking program linking failed." << endl; /* Delete shaders? */ return -1; }
    cout << "Picking program linked (ID: " << pickingShaderProgram << ")." << endl;


    // --- Delete individual shaders (no longer needed) ---
    cout << "Deleting individual shaders..." << endl;
    glDeleteShader(vertexShader);
    glDeleteShader(mainFragmentShader);
    glDeleteShader(pickingFragmentShader);


    // --- Get NON-UBO Uniform Locations ---
    cout << "Getting non-UBO uniform locations..." << endl;
    GLint u_resolutionLoc, u_cameraPosLoc, u_cameraBasisLoc, u_fovLoc, u_clearColorLoc,
          u_debugModeLoc, u_blendSmoothnessLoc, u_sdfCountLoc, u_selectedObjectIDLoc;
    // Main Program
    glUseProgram(shaderProgram);
    u_resolutionLoc = glGetUniformLocation(shaderProgram, "u_resolution");
    u_cameraPosLoc = glGetUniformLocation(shaderProgram, "u_cameraPos");
    u_cameraBasisLoc = glGetUniformLocation(shaderProgram, "u_cameraBasis");
    u_fovLoc = glGetUniformLocation(shaderProgram, "u_fov");
    u_clearColorLoc = glGetUniformLocation(shaderProgram, "u_clearColor");
    u_debugModeLoc = glGetUniformLocation(shaderProgram, "u_debugMode");
    u_blendSmoothnessLoc = glGetUniformLocation(shaderProgram, "u_blendSmoothness");
    u_sdfCountLoc = glGetUniformLocation(shaderProgram, "u_sdfCount");
    u_selectedObjectIDLoc = glGetUniformLocation(shaderProgram, "u_selectedObjectID");
    glUseProgram(0);


    // Picking Program
    glUseProgram(pickingShaderProgram);
    pickingUniformLocs.clear(); // Clear map before repopulating
    pickingUniformLocs["u_resolution"] = glGetUniformLocation(pickingShaderProgram, "u_resolution");
    pickingUniformLocs["u_cameraPos"] = glGetUniformLocation(pickingShaderProgram, "u_cameraPos");
    pickingUniformLocs["u_cameraBasis"] = glGetUniformLocation(pickingShaderProgram, "u_cameraBasis");
    pickingUniformLocs["u_fov"] = glGetUniformLocation(pickingShaderProgram, "u_fov");
    pickingUniformLocs["u_sdfCount"] = glGetUniformLocation(pickingShaderProgram, "u_sdfCount");
    glUseProgram(0);

    cout << "Finished getting non-UBO uniform locations." << endl;

    // --- Setup UBO (AFTER linking and getting other uniforms) ---
    setupUBO(); // Contains the block index query and binding
     // Check state AFTER UBO setup

    // --- Setup Quad & FBO ---
    cout << "Setting up Quad and FBO..." << endl;
    glGenVertexArrays(1, &quadVAO); glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO); glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    float quadVertices[] = { -1.f, 1.f, -1.f,-1.f, 1.f,-1.f, -1.f, 1.f, 1.f,-1.f, 1.f, 1.f };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    int initialWidth, initialHeight; glfwGetFramebufferSize(window, &initialWidth, &initialHeight);
    setupPickingFBO(initialWidth, initialHeight);

    cout << "Quad and FBO setup complete." << endl;

    // --- Initialize SDF Objects ---
    cout << "Initializing SDF Objects..." << endl;
    SDFObject sphere1(nextSdfId++); sphere1.type = SDFType::SPHERE; sphere1.position = vec3(-1.5f, 0.0f, 0.0f); sphere1.radius = 0.8f; sphere1.color = vec3(1.0f, 1.0f, 1.0f); sdfObjects.push_back(sphere1);
    SDFObject box1(nextSdfId++); box1.type = SDFType::BOX; box1.position = vec3(1.5f, 0.0f, 0.0f); box1.halfSize = vec3(0.6f, 0.7f, 0.8f); box1.color = vec3(1.0f, 1.0f, 1.0f); sdfObjects.push_back(box1);


    /*// --- Gizmo State ---
    ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;*/

    // --- Timing Variables ---
    cout << "Initialization complete. Entering render loop..." << endl;
    double lastTime = glfwGetTime();
    double deltaTime = 0.0;
    static size_t currentRSS = 0;
    static int frameCounter = 0;
    const int ramUpdateInterval = 60; // Update RAM roughly every second
    // gpuFrameTimeNano is already declared globally


    // ======== RENDER LOOP ========
    while (!glfwWindowShouldClose(window))
    {

        // --- Timing & Basic Events ---
        double currentTime = glfwGetTime();
        deltaTime = currentTime - lastTime;
        lastTime = currentTime;
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);

        // --- Update UBO ---
        updateSDFUBOData();

        // --- Begin ImGui/ImGuizmo Frame ---
        ui.newFrame();
        /*ImGuizmo::BeginFrame();*/

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);

        // --- Update RAM Usage (Less Frequent) ---
        if (frameCounter++ % ramUpdateInterval == 0) { currentRSS = utility::getCurrentRSS(); }

        // --- Handle Inputs ---
        ImGuiIO& io = ImGui::GetIO();
        bool consumedKeyboardInput = false; // Prevent camera move if keys handled
        bool consumedMouseInput = false; // Prevent picking if mouse confirms/cancels

        // Modal transform Input Handling
        if (currentTransformMode != TransformMode::NONE && selectedObjectId != -1) {
            consumedKeyboardInput = true; // We are handling input in modal mode

            // Get selected object pointer (needed often)
            SDFObject* objPtr = findSelectedObject(selectedObjectId);

            // Confirmation
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)&& !io.WantCaptureMouse || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                std::cout << "Modal Transform Confirmed" << std::endl;
                currentTransformMode = TransformMode::NONE; // exit modal mode
                modalTransformActive = false;
                isAxisConstrained = false;
                constrainedAxis = GizmoAxis::NONE;
                consumedMouseInput = true;
            }
            // Cancellation
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                std::cout << "Modal Transform Canceled" << std::endl;
                SDFObject* objPtr = findSelectedObject(selectedObjectId); // Need helper function
                if (objPtr) {
                    objPtr->position = initialPosition;
                    objPtr->rotation = initialRotation;
                    objPtr->scale = initialScale;
                }
                currentTransformMode = TransformMode::NONE; // exit modal mode
                modalTransformActive = false;
                isAxisConstrained = false;
                constrainedAxis = GizmoAxis::NONE;
                if (!ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    consumedMouseInput = true;
                }
            }

            // Axis constraint keys
            else if (!io.WantCaptureKeyboard) { // Check keyboard focus
                GizmoAxis newlyPressedAxis = GizmoAxis::NONE;
                if (ImGui::IsKeyPressed(ImGuiKey_X)) newlyPressedAxis = GizmoAxis::X;
                else if (ImGui::IsKeyPressed(ImGuiKey_Y)) newlyPressedAxis = GizmoAxis::Y;
                else if (ImGui::IsKeyPressed(ImGuiKey_Z)) newlyPressedAxis = GizmoAxis::Z;

                if (newlyPressedAxis != GizmoAxis::NONE) {
                    if (isAxisConstrained && constrainedAxis == newlyPressedAxis) {
                        isAxisConstrained = false; // toggle off
                        constrainedAxis = GizmoAxis::NONE;
                        std::cout << "Modal constraint toggled off" << std::endl;
                    } else {
                        isAxisConstrained = true; // toggle on / Switch
                        constrainedAxis = newlyPressedAxis;
                        std::cout << "Modal Constraint: " << (constrainedAxis == GizmoAxis::X ? "X" : (constrainedAxis == GizmoAxis::Y ? "Y" : "Z")) << std::endl;
                    }
                    // Immediately update position based on new constraint and current total mouse delta
                    double currentMouseX, currentMouseY;
                    glfwGetCursorPos(window, &currentMouseX, &currentMouseY);
                    double totalDeltaX = currentMouseX - modalStartX;
                    double totalDeltaY = currentMouseY - modalStartY;
                    if (objPtr) {
                        objPtr->position = initialPosition;
                        objPtr->rotation = initialRotation;
                        objPtr->scale = initialScale;
                        if (currentTransformMode == TransformMode::TRANSLATING) {
                            ApplyModalTranslation(objPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                        } // R/S later
                    }
                    lastModalMouseX = currentMouseX; // Update last mouse position *for next frame's delta*
                    lastModalMouseY = currentMouseY;
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_L)) {
                    currentGizmoSpace = (currentGizmoSpace == GizmoSpace::LOCAL) ? GizmoSpace::WORLD : GizmoSpace::LOCAL;
                    double currentMouseX, currentMouseY;
                    glfwGetCursorPos(window, &currentMouseX, &currentMouseY);
                    double totalDeltaX = currentMouseX - modalStartX;
                    double totalDeltaY = currentMouseY - modalStartY;
                    if (objPtr) {
                        objPtr->position = initialPosition;
                        objPtr->rotation = initialRotation;
                        objPtr->scale = initialScale;
                        if (currentTransformMode == TransformMode::TRANSLATING) {
                            ApplyModalTranslation(objPtr, totalDeltaX, totalDeltaY, display_w, display_h);
                        }// Add R/S Later
                    }
                    lastModalMouseX = currentMouseX;
                    lastModalMouseY = currentMouseY;
                } // End Axis/L key checks
            }


            // Mouse Movement Transformation (ONLY if active)
            if (currentTransformMode != TransformMode::NONE) {
                double currentMouseX, currentMouseY;
                glfwGetCursorPos(window, &currentMouseX, &currentMouseY);

                if (abs(currentMouseX - lastModalMouseX) > 1e-3 || abs(currentMouseY - lastModalMouseY) > 1e-3) {
                    // Calculate total mouse delta since modal start
                    double totalDeltaX = currentMouseX - modalStartX;
                    double totalDeltaY = currentMouseY - modalStartY;

                    if (objPtr) {
                        // Revert to initial state before applying delta to prevent accumulation errors
                        objPtr->position = initialPosition;
                        objPtr->rotation = initialRotation;
                        objPtr->scale = initialScale;

                        // Apply Transform base on Mode and Constraints
                        if (currentTransformMode == TransformMode::TRANSLATING) {
                            ApplyModalTranslation(objPtr, totalDeltaX, totalDeltaY, display_w, display_h); // New helper function
                        } else if (currentTransformMode == TransformMode::ROTATING) {
                            // ApplyModalRotation(objPtr, totalDeltaX, totalDeltaY); // Placeholder
                        } else if (currentTransformMode == TransformMode::SCALING) {
                            // ApplyModalScaling(objPtr, totalDeltaX, totalDeltaY); // Placeholder
                        }
                    }

                    lastModalMouseX = currentMouseX; // Update last mouse position *for next frame's delta*
                    lastModalMouseY = currentMouseY;
                }
            }
        } // End Modal Transform Input Handling

        // Normal Mode Input Handling (selection & entering Modal Mode)
        if (currentTransformMode == TransformMode::NONE && selectedObjectId != -1 && !io.WantCaptureKeyboard) {
            SDFObject* objPtr = findSelectedObject(selectedObjectId);

            // Enter Modal Translate (G)
            if (ImGui::IsKeyPressed(ImGuiKey_G) && objPtr) {
                std::cout << "Entering Modal Transform (Translate)" << std::endl;
                currentTransformMode = TransformMode::TRANSLATING;
                SDFObject* objPtr = findSelectedObject(selectedObjectId);
                if (objPtr) { // Store initial state
                    initialPosition = objPtr->position;
                    initialRotation = objPtr->rotation;
                    initialScale = objPtr->scale;
                }
                glfwGetCursorPos(window, &modalStartX, &modalStartY); // Record starting mouse pos
                lastModalMouseX = modalStartX;
                lastModalMouseY = modalStartY;
                modalTransformActive = true; // Start transforming immediately
                isAxisConstrained = false; // reset constraints
                constrainedAxis = GizmoAxis::NONE; // reset constraints
                consumedKeyboardInput = true; // We are handling input in modal mode
            }
            // --- Enter Modal Rotate (R) ---
            else if (ImGui::IsKeyPressed(ImGuiKey_R) && objPtr) {
                std::cout << "Entering Modal Rotate" << std::endl;
                currentTransformMode = TransformMode::ROTATING;
                if (objPtr) { /* Store initial State */ initialPosition = objPtr->position; initialRotation = objPtr->rotation; initialScale = objPtr->scale; }
                glfwGetCursorPos(window, &modalStartX, &modalStartY);
                lastModalMouseX = modalStartX; lastModalMouseY = modalStartY;
                modalTransformActive = true;
                isAxisConstrained = false; constrainedAxis = GizmoAxis::NONE;
                consumedKeyboardInput = true;
            }
            // --- Enter Modal Scale (S) ---
            else if (ImGui::IsKeyPressed(ImGuiKey_S)) {
                std::cout << "Entering Modal Scale" << std::endl;
                currentTransformMode = TransformMode::SCALING;
                SDFObject* objPtr = findSelectedObject(selectedObjectId);
                if(objPtr) { /* Store initial state */ initialPosition = objPtr->position; initialRotation = objPtr->rotation; initialScale = objPtr->scale; }
                glfwGetCursorPos(window, &modalStartX, &modalStartY);
                lastModalMouseX = modalStartX; lastModalMouseY = modalStartY;
                modalTransformActive = true;
                isAxisConstrained = false; constrainedAxis = GizmoAxis::NONE;
                consumedKeyboardInput = true;
            }
            // --- Deselect (D) --- (Can still deselect if not in modal mode)
            else if (ImGui::IsKeyPressed(ImGuiKey_D)) {
                selectedObjectId = -1;
                useGizmo = false;
                consumedKeyboardInput = true;
            }
            // --- Local/World Toggle (L) --- (Can still toggle if not in modal mode)
            else if (ImGui::IsKeyPressed(ImGuiKey_L)) {
                currentGizmoSpace = (currentGizmoSpace == GizmoSpace::LOCAL) ? GizmoSpace::WORLD : GizmoSpace::LOCAL;
                 std::cout << "Constraint Space Set To: " << (currentGizmoSpace == GizmoSpace::LOCAL ? "LOCAL" : "WORLD") << std::endl;
                consumedKeyboardInput = true;
            }
        } // End normal mode input handling

        // Camera Keyboard Movement (unmodified, runs if input not consumed by modal/gizmo keys)
        if (currentTransformMode == TransformMode::NONE && !consumedKeyboardInput && !io.WantCaptureMouse && !io.WantCaptureKeyboard) {
            camera.ProcessKeyboardMovement(window, static_cast<float>(deltaTime));
        }
        // --- Perform GPU Picking (if requested by previous frame's input) ---
        handlePickingRequest(display_w, display_h);

        // --- Setup for Main Render Pass Viewport & Aspect Ratio ---
        glViewport(0, 0, display_w, display_h);
        float aspectRatio = (display_w > 0 && display_h > 0) ? static_cast<float>(display_w) / static_cast<float>(display_h) : 1.0f;


        // --- Create ImGui UI Windows/Controls ---
        const RenderParams& params = ui.getParams(); // Get params for main render pass uniforms
        // Pass the last valid gpuFrameTimeNano reading
        ui.createUI(camera.Fov, currentRSS,
                      sdfObjects, selectedObjectId, nextSdfId, useGizmo);
         // Check after UI logic

        // --- Render Main SDF Scene ---
        glUseProgram(shaderProgram);
        // Set uniforms that are NOT part of the UBO
        glUniform2f(u_resolutionLoc, (float)display_w, (float)display_h);
        glUniform3fv(u_cameraPosLoc, 1, value_ptr(camera.Position));
        glUniformMatrix3fv(u_cameraBasisLoc, 1, GL_FALSE, value_ptr(camera.GetBasisMatrix()));
        glUniform1f(u_fovLoc, camera.Fov);
        glUniform3fv(u_clearColorLoc, 1, params.clearColor);
        glUniform1i(u_debugModeLoc, ui.getDebugMode());
        glUniform1f(u_blendSmoothnessLoc, params.blendSmoothness);
        int numObjectsToSend = std::min((int)sdfObjects.size(), MAX_SDF_OBJECTS);
        glUniform1i(u_sdfCountLoc, numObjectsToSend);
        int selectedObjectIndex = findObjectIndex(sdfObjects, selectedObjectId);
        glUniform1i(u_selectedObjectIDLoc, selectedObjectIndex); // Send selected INDEX
         // Check after setting main uniforms

        // Draw the fullscreen quad
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // --- Render ImGui Draw Data ---
        // IMPORTANT: This should be inside the query if you want to measure UI render time
        ui.render();

        // --- Swap Buffers ---
        glfwMakeContextCurrent(window); // Ensure main context is current
        glfwSwapBuffers(window);
         // Final check for the frame

    }
}
    // ======== End Render Loop ========