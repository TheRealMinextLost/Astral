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

#include <iomanip>

#include "Basic/SDFObject.h"
#include "Basic/TransformManager.h"

bool pickRequested = false;
int pickMouseX = 0;
int pickMouseY = 0;

using namespace glm;
using namespace std;

// -- Shader file paths --
const string VERTEX_SHADER_PATH = "shaders/raymarch.vert";
const string FRAGMENT_SHADER_PATH = "shaders/raymarch.frag";

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
GLuint sdfDataUBO = 0;
GLuint renderFBO = 0;
GLuint colorTexture = 0;
GLuint pickingTexture = 0;
GLuint depthRenderbuffer = 0;

// Global App State
Camera camera(vec3(0.0f, -5.0f, 1.0f));
vector<SDFObject> sdfObjects;
int nextSdfId = 0;
int selectedObjectId = -1;
bool useGizmo = false;


// Helper function to check and print OpenGL errors
GLenum glCheckError_(const char *file, int line) {
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR) {
        std::string error;
        switch (errorCode) {
            case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
            case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
            case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
            default:                               error = "Unknown (" + std::to_string(errorCode) + ")"; break;
        }
        std::cerr << "OpenGL Error (" << error << ") " << file << ":" << line << std::endl;
    }
    return errorCode;
}
#define glCheckError() glCheckError_(__FILE__, __LINE__)


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
    glLinkProgram(program);  // Check right after a link

    GLint success; GLchar infoLog[1024];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    glGetProgramInfoLog(program, 1024, NULL, infoLog);
    if (!success) {
        cerr << "ERROR::PROGRAM::LINKING_FAILED\n" << infoLog << endl;
        glDeleteProgram(program); return 0;
    } else if (strlen(infoLog) > 0) { cout << "Program (ID: " << program << ") Link Log (Success with messages):\n" << infoLog << endl; }

    // Detach shaders immediately after a successful link
    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);

    return program;
}

void setupRenderFBO(int width, int height) {
    // Cleanup existing FBO resources
    if (renderFBO) {
        glDeleteFramebuffers(1, &renderFBO);
        glDeleteTextures(1, &colorTexture);
        glDeleteTextures(1, &pickingTexture);
        if (depthRenderbuffer) glDeleteRenderbuffers(1, &depthRenderbuffer);
        renderFBO = 0; colorTexture = 0; pickingTexture = 0; depthRenderbuffer = 0;
    }

    glCheckError();

    if (width <= 0 || height <= 0) {
        std::cerr << "Warning: Invalid dimensions for FBO setup (" << width << "x" << height << ")" << std::endl;
        return;
    }

    glGenFramebuffers(1, &renderFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, renderFBO);

    // 1. Color Texture
    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
    std::cout << "Color Texture created (ID: " << colorTexture << ")" << std::endl;
    glCheckError();

    // 2. Picking ID Texture
    glGenTextures(1, &pickingTexture);
    glBindTexture(GL_TEXTURE_2D, pickingTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, width, height, 0 , GL_RED_INTEGER, GL_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, pickingTexture, 0);
    std::cout << "Picking ID Texture created (ID: " << pickingTexture << ")" << std::endl;
    glCheckError();


    // 3. Renderbuffer Depth
    glGenRenderbuffers(1, &depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderbuffer);
    std::cout << "Depth Renderbuffer created (ID: " << depthRenderbuffer << ")" << std::endl;
    glCheckError();

    // 4. Specify Draw Buffers for MRT
    GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, drawBuffers);
    glCheckError();

    // 5. Check FBO completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "ERROR::FRAMEBUFFER:: Render FBO is not complete!" << std::endl;
    } else {
        std::cout << "Render FBO setup successful." << std::endl;
    }

    // Unbind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glCheckError();
}


