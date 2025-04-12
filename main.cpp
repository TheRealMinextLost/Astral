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
#include "ImGizmo/ImGuizmo.h"
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
GLuint gpuTimerQuery = 0;
GLuint pickingFBO = 0;
GLuint pickingTexture = 0;

// Global App State
Camera camera(vec3(0.0f, 1.0f, 5.0f));
vector<SDFObject> sdfObjects;
int nextSdfId = 0;
int selectedObjectId = -1;
bool useGizmo = false;

// Picking State
std::map<std::string, GLint> pickingUniformLocs; // Cache for non-UBO picking uniforms
bool pickRequested = false;
int pickMouseX = 0;
int pickMouseY = 0;

// Timing & Performance
GLuint64 gpuFrameTimeNano = 0; // Last available GPU frame time
bool gpuTimerQueryInFlight = false; // Should not be strictly needed with new logic

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
    glCheckError(); // Check after setup
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
    GLuint program = glCreateProgram(); glCheckError();
    glAttachShader(program, vertexShader); glCheckError();
    glAttachShader(program, fragmentShader); glCheckError();
    glLinkProgram(program); glCheckError(); // Check right after link

    GLint success; GLchar infoLog[1024];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    glGetProgramInfoLog(program, 1024, NULL, infoLog);
    if (!success) {
        cerr << "ERROR::PROGRAM::LINKING_FAILED\n" << infoLog << endl;
        glDeleteProgram(program); return 0;
    } else if (strlen(infoLog) > 0) { cout << "Program (ID: " << program << ") Link Log (Success with messages):\n" << infoLog << endl; }

    // Detach shaders immediately after successful link
    glDetachShader(program, vertexShader); glCheckError();
    glDetachShader(program, fragmentShader); glCheckError();

    return program;
}

// --- Picking ---
void requestPicking(int mouseX, int mouseY) {
    pickRequested = true;
    pickMouseX = mouseX;
    pickMouseY = mouseY;
}

void handlePickingRequest(int windowWidth, int windowHeight) {
    if (!pickRequested || windowWidth <= 0 || windowHeight <= 0) { return; }
    pickRequested = false;

    if (!pickingFBO || !pickingShaderProgram) { cerr << "Picking system not initialized!" << std::endl; return; }

    // Bind Picking FBO & Set Viewport
    glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO); glCheckError();
    glViewport(0, 0, windowWidth, windowHeight); glCheckError();

    // Clear Texture
    GLint clearValue = -1; glClearBufferiv(GL_COLOR, 0, &clearValue); glCheckError();

    // Use Picking Shader & Set Non-UBO Uniforms
    glUseProgram(pickingShaderProgram); glCheckError();
    if (pickingUniformLocs.count("u_resolution")) glUniform2f(pickingUniformLocs["u_resolution"], (float)windowWidth, (float)windowHeight);
    if (pickingUniformLocs.count("u_cameraPos")) glUniform3fv(pickingUniformLocs["u_cameraPos"], 1, value_ptr(camera.Position));
    if (pickingUniformLocs.count("u_cameraBasis")) glUniformMatrix3fv(pickingUniformLocs["u_cameraBasis"], 1, GL_FALSE, value_ptr(camera.GetBasisMatrix()));
    if (pickingUniformLocs.count("u_fov")) glUniform1f(pickingUniformLocs["u_fov"], camera.Fov);
    int numObjectsToSendPick = std::min((int)sdfObjects.size(), MAX_SDF_OBJECTS);
    if (pickingUniformLocs.count("u_sdfCount")) glUniform1i(pickingUniformLocs["u_sdfCount"], numObjectsToSendPick);
    glCheckError(); // Check after setting picking uniforms

    // Draw Fullscreen Quad
    glDisable(GL_DEPTH_TEST); glCheckError();
    glBindVertexArray(quadVAO); glCheckError();
    glDrawArrays(GL_TRIANGLES, 0, 6); glCheckError();
    glBindVertexArray(0); glCheckError();

    // Read Pixel Data
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glCheckError();
    glReadBuffer(GL_COLOR_ATTACHMENT0); glCheckError();
    int pickedIndex = -1;
    int readY = windowHeight - 1 - pickMouseY;
    if (pickMouseX >= 0 && pickMouseX < windowWidth && readY >= 0 && readY < windowHeight) {
        glReadPixels(pickMouseX, readY, 1, 1, GL_RED_INTEGER, GL_INT, &pickedIndex); glCheckError();
    } else { cerr << "Picking coordinates out of bounds!" << std::endl; }

    // Unbind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glCheckError();

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
    // No need to update if empty, but bufferSubData with size 0 is harmless
    // if (numObjectsToSend <= 0) return;

    // Map only the part of the buffer we need (potentially more efficient)
    glBindBuffer(GL_UNIFORM_BUFFER, sdfDataUBO); glCheckError();
    GLintptr offset = 0;
    GLsizeiptr length = numObjectsToSend * sizeof(SDFObjectGPUData);

    // If sending data, prepare it
    if (numObjectsToSend > 0) {
         std::vector<SDFObjectGPUData> gpuData(numObjectsToSend); // Create temporary buffer
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
         // Update the buffer data
         glBufferSubData(GL_UNIFORM_BUFFER, offset, length, gpuData.data()); glCheckError();
    }
    // Even if sending 0 objects, make sure bufferSubData isn't called with invalid size
    // else { // Optionally clear the buffer if needed, though often not necessary
    //      glClearBufferSubData(...)
    // }

    glBindBuffer(GL_UNIFORM_BUFFER, 0); glCheckError();
}

