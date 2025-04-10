
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include "UI/AstralUI.h"
#include "utilities/utility.h"
#include "Basic/Camera.h"

// -- Shader file paths --
const std::string VERTEX_SHADER_PATH = "shaders/raymarch.vert";
const std::string FRAGMENT_SHADER_PATH = "shaders/raymarch.frag";

// Window dimensions
unsigned int SCR_WIDTH = 1920;
unsigned int SCR_HEIGHT = 1080;

// Global Camera instance
Camera camera(glm::vec3(0.0f, 0.0f, 5.0f)); //Initial position

int main()
{
    // Initialize GLFW
    glfwInit();

    // Add window hints
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "SDF Sphere Raymarcher", NULL, NULL);
    if (window == NULL) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    // ** Initialize GLAD **
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate(); // Exit if GLAD fails
        return -1;
    }

    // Set user pointer to the camera instance
    glfwSetWindowUserPointer(window, &camera);

    // Initialize UI
    AstralUI ui(window); // After GLAD

    // Set GLFW callbacks
    glfwSetMouseButtonCallback(window, Camera::MouseButtonCallback);
    glfwSetCursorPosCallback(window, Camera::CursorPosCallback);
    glfwSetScrollCallback(window, Camera::ScrollCallback);

    // Load Shader Sources
    std::string vertexShaderCode = utility::loadShaderSource(VERTEX_SHADER_PATH);
    std::string fragmentShaderCode = utility::loadShaderSource(FRAGMENT_SHADER_PATH);

    // Close app if couldn't load shader
    if (vertexShaderCode.empty() || fragmentShaderCode.empty()) {
        std::cerr << "Failed to load shaders. Exiting!" << std::endl;
        glfwTerminate();
        return -1;
    }

    const char* vertexShaderSourcePtr = vertexShaderCode.c_str();
    const char* fragmentShaderSourcePtr = fragmentShaderCode.c_str();

    // Build and compile our shader program
    // Vertex shader
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSourcePtr, NULL);
    glCompileShader(vertexShader);

    // Check for vertex shader compile errors... (add error checking)
    GLint success;
    GLchar infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }


    // Fragment shader
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSourcePtr, NULL);
    glCompileShader(fragmentShader);
    // Check for Fragment shader compile errors
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }


    // Link shaders
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    // Check for linking errors
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }


    // Delete the shaders as they're linked into our program and no longer necessary
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Setup Full-Screen Quad
    float quadVertices[] = { /* ... */ -1.0f,  1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  1.0f };
    unsigned int quadVAO, quadVBO;
    //
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,2 * sizeof(float), (void*)0);
    glBindVertexArray(0);


    // Get uniform locations
    GLint u_resolutionLoc   = glGetUniformLocation(shaderProgram, "u_resolution");
    GLint u_timeLoc         = glGetUniformLocation(shaderProgram, "u_time");
    // other
    GLint u_cameraPosLoc    = glGetUniformLocation(shaderProgram, "u_cameraPos");
    GLint u_cameraBasisLoc  = glGetUniformLocation(shaderProgram, "u_cameraBasis");
    GLint u_fovLoc          = glGetUniformLocation(shaderProgram, "u_fov");
    GLint u_spherePosLoc    = glGetUniformLocation(shaderProgram, "u_spherePos");
    GLint u_sphereRadiusLoc = glGetUniformLocation(shaderProgram, "u_sphereRadius");
    if (u_sphereRadiusLoc == -1) { std::cerr << "Uniform u_sphereRadius not found!" << std::endl; }
    GLint u_sphereColorLoc  = glGetUniformLocation(shaderProgram, "u_sphereColor");
    GLint u_clearColorLoc   = glGetUniformLocation(shaderProgram, "u_clearColor");
    GLint u_debugModeLoc    = glGetUniformLocation(shaderProgram, "u_debugMode");
    // Check if locations are valid
    if (u_debugModeLoc == -1) { std::cerr << "Warning: u_debugMode not found!" << std::endl; }


    // Render loop
    while (!glfwWindowShouldClose(window))
    {
        // Process input, poll events
        glfwPollEvents();

        // Check for Escape Key after pooling events
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // Start the ImGui frame
        ui.newFrame();
        ui.createUI(camera.Fov); // UI can modify camera's FOV

        // Get params, framebuffer size, viewport
        const RenderParams& params = ui.getParams();
        int currentDebugMode = ui.getDebugMode();

        // Get framebuffer size, set viewport
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);


        // -- Render SDF --
        glUseProgram(shaderProgram);
        // Set uniforms
        glUniform2f(u_resolutionLoc, (float)display_w, (float)display_h);
        glUniform1f(u_timeLoc, (float)glfwGetTime());

        // Get camera data directly from the camera object
        glUniform3fv(u_cameraPosLoc, 1, glm::value_ptr(camera.Position));
        glm::mat3 camBasis = camera.GetBasisMatrix(); // use camera's method
        glUniformMatrix3fv(u_cameraBasisLoc, 1 , GL_FALSE, glm::value_ptr(camBasis));
        glUniform1f(u_fovLoc, camera.Fov); // uses camera's fov

        // Get sphere data from UI Parameters
        glUniform3fv(u_spherePosLoc, 1, params.spherePosition);
        glUniform1f(u_sphereRadiusLoc, params.sphereRadius);
        glUniform3fv(u_sphereColorLoc, 1, params.sphereColor);
        glUniform3fv(u_clearColorLoc,1,params.clearColor);
        glUniform1i(u_debugModeLoc, currentDebugMode);


        glDisable(GL_DEPTH_TEST); // keep disabled

        // Draw quad (VAO bind, draw call)
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // Render ImGui
        ui.render();


        // Swap buffers and poll events
        glfwSwapBuffers(window);
    }

    // Cleanup
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}