void handlePickingRequest(int windowWidth, int windowHeight) {
    if (!pickRequested || windowWidth <= 0 || windowHeight <= 0) {
        if (!pickRequested) return;
        pickRequested = false;
        return;
    }
    pickRequested = false;

    if (!renderFBO || !pickingTexture) {
        std::cerr << "ERROR::PICKING:: Render FBO or Picking Texture not initialized!" << std::endl;
        selectedObjectId = -1;
        useGizmo = false;
        return;
    }
    // Bind Picking FBO & Set Viewport
    glBindFramebuffer(GL_READ_FRAMEBUFFER, renderFBO);

    glReadBuffer(GL_COLOR_ATTACHMENT1);

    int pickedIndex = -1;
    int readY = windowHeight - 1 - pickMouseY;

    if (pickMouseX >= 0 && pickMouseX < windowWidth && readY >= 0 && readY < windowHeight) {
        glReadPixels( pickMouseX, readY, 1, 1, GL_RED_INTEGER, GL_INT, &pickedIndex);
        glCheckError(); // Check RIGHT AFTER glReadPixels
    } else {
        std::cerr << "Warning::PICKING:: Coordinates out of bounds." << std::endl;
    }

    // Unbind the read framebuffer
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    // --- Process the result ---
    // ... (Processing logic remains the same) ...
    if (pickedIndex >= 0 && pickedIndex < sdfObjects.size()) {
        if (sdfObjects[pickedIndex].id != selectedObjectId) {
            selectedObjectId = sdfObjects[pickedIndex].id;
            useGizmo = true; // Or set based on the transform Manager state
            std::cout << "Picked Object Index: " << pickedIndex << " -> ID: " << selectedObjectId << std::endl;
        }
    } else {
        if (selectedObjectId != -1) {
            selectedObjectId = -1;
            useGizmo = false;
            std::cout << "Picked Background (Index: " << pickedIndex << ")" << std::endl;
        }
    }
}

// --- Framebuffer Resize ---
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (width > 0 && height > 0) {
        glViewport(0, 0, width, height);
        setupRenderFBO(width, height);
    }
}