void setupUBO() {
    cout << "Setting up UBO..." << endl;
    glGenBuffers(1, &sdfDataUBO);                           glCheckError();
    glBindBuffer(GL_UNIFORM_BUFFER, sdfDataUBO);            glCheckError();
    glBufferData(GL_UNIFORM_BUFFER, MAX_SDF_OBJECTS * sizeof(SDFObjectGPUData), nullptr, GL_DYNAMIC_DRAW); glCheckError();
    glBindBuffer(GL_UNIFORM_BUFFER, 0);                     glCheckError();

    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_POINT, sdfDataUBO);  glCheckError();

    // Link Shader Blocks (Assume shaderProgram and pickingShaderProgram are valid)
    // Main Shader
    cout << "Checking & Binding UBO for Main Shader (Program ID: " << shaderProgram << ")" << endl;
    if (shaderProgram != 0) {
        GLuint blockIndexMain = glGetUniformBlockIndex(shaderProgram, "SDFBlock"); glCheckError();
        cout << "  Main Shader - glGetUniformBlockIndex for 'SDFBlock' returned: " << blockIndexMain << endl;
        if (blockIndexMain != GL_INVALID_INDEX) {
            glUniformBlockBinding(shaderProgram, blockIndexMain, UBO_BINDING_POINT); glCheckError();
            cout << "  Main Shader - Bound 'SDFBlock' to binding point " << UBO_BINDING_POINT << "." << endl;
        } else { cerr << "!!!!!! Warning: Uniform block 'SDFBlock' NOT FOUND in main shader program. !!!!!!" << endl; }
    } else { cerr << "Error: Main shader program handle is invalid before UBO setup." << endl; }

    // Picking Shader
    cout << "Checking & Binding UBO for Picking Shader (Program ID: " << pickingShaderProgram << ")" << endl;
    if (pickingShaderProgram != 0) {
        GLuint blockIndexPicking = glGetUniformBlockIndex(pickingShaderProgram, "SDFBlock"); glCheckError();
        cout << "  Picking Shader - glGetUniformBlockIndex for 'SDFBlock' returned: " << blockIndexPicking << endl;
        if (blockIndexPicking != GL_INVALID_INDEX) {
            glUniformBlockBinding(pickingShaderProgram, blockIndexPicking, UBO_BINDING_POINT); glCheckError();
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
    glCheckError(); // Initial check

    // --- OpenGL Setup & Timer ---
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glGenQueries(1, &gpuTimerQuery);
    glCheckError();

    // --- Init UI & Callbacks ---
    AstralUI ui(window);
    glfwSetWindowUserPointer(window, &camera);
    glfwSetMouseButtonCallback(window, Camera::MouseButtonCallback);
    glfwSetCursorPosCallback(window, Camera::CursorPosCallback);
    glfwSetScrollCallback(window, Camera::ScrollCallback);
    glCheckError();

    // --- Load ALL Shader Sources ---
    cout << "Loading shaders..." << endl;
    string vertexShaderCode = utility::loadShaderSource(VERTEX_SHADER_PATH);
    string fragmentShaderCode = utility::loadShaderSource(FRAGMENT_SHADER_PATH);
    string pickingFragmentCode = utility::loadShaderSource(PICKING_SHADER_PATH);
    if (vertexShaderCode.empty() || fragmentShaderCode.empty() || pickingFragmentCode.empty()) { cerr << "Failed to load shaders!" << endl; return -1; }
    cout << "Shaders loaded." << endl;
    glCheckError();

    // --- Compile ALL Shaders ---
    cout << "Compiling shaders..." << endl;
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderCode);
    GLuint mainFragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderCode);
    GLuint pickingFragmentShader = compileShader(GL_FRAGMENT_SHADER, pickingFragmentCode);
    if (!vertexShader || !mainFragmentShader || !pickingFragmentShader) { cerr << "Shader compilation failed." << endl; return -1; }
    cout << "Shaders compiled." << endl;
    glCheckError();

    // --- Link MAIN Program ---
    cout << "Linking main program..." << endl;
    shaderProgram = linkProgram(vertexShader, mainFragmentShader);
    if (shaderProgram == 0) { cerr << "Main program linking failed." << endl; /* Delete shaders? */ return -1; }
    cout << "Main program linked (ID: " << shaderProgram << ")." << endl;
    glCheckError();

    // --- Link PICKING Program ---
    cout << "Linking picking program..." << endl;
    pickingShaderProgram = linkProgram(vertexShader, pickingFragmentShader);
    if (pickingShaderProgram == 0) { cerr << "Picking program linking failed." << endl; /* Delete shaders? */ return -1; }
    cout << "Picking program linked (ID: " << pickingShaderProgram << ")." << endl;
    glCheckError();

    // --- Delete individual shaders (no longer needed) ---
    cout << "Deleting individual shaders..." << endl;
    glDeleteShader(vertexShader);
    glDeleteShader(mainFragmentShader);
    glDeleteShader(pickingFragmentShader);
    glCheckError();

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
    glCheckError();

    // Picking Program
    glUseProgram(pickingShaderProgram);
    pickingUniformLocs.clear(); // Clear map before repopulating
    pickingUniformLocs["u_resolution"] = glGetUniformLocation(pickingShaderProgram, "u_resolution");
    pickingUniformLocs["u_cameraPos"] = glGetUniformLocation(pickingShaderProgram, "u_cameraPos");
    pickingUniformLocs["u_cameraBasis"] = glGetUniformLocation(pickingShaderProgram, "u_cameraBasis");
    pickingUniformLocs["u_fov"] = glGetUniformLocation(pickingShaderProgram, "u_fov");
    pickingUniformLocs["u_sdfCount"] = glGetUniformLocation(pickingShaderProgram, "u_sdfCount");
    glUseProgram(0);
    glCheckError();
    cout << "Finished getting non-UBO uniform locations." << endl;

    // --- Setup UBO (AFTER linking and getting other uniforms) ---
    setupUBO(); // Contains the block index query and binding
    glCheckError(); // Check state AFTER UBO setup

    // --- Setup Quad & FBO ---
    cout << "Setting up Quad and FBO..." << endl;
    glGenVertexArrays(1, &quadVAO); glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO); glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    float quadVertices[] = { -1.f, 1.f, -1.f,-1.f, 1.f,-1.f, -1.f, 1.f, 1.f,-1.f, 1.f, 1.f };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    glCheckError();
    int initialWidth, initialHeight; glfwGetFramebufferSize(window, &initialWidth, &initialHeight);
    setupPickingFBO(initialWidth, initialHeight);
    glCheckError();
    cout << "Quad and FBO setup complete." << endl;

    // --- Initialize SDF Objects ---
    cout << "Initializing SDF Objects..." << endl;
    SDFObject sphere1(nextSdfId++); sphere1.type = SDFType::SPHERE; sphere1.position = vec3(-1.5f, 0.0f, 0.0f); sphere1.radius = 0.8f; sphere1.color = vec3(1.0f, 1.0f, 1.0f); sdfObjects.push_back(sphere1);
    SDFObject box1(nextSdfId++); box1.type = SDFType::BOX; box1.position = vec3(1.5f, 0.0f, 0.0f); box1.halfSize = vec3(0.6f, 0.7f, 0.8f); box1.color = vec3(1.0f, 1.0f, 1.0f); sdfObjects.push_back(box1);
    glCheckError();

    // --- Gizmo State ---
    ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;

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
        // --- 1. Check and Get PREVIOUS Frame's Timer Result ---
        GLint timerResultAvailable = 0;
        if (gpuTimerQuery != 0) { // Check if query object is valid
            // Non-blocking check if the result from the query ended last frame is ready
            glGetQueryObjectiv(gpuTimerQuery, GL_QUERY_RESULT_AVAILABLE, &timerResultAvailable);
            if (timerResultAvailable) {
                // Result is ready, get it.
                glGetQueryObjectui64v(gpuTimerQuery, GL_QUERY_RESULT, &gpuFrameTimeNano);
                glCheckError();
                // Now gpuFrameTimeNano holds the latest *available* measurement
            }
            // Else: Result not ready yet, gpuFrameTimeNano keeps its old value.
        }

        // --- 2. Begin CURRENT Frame's Timer Query ---
        if (gpuTimerQuery != 0) {
            glBeginQuery(GL_TIME_ELAPSED, gpuTimerQuery);
        }
        glCheckError();
        // --- Timing & Basic Events ---
        double currentTime = glfwGetTime();
        deltaTime = currentTime - lastTime;
        lastTime = currentTime;
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);

        // --- Update UBO ---
        updateSDFUBOData(); glCheckError();

        // --- Begin ImGui/ImGuizmo Frame ---
        ui.newFrame();
        ImGuizmo::BeginFrame();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);

        // --- Update RAM Usage (Less Frequent) ---
        if (frameCounter++ % ramUpdateInterval == 0) { currentRSS = utility::getCurrentRSS(); }

        // --- Handle Inputs ---
        ImGuiIO& io = ImGui::GetIO();
        // Gizmo Keyboard Shortcuts
        if (selectedObjectId != -1 && !io.WantCaptureKeyboard) {
            if (ImGui::IsKeyPressed(ImGuiKey_G)) { currentGizmoOperation = ImGuizmo::TRANSLATE; }
            if (ImGui::IsKeyPressed(ImGuiKey_R)) { currentGizmoOperation = ImGuizmo::ROTATE; }
            if (ImGui::IsKeyPressed(ImGuiKey_S)) { currentGizmoOperation = ImGuizmo::SCALE; }
            if (ImGui::IsKeyPressed(ImGuiKey_L)) { currentGizmoMode = (currentGizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL; }
            if (ImGui::IsKeyPressed(ImGuiKey_D)) { selectedObjectId = -1; useGizmo = false; }
        }
        // Camera Keyboard Movement
        if (!io.WantCaptureMouse && !io.WantCaptureKeyboard) {
            camera.ProcessKeyboardMovement(window, static_cast<float>(deltaTime));
        }

        // --- Perform GPU Picking (if requested by previous frame's input) ---
        handlePickingRequest(display_w, display_h); glCheckError();

        // --- Setup for Main Render Pass Viewport & Aspect Ratio ---
        glViewport(0, 0, display_w, display_h); glCheckError();
        float aspectRatio = (display_w > 0 && display_h > 0) ? static_cast<float>(display_w) / static_cast<float>(display_h) : 1.0f;

        // --- ImGuizmo Setup & Manipulation ---
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(0,0, (float)display_w, (float)display_h);
        mat4 viewMatrix = camera.GetViewMatrix();
        mat4 projectionMatrix = camera.GetProjectionMatrix(aspectRatio);
        if (useGizmo && selectedObjectId != -1) {
            SDFObject* selectedObjPtr = nullptr;
            for (auto& obj : sdfObjects) { if (obj.id == selectedObjectId) { selectedObjPtr = &obj; break; } }
            if (selectedObjPtr) {
                mat4 modelMatrix = selectedObjPtr->getModelMatrix();
                ImGuizmo::Manipulate(value_ptr(viewMatrix), value_ptr(projectionMatrix),
                                     currentGizmoOperation, currentGizmoMode, value_ptr(modelMatrix));
                if (ImGuizmo::IsUsing()) {
                    vec3 newPos, newRot, newScale;
                    ImGuizmo::DecomposeMatrixToComponents(value_ptr(modelMatrix), value_ptr(newPos), value_ptr(newRot), value_ptr(newScale));
                    if (currentGizmoOperation == ImGuizmo::SCALE) { newScale = max(newScale, vec3(0.001f)); }
                    selectedObjPtr->position = newPos;
                    selectedObjPtr->rotation = newRot;
                    selectedObjPtr->scale = newScale;
                    // std::cout << "Gizmo Updated ID " << selectedObjPtr->id << "..." << std::endl; // Optional debug
                }
            } else { selectedObjectId = -1; useGizmo = false; } // Deselect if object somehow not found
        }

        // --- Create ImGui UI Windows/Controls ---
        const RenderParams& params = ui.getParams(); // Get params for main render pass uniforms
        // Pass the last valid gpuFrameTimeNano reading
        ui.createUI(camera.Fov, (double)gpuFrameTimeNano / 1e6, currentRSS,
                      sdfObjects, selectedObjectId, nextSdfId, useGizmo);
        glCheckError(); // Check after UI logic

        // --- Render Main SDF Scene ---
        glUseProgram(shaderProgram); glCheckError();
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
        glCheckError(); // Check after setting main uniforms

        // Draw the fullscreen quad
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(quadVAO); glCheckError();
        glDrawArrays(GL_TRIANGLES, 0, 6); glCheckError();
        glBindVertexArray(0); glCheckError();

        // --- Render ImGui Draw Data ---
        // IMPORTANT: This should be inside the query if you want to measure UI render time
        ui.render(); glCheckError();

        // --- 3. End CURRENT Frame's Timer Query ---
        if (gpuTimerQuery != 0) {
            glEndQuery(GL_TIME_ELAPSED);
            // No glCheckError() immediately after glEndQuery
        }

        // --- Swap Buffers ---
        glfwMakeContextCurrent(window); // Ensure main context is current
        glfwSwapBuffers(window);
        glCheckError(); // Final check for the frame

    }
}
    // ======== End Render Loop ========