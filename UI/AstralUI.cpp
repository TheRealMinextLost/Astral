//
// Handling UI
//

#include "AstralUI.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <numeric>
#include <cmath>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <imgui_internal.h>

using namespace ImGui;
using namespace glm;
using namespace std;

AstralUI::AstralUI(GLFWwindow* window) : m_window(window) {
    init();
    // Initialize RenderParams defaults
    m_params.clearColor[0] = 0.1f;
    m_params.clearColor[1]= 0.1f;
    m_params.clearColor[2] = 0.15f;
    m_params.blendSmoothness = 0.1f;
}

AstralUI::~AstralUI() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    DestroyContext();
}

void AstralUI::init() {
    // Setup ImGui context
    IMGUI_CHECKVERSION();
    CreateContext();
    ImGuiIO& io = GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport

    // Setup Dear ImGui style
    StyleColorsDark();
    //StyleColorsLight();

    // When viewports are enabled we need to configure style
    ImGuiStyle& style = GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void AstralUI::newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    NewFrame();

    // --- Setup Dockspace Host Window ---
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    ImGuiViewport* viewport = GetMainViewport();
    SetNextWindowPos(viewport->WorkPos);
    SetNextWindowSize(viewport->WorkSize);
    SetNextWindowViewport(viewport->ID);
    PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    window_flags |= ImGuiWindowFlags_NoBackground; // for passthrough


    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    Begin("DockSpace Host", nullptr, window_flags); // Begin host window
    PopStyleVar(); // Pop WindowPadding

    PopStyleVar(2); // Pop WindowRounding, WindowBorderSize

    // --- Dockspace Logic ---
    ImGuiIO& io = GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = GetID("MyDockSpace"); // Master ID

        // Submit the DockSpace node first (important!)
        // We use PassthruCentralNode so the *central* area (after splitting) interacts with the 3D view.
        DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        // Build the layout programmatically ONLY on the first frame
        if (!m_dockspace_layout_initialized) {
            m_dockspace_layout_initialized = true;

            // Start fresh
            DockBuilderRemoveNode(dockspace_id);
            // Add the node back with dockspace flag
            DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            // Set it to fill the host window size
            DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

            // Split the master dockspace: dock_id_right gets 20% width, dock_id_main gets the rest
            ImGuiID dock_id_main; // Will store the ID of the central node
            ImGuiID dock_id_right; // Will store the ID of the right node
            float right_panel_width_ratio = 0.20f; // Adjust as needed (20%)
            DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, right_panel_width_ratio, &dock_id_right, &dock_id_main);

            // Important: Configure the nodes (optional but good for fixed panels)
            ImGuiDockNode* right_node = DockBuilderGetNode(dock_id_right);
            if (right_node) {
                right_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar; // Hide tab bar if only one window
                // right_node->LocalFlags |= ImGuiDockNodeFlags_NoDockingSplitMe; // Prevent splitting this node further
                // right_node->LocalFlags |= ImGuiDockNodeFlags_NoDockingOverMe; // Prevent docking other windows *into* this node
            }
             ImGuiDockNode* main_node = DockBuilderGetNode(dock_id_main);
            if (main_node) {
                 // Ensure the main node keeps the passthrough capability
                 main_node->LocalFlags |= ImGuiDockNodeFlags_PassthruCentralNode;
                 main_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar; // Usually don't want a tab bar for the main 3D view area
            }


            // Dock the "Astral Settings" window into the right node
            DockBuilderDockWindow("Astral Settings", dock_id_right);
            // You could dock other default windows into dock_id_main if needed

            // Commit the layout
            DockBuilderFinish(dockspace_id);
            std::cout << "Dockspace layout built." << std::endl;
        }
    } else {
        Text("Docking is not enabled!");
    }

    End(); // End the host window ("DockSpace Host")
    // --- End Dockspace Setup ---
}


void AstralUI::createUI(float& fovRef, double gpuTimeMs, size_t ramBytes,
                      vector<SDFObject>& objects, int& currentSelectedId,
                      int& nextSdfId, bool& useGizmoRef)
{
    ImGuiWindowFlags settings_window_flags = ImGuiWindowFlags_NoCollapse;

    if (Begin("Astral Settings", &m_showSettingsWindow)) {
        // Call the panel rendering function ONLY if Begin() didn't return false (e.g., window is not collapsed)
        renderMainPanel(fovRef, gpuTimeMs, ramBytes, objects, currentSelectedId, nextSdfId, useGizmoRef);
    }
    // Always call End() to match Begin()
    End();
}


void AstralUI::renderMainPanel(float& fovRef, double gpuTimeMs, size_t ramBytes,
                             vector<SDFObject>& objects, int& currentSelectedId,
                             int& nextSdfId, bool& useGizmoRef)
{
    // Scene setting
    if (CollapsingHeader("Scene Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ColorEdit3("Clear Color", m_params.clearColor);
        SliderFloat("Flied of View", &fovRef, 10.0f, 120.0f);
        SliderFloat("Blend Smoothness", &m_params.blendSmoothness, 0.001f, 5.0f);
    }

    Separator();

    if (CollapsingHeader("Scene Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Button to add new objects
        if (Button("Add Sphere")) {
            SDFObject newObj(nextSdfId++);
            newObj.type = SDFType::SPHERE;
            newObj.position = glm::vec3(0.0f, 0.0f, -1.0f);
            objects.push_back(newObj);
        }
        SameLine();
        if (Button("Add Box")) {
            SDFObject newObj(nextSdfId++);
            newObj.type = SDFType::BOX;
            newObj.position = glm::vec3(0.0f, 0.0f, -1.0f);
            objects.push_back(newObj);
        }
    }

    Separator();
    // List existing objects
    // Use a temporary variable to avoid modifying selection during iteration issues
    int idToSelect = currentSelectedId;
    int idToDelete = -1;
    int indexToDelete = -1;

    // List existing objects;
    for (int i = 0; i < objects.size(); ++i) {
        // Use object's unique ID for ImGui identification
        PushID(objects[i].id);

        bool isSelected =(objects[i].id == currentSelectedId);
        // Selectable item - changes background if selected
        if (Selectable(objects[i].name.c_str(), isSelected)) {
            currentSelectedId = objects[i].id; // Select this object when clicked in the list
            useGizmoRef = true;
        }

        // Context menu for deleting
        if (BeginPopupContextItem("object_context_menu")) {
            Text("Object: %s", objects[i].name.c_str());
            if (MenuItem("Delete")) {
                idToDelete = objects[i].id; // Mark for deletion
                indexToDelete = i;
            }
            EndPopup();
        }

        PopID();
    }


    // Apply selection change after the loop
    currentSelectedId = idToSelect;

    // Apply deletion after the loop
    if (idToDelete != -1 && indexToDelete != -1) {
        if (idToDelete == currentSelectedId) { // Deselect if deleting selected
            currentSelectedId = -1;
            useGizmoRef = false;
        }
        // Ensure index is still valid (should be unless vector reallocated unexpectedly)
        if (indexToDelete < objects.size() && objects[indexToDelete].id == idToDelete) {
            objects.erase(objects.begin() + indexToDelete);
        } else {
            std::cerr << "Error: Index mismatch during object deletion." << std::endl;
            // Fallback: search for id again (slower)
            for (size_t i = 0; i < objects.size(); ++i) {
                if (objects[i].id == idToDelete) {
                    objects.erase(objects.begin() + i);
                    break;
                }
            }
        }
    }

    Separator();

    // Inspector Panel (Show properties of selected object)
    if (CollapsingHeader("Inspector", ImGuiTreeNodeFlags_DefaultOpen)) {
        SDFObject* selectedObjPtr = nullptr;
        if (currentSelectedId != -1) {
            for (auto& obj : objects) {
                if (obj.id == currentSelectedId) {
                    selectedObjPtr = &obj;
                    break;
                }
            }
        }
        if (selectedObjPtr) {
            // Display Name (Optional: Allow editing)
            char nameBuf[64];
            strncpy(nameBuf, selectedObjPtr->name.c_str(), sizeof(nameBuf) -1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            if (InputText("Name", nameBuf, sizeof(nameBuf))) {
                selectedObjPtr->name = nameBuf;
            }
            Text("ID: %d", selectedObjPtr->id);
            Separator();

            // Edit Transform
            Text("Transform");
            DragFloat3("Position", value_ptr(selectedObjPtr->position), 0.1f);
            DragFloat3("Rotation", value_ptr(selectedObjPtr->rotation), 1.0f);
            DragFloat3("Scale", value_ptr(selectedObjPtr->scale), 0.05f);
            Separator();

            // Edit Color
            Text("Appearance");
            ColorEdit3("Color", value_ptr(selectedObjPtr->color));
            Separator();

            // Edit Type-Specific Parameters
            Text("Parameters");
            if (selectedObjPtr->type == SDFType::SPHERE) {
                DragFloat("Radius", &selectedObjPtr->radius, 0.1f, 0.01f, 100.0f);
            } else if (selectedObjPtr->type == SDFType::BOX) {
                DragFloat3("Half Size", value_ptr(selectedObjPtr->halfSize), 0.01f, 0.01f, 100.0f);
            }

        } else {
            Text("No Object Selected");
        }
    } // End Inspector

    Separator();


    // section Raymarch debug
    if (CollapsingHeader("Raymarch Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        Text("Visualization Mode");
        RadioButton("Basic",&m_selectedDebugMode, 0); SameLine();
        RadioButton("Steps",&m_selectedDebugMode, 1); SameLine();
        RadioButton("Hit/Miss", &m_selectedDebugMode, 2);SameLine();
        RadioButton("Normals", &m_selectedDebugMode, 3);SameLine();
        RadioButton("Object ID", &m_selectedDebugMode, 4);
    }

    Separator(); // Separate section

    // Info Panel
    if (CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGuiIO& io = GetIO(); // Get IO context

        //Update frame time history
        m_frameTimes[m_frameTimeIndex] = io.Framerate > 0 ? (1000.0f / io.Framerate) : 0.0f;
        m_frameTimeIndex = (m_frameTimeIndex + 1) % IM_ARRAYSIZE(m_frameTimes);

        Text("FPS: %.1f", io.Framerate);
        Text("Frame Time: %.3f ms", io.Framerate > 0 ? (1000.0f / io.Framerate) : 0.0f);
        // --- GPU Timing ---
        Text("GPU Raymarch Time: %.3f ms", gpuTimeMs); // Display GPU time
        Separator();

        // Plot frame times
        PlotLines("Frame Times", m_frameTimes, IM_ARRAYSIZE(m_frameTimes), m_frameTimeIndex,
                            "FrameTime (ms)", 0.0f, 33.3f, ImVec2(0,80));

        // Display application information
        Separator();
        Text("Astral Engine");
        // Ensure glad is included if you don't have access to glGetString otherwise
        const GLubyte* glVersion = glGetString(GL_VERSION);
        const GLubyte* glRenderer = glGetString(GL_RENDERER);
        Text("OpenGL Version: %s", glVersion ? (const char*)glVersion : "Unknown");
        Text("GPU: %s", glRenderer ? (const char*)glRenderer : "Unknown");


        // --- Memory Usage ---
        Text("RAM Usage (RSS): %.2f MB", (double)ramBytes / (1024.0 * 1024.0)); // Display RAM in MB
        Separator();

    }
}



void AstralUI::render() {
    Render();
    ImGui_ImplOpenGL3_RenderDrawData(GetDrawData());

    // Update and Render additional Platform Windows
    ImGuiIO& io = GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        UpdatePlatformWindows();
        RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
}