// --- UBO ---
void updateSDFUBOData() {
    int numObjectsToSend = std::min((int)sdfObjects.size(), MAX_SDF_OBJECTS);
    if (numObjectsToSend <= 0) {
        return;
    }

    std::vector<SDFObjectGPUData> gpuData(numObjectsToSend);
    for(int i = 0; i < numObjectsToSend; ++i) {
        const auto& obj = sdfObjects[i];

        gpuData[i].inverseModelMatrix = obj.getInverseModelMatrix();
        gpuData[i].color = glm::vec4(obj.color, 1.0f);

        gpuData[i].paramsXYZ_type = glm::vec4(obj.parameters.x, obj.parameters.y, obj.parameters.z, static_cast<float>(obj.type));
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


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
    if (vertexShaderCode.empty() || fragmentShaderCode.empty()) { cerr << "Failed to load shaders!" << endl; return -1; }
    cout << "Shaders loaded." << endl;


    // --- Compile ALL Shaders ---
    cout << "Compiling shaders..." << endl;
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderCode);
    GLuint mainFragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderCode);
    if (!vertexShader || !mainFragmentShader ) { cerr << "Shader compilation failed." << endl; return -1; }
    cout << "Shaders compiled." << endl;


    // --- Link MAIN Program ---
    cout << "Linking main program..." << endl;
    shaderProgram = linkProgram(vertexShader, mainFragmentShader);
    if (shaderProgram == 0) { cerr << "Main program linking failed." << endl; /* Delete shaders? */ return -1; }
    cout << "Main program linked (ID: " << shaderProgram << ")." << endl;

    // --- Delete individual shaders (no longer needed) ---
    cout << "Deleting individual shaders..." << endl;
    glDeleteShader(vertexShader);
    glDeleteShader(mainFragmentShader);

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
    cout << "Finished getting non-UBO uniform locations for main shader." << endl;


    // --- Set up UBO (AFTER linking and getting other uniforms) ---
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
    glfwGetFramebufferSize(window, &initialWidth, &initialHeight);
    setupRenderFBO(initialWidth, initialHeight);


    // --- Initialize SDF Objects ---
    cout << "Initializing SDF Objects..." << endl;
    SDFObject sphere1(nextSdfId++);
    sphere1.type = SDFType::SPHERE;
    sphere1.position = vec3(-1.5f, 0.0f, 0.0f);
    sphere1.parameters = vec3(0.8f); // Set all radii to 0.8 for a uniform sphere
    sphere1.color = vec3(1.0f, 1.0f, 1.0f);
    sdfObjects.push_back(sphere1);

    SDFObject box1(nextSdfId++);
    box1.type = SDFType::BOX;
    box1.position = vec3(1.5f, 0.0f, 0.0f);
    box1.parameters = vec3(0.6f, 0.7f, 0.8f); // Set half-sizes directly
    box1.color = vec3(1.0f, 1.0f, 1.0f);
    sdfObjects.push_back(box1);


    // Initialize Transform Manager
    TransformManager transformManager(&camera,window);

    camera.SetTransformManager(&transformManager);


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

        // --- Begin ImGui Frame ---
        ui.newFrame();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        // --- Handle Inputs ---
        ImGuiIO& io = ImGui::GetIO();

        // -- Get UI Paramaters --
        const RenderParams& params = ui.getParams(); // Get params for main render pass uniforms

        // -- Handle Inputs using TransformManager --
        TransformManager::InputResult inputResult = transformManager.update(sdfObjects, selectedObjectId);

        // -- Handle picking request (reading from texture) --
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inputResult.consumedMouse && !io.WantCaptureMouse) {
            pickRequested = true;
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            pickMouseX = static_cast<int>(mouseX);
            pickMouseY = static_cast<int>(mouseY);
            std::cout << "Picking Requested in main.cpp at: " << pickMouseX << ", " << pickMouseY << std::endl; // Add debug print
        }

        // -- Camera keyboard Movement --
        if (!inputResult.consumedKeyboard && !io.WantCaptureKeyboard) {
            camera.ProcessKeyboardMovement(window, static_cast<float>(deltaTime));
        }


        // --- Setup for Main Render Pass Viewport & Aspect Ratio ---
        glBindFramebuffer(GL_FRAMEBUFFER, renderFBO);
        glViewport(0, 0, display_w, display_h);

        // Set Draw Buffers specifically for this render pass
        GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, drawBuffers);
        glCheckError();

        // Specify clear values for BOTH atttachments
        glClearColor(params.clearColor[0], params.clearColor[1], params.clearColor[2],  1.0f);
        GLint clearInt = -1;
        glClearBufferiv(GL_COLOR, 1, &clearInt);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


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
        glUseProgram(0);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        handlePickingRequest(display_w, display_h);
        glCheckError();

        glBindFramebuffer(GL_READ_FRAMEBUFFER, renderFBO);
        glCheckError();
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glCheckError();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glCheckError();
        if (display_w > 0 && display_h > 0) {
            glBlitFramebuffer(0, 0, display_w, display_h, 0, 0, display_w, display_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glCheckError(); // Check right after blit
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind completely

        // --- Update RAM Usage (Less Frequent) ---
        if (frameCounter++ % ramUpdateInterval == 0) { currentRSS = utility::getCurrentRSS(); }


        // --- Create ImGui UI Windows/Controls ---
        ui.createUI(camera.Fov, currentRSS,
                      sdfObjects, selectedObjectId, nextSdfId, useGizmo);

        glViewport(0, 0, display_w, display_h);
        ui.render();

        // --- Swap Buffers ---
        glfwMakeContextCurrent(window); // Ensure main context is current
        glfwSwapBuffers(window);
         // Final check for the frame

    }

    cout << "Cleaning up..." << endl;
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteProgram(shaderProgram);
    glDeleteBuffers(1, &sdfDataUBO);

    // Cleanup MRT FBO resources
    if (renderFBO) glDeleteFramebuffers(1, &renderFBO);
    if (colorTexture) glDeleteTextures(1, &colorTexture);
    if (pickingTexture) glDeleteTextures(1, &pickingTexture);
    if (depthRenderbuffer) glDeleteRenderbuffers(1, &depthRenderbuffer);

    glfwTerminate();
    cout << "Application terminated." << endl;
    return 0;
}
    // ======== End Render Loop ========