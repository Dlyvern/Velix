#include "Editor.hpp"
#include <VelixFlow/MeshComponent.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include <unordered_set>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstdlib>
#include "VelixFlow/LightComponent.hpp"
#include "VelixFlow/ParticleComponent.hpp"
#include "VelixFlow/AnimatorComponent.hpp"
#include "VelixFlow/Mouse.hpp"
#include "VelixFlow/Keyboard.hpp"
#include "VelixFlow/Raycasting.hpp"
#include "UIInputText.hpp"
#include "UILight.hpp"
#include "UIMaterial.hpp"
#include "UIMesh.hpp"
#include "UITransform.hpp"
#include "VelixFlow/RigidbodyComponent.hpp"
#include "VelixFlow/Utilities.hpp"
#include "VelixFlow/Filesystem.hpp"
#include "VelixFlow/LibrariesLoader.hpp"
#include <VelixFlow/ReflectedObject.hpp>
#include "InspectableGameObject.hpp"

#include <VelixFlow/ScriptsRegister.hpp>

#include <unistd.h>
#include <pwd.h>
#include <VelixFlow/AssetsLoader.hpp>
#include <VelixFlow/Logger.hpp>

#include <VelixFlow/DefaultRender.hpp>
#include "StencilRender.hpp"

#include "ProjectManager.hpp"

#include "Engine.hpp"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "../libraries/2ndParty/ImGuiColorTextEdit/TextEditor.h"

#include "EditorCommon.hpp"
#include <VelixFlow/ScriptSystem.hpp>

#define IMGUI_ENABLE_DOCKING

void Editor::destroy()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::init()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplGlfw_InitForOpenGL(Engine::s_application->getWindow()->getOpenGLWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

    style.Alpha = 1.0f;
    style.WindowRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.GrabMinSize = 20.0f;
    style.FrameRounding = 6.0f;
style.Colors[ImGuiCol_Text] = ImVec4(0.976f, 0.980f, 0.984f, 1.00f);              // Off White #F9FAFB
style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.611f, 0.639f, 0.686f, 1.00f);       // Light Gray #9CA3AF

// Backgrounds
style.Colors[ImGuiCol_WindowBg] = ImVec4(0.122f, 0.161f, 0.204f, 1.00f);           // Dark Gray #1F2933
style.Colors[ImGuiCol_ChildBg] = ImVec4(0.122f, 0.161f, 0.204f, 1.00f);
style.Colors[ImGuiCol_PopupBg] = ImVec4(0.122f, 0.161f, 0.204f, 1.00f);

style.Colors[ImGuiCol_Border] = ImVec4(0.294f, 0.333f, 0.388f, 1.00f);             // Medium Gray #4B5563
style.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

// Frames (inputs, sliders, etc)
style.Colors[ImGuiCol_FrameBg] = ImVec4(0.122f, 0.161f, 0.204f, 0.75f);            // Dark Gray + transparency
style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.091f, 0.753f, 0.922f, 0.30f);     // Velix Teal #17C0EB translucent
style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.063f, 0.741f, 0.894f, 0.60f);      // Velix Teal stronger

// Title bars
style.Colors[ImGuiCol_TitleBg] = ImVec4(0.040f, 0.239f, 0.325f, 0.75f);            // Velix Navy #0A3B54 translucent
style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.040f, 0.239f, 0.325f, 1.00f);      // Velix Navy full
style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.122f, 0.161f, 0.204f, 0.75f);  // Dark Gray translucent

// Scrollbar
style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.122f, 0.161f, 0.204f, 0.50f);
style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.041f, 0.678f, 0.890f, 0.44f);       // Velix Blue #0ABDE3 translucent
style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.041f, 0.678f, 0.890f, 0.74f);
style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.041f, 0.678f, 0.890f, 1.00f);

// Checkmarks and sliders
style.Colors[ImGuiCol_CheckMark] = ImVec4(0.041f, 0.678f, 0.890f, 0.68f);
style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.041f, 0.678f, 0.890f, 0.36f);
style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.041f, 0.678f, 0.890f, 0.76f);

// Buttons
style.Colors[ImGuiCol_Button] = ImVec4(0.041f, 0.678f, 0.890f, 0.46f);              // Velix Blue #0ABDE3 translucent
style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.090f, 0.753f, 0.922f, 0.43f);       // Velix Teal #17C0EB translucent
style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.040f, 0.239f, 0.325f, 0.62f);        // Velix Navy #0A3B54 translucent

// Headers (tree nodes, etc)
style.Colors[ImGuiCol_Header] = ImVec4(0.041f, 0.678f, 0.890f, 0.33f);
style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.090f, 0.753f, 0.922f, 0.42f);
style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.040f, 0.239f, 0.325f, 0.54f);

// Resize grips
style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.041f, 0.678f, 0.890f, 0.54f);
style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.090f, 0.753f, 0.922f, 0.74f);
style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.040f, 0.239f, 0.325f, 1.00f);

// Plot lines
style.Colors[ImGuiCol_PlotLines] = ImVec4(0.041f, 0.678f, 0.890f, 1.00f);
style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.041f, 0.678f, 0.890f, 1.00f);

// Histogram
style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.041f, 0.678f, 0.890f, 1.00f);
style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.041f, 0.678f, 0.890f, 1.00f);

// Text selected background
style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.041f, 0.678f, 0.890f, 0.22f);
	m_editorCamera = std::make_unique<Camera>(Engine::s_application->getCamera());

}

bool isMouseOverEmptySpace()
{
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    return ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
           ImGui::IsWindowHovered() &&
           !ImGui::IsAnyItemHovered();
}

void BeginDockSpace()
{
    ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpaceWindow", nullptr, windowFlags);

    ImGui::PopStyleVar(3);

    ImGuiID dockSpaceId = ImGui::GetID("MyDockspace");
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);

    static bool dock_built = false;

    if (!dock_built)
    {
        dock_built = true;

        ImGui::DockBuilderRemoveNode(dockSpaceId);
        ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->WorkSize);

        ImGuiID dockMainId = dockSpaceId;
        ImGuiID dockIdRight = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, nullptr, &dockMainId);
        ImGuiID dockIdLeft = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.20f, nullptr, &dockMainId);
        ImGuiID dockIdBottom = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.25f, nullptr, &dockMainId);
        ImGuiID dockIdCenter = dockMainId;

        ImGuiID dockIdLeftTop;
        ImGuiID dockIdLeftBottom = ImGui::DockBuilderSplitNode(dockIdLeft, ImGuiDir_Down, 0.3f, &dockIdLeftTop, &dockIdLeft);

        // Split bottom area into tab bar
        ImGuiID dockIdBottomLeft;
        ImGuiID dockIdBottomRight = ImGui::DockBuilderSplitNode(dockIdBottom, ImGuiDir_Right, 0.5f, &dockIdBottomLeft, &dockIdBottom);

        ImGui::DockBuilderDockWindow("Scene hierarchy", dockIdLeft);
        ImGui::DockBuilderDockWindow("Properties", dockIdRight);

        ImGui::DockBuilderDockWindow("Assets", dockIdBottomLeft);
        ImGui::DockBuilderDockWindow("Logger", dockIdBottomLeft);
        ImGui::DockBuilderDockWindow("Terminal", dockIdBottomRight);

        ImGui::DockBuilderDockWindow("Benchmark", dockIdLeftBottom);
        ImGui::DockBuilderDockWindow("Scene View", dockIdCenter);

        ImGui::DockBuilderFinish(dockSpaceId);
    }

    ImGui::End();
}

void Editor::updateInput()
{
    if (input::Keyboard.isKeyReleased(input::KeyCode::KEY_DELETE) && m_selectedGameObject)
    {
        if (Engine::s_application->getScene()->deleteGameObject(m_selectedGameObject))
            setSelectedGameObject(nullptr);
    }

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::V) && m_savedGameObject)
    {
        Engine::s_application->getScene()->addGameObject(m_savedGameObject);
        setSelectedGameObject(m_savedGameObject.get());
    }

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::C) && m_selectedGameObject)
    {
        std::unordered_set<std::string> existingNames;

        for (const auto &obj : Engine::s_application->getScene()->getGameObjects())
            existingNames.insert(obj->getName());

        std::string uniqueName = utilities::generateUniqueName(m_selectedGameObject->getName(), existingNames);

        auto newGameObject = std::make_shared<GameObject>(uniqueName);

        newGameObject->setPosition(m_selectedGameObject->getPosition());
        newGameObject->setRotation(m_selectedGameObject->getRotation());
        newGameObject->setScale(m_selectedGameObject->getScale());
        newGameObject->addComponent<RigidbodyComponent>(newGameObject);

        if (auto selectedMeshComponent = m_selectedGameObject->getComponent<MeshComponent>())
        {
            auto addedMeshComponent = newGameObject->addComponent<MeshComponent>(selectedMeshComponent->getModel());

            if (addedMeshComponent->getModel()->hasSkeleton())
                physics::PhysicsController::instance().resizeCollider({1.0f, 2.0f, 1.0f}, newGameObject);

            for(int i = 0; i < selectedMeshComponent->getModel()->getNumMeshes(); ++i)
            {
                auto mat = selectedMeshComponent->getMaterialOverride(i);

                if(mat)
                    newGameObject->getComponent<MeshComponent>()->setMaterialOverride(i, mat);
            }
        }

        m_savedGameObject = newGameObject;
    }

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::Z))
        m_actionsManager.undo();

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::Y))
        m_actionsManager.redo();

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::S))
    {
        const auto project = ProjectManager::instance().getCurrentProject();

        // TODO CHANGE IT LATER 'project->getEntryScene()'
        Engine::s_application->getScene()->saveSceneToFile(project->entryScene);
    }

    if (input::Keyboard.isKeyReleased(input::KeyCode::W))
        m_transformMode = TransformMode::Translate;

    if (input::Keyboard.isKeyReleased(input::KeyCode::E))
        m_transformMode = TransformMode::Scale;

    if (input::Keyboard.isKeyReleased(input::KeyCode::R))
        m_transformMode = TransformMode::Rotate;


    // if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        // m_selectedInspectable = nullptr;
}

void Editor::update(float deltaTime)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();

    if (m_state == State::Editor)
    {
        m_editorCamera->update(deltaTime); 
        ImGuizmo::BeginFrame();
        showEditor();
    }
    else if (m_state == State::Start)
        showStart();
    else if(m_state == State::Play)
        showEditor();

    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (const ImGuiIO &io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow *backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
}

Editor::State Editor::getState() const
{
    return m_state;
}

std::string getHome()
{
    return (std::getenv("HOME") + std::string("/Documents/ElixirProjects"));
}

void Editor::showStart()
{
    const ImGuiIO &io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoDecoration;

    ImGui::Begin("Start", nullptr, flags);

    static bool showRecentProjects = false;

    if (ImGui::Button("Recent projects"))
        showRecentProjects = true;

    if (showRecentProjects)
    {
        ImGui::OpenPopup("Recent projects");
        showRecentProjects = false;
    }

    if (ImGui::BeginPopupModal("Recent projects", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        std::filesystem::path directory{getHome()};

        for (const auto &entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.is_directory())
            {
                if (ImGui::Button(entry.path().filename().string().c_str()))
                {
                    auto project = new Project();

                    if (!ProjectManager::instance().loadConfigInProject(entry.path().string() + "/Project.elixirproject", project))
                    {
                        ELIX_LOG_ERROR("Failed to load project");
                        delete project;
                        continue;
                    }

                    if (!ProjectManager::instance().loadProject(project))
                    {
                        ELIX_LOG_ERROR("Failed to load project");
                        delete project;
                        continue;
                    }

                    ProjectManager::instance().setCurrentProject(project);

                    m_state = State::Editor;

                    ImGui::CloseCurrentPopup();
                    showRecentProjects = false;

                    break;
                }
            }
        }

        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    static bool showCreatePopup = false;

    if (ImGui::Button("Create a new project"))
        showCreatePopup = true;

    if (showCreatePopup)
    {
        ImGui::OpenPopup("Create New Project");
        showCreatePopup = false;
    }

    if (ImGui::BeginPopupModal("Create New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static char projectName[128] = "MyGame";
        static char location[512] = "";

#if defined(_WIN32)
        if (strlen(location) == 0)
            strcpy(location, (std::getenv("USERPROFILE") + std::string("\\Documents\\ElixirProjects")).c_str());
#else
        if (strlen(location) == 0)
            strcpy(location, (std::getenv("HOME") + std::string("/Documents/ElixirProjects")).c_str());
#endif

        ImGui::InputText("Project Name", projectName, IM_ARRAYSIZE(projectName));
        ImGui::InputText("Location", location, IM_ARRAYSIZE(location));

        if (ImGui::Button("Create"))
        {
            const std::string projectDir = std::string(location) + "/" + projectName;

            const auto project = ProjectManager::instance().createProject(projectName, projectDir);

            if (!project)
            {
                ELIX_LOG_ERROR("Failed to create project");
                ImGui::End();
                return;
            }

            if (!ProjectManager::instance().loadProject(project))
            {
                ELIX_LOG_ERROR("Failed to load project");
                ImGui::End();
                return;
            }

            ProjectManager::instance().setCurrentProject(project);

            m_state = State::Editor;

            ImGui::CloseCurrentPopup();
            showCreatePopup = false;
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
            showCreatePopup = false;
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}

void Editor::showEditor()
{
    updateInput();
    showMenuBar();
    BeginDockSpace();
    showViewPort();
    showDebugInfo();
    drawLogWindow();
    drawTerminal();
}

void Editor::showGuizmo(GameObject *gameObject, float x, float y, float width, float height)
{
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(x, y, width, height);
    glm::mat4 modelMatrix = gameObject->getTransformMatrix();

    glm::mat4 viewMatrix = m_editorCamera->getCamera()->getViewMatrix();
    glm::mat4 projMatrix = m_editorCamera->getCamera()->getProjectionMatrix();

    ImGuizmo::OPERATION operation{ImGuizmo::TRANSLATE};

    switch (m_transformMode)
    {
    case TransformMode::Translate:
        operation = ImGuizmo::TRANSLATE;
        break;
    case TransformMode::Rotate:
        operation = ImGuizmo::ROTATE;
        break;
    case TransformMode::Scale:
        operation = ImGuizmo::SCALE;
        break;
    }

    ImGuizmo::Manipulate(
        glm::value_ptr(viewMatrix),
        glm::value_ptr(projMatrix),
        operation,
        ImGuizmo::WORLD,
        glm::value_ptr(modelMatrix),
        nullptr,
        nullptr,
        nullptr);

    if (ImGuizmo::IsUsing())
    {
        glm::vec3 translation, rotation, scale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(modelMatrix),
            glm::value_ptr(translation),
            glm::value_ptr(rotation),
            glm::value_ptr(scale));

        gameObject->setPosition(translation);
        gameObject->setRotation(rotation);
        gameObject->setScale(scale);
    }
}

std::string replaceAll(std::string str, const std::string& from, const std::string& to)
{
    size_t startPos = 0;
    while ((startPos = str.find(from, startPos)) != std::string::npos) {
        str.replace(startPos, from.length(), to);
        startPos += to.length();
    }
    return str;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("Failed to open file: " + path);

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) throw std::runtime_error("Failed to write file: " + path);

    file << content;
}

void Editor::showMenuBar()
{
    static char classNameBuffer[64] = "";
    static bool openCreateClassPopup = false;

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            ImGui::MenuItem("New Project");
            ImGui::MenuItem("Open Project");
            ImGui::MenuItem("Save Scene");
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Export"))
        {
            if(ImGui::MenuItem("Export game"))
            {
                ProjectManager::instance().exportProjectGame();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Play"))
        {
            if (ImGui::MenuItem("Play game"))
            {
                for(const auto& object : Engine::s_application->getScene()->getGameObjects())
                    if(object->hasComponent<elix::CameraComponent>())
                    {
                        m_state = State::Play; 
                        Engine::s_application->getCamera()->setPosition(object->getPosition());
                    }
            }

            if (ImGui::MenuItem("Stop game"))
            {
            }

            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Create"))
        {
            if(ImGui::MenuItem("Create a new class"))
            {
                openCreateClassPopup = true;
            }

            ImGui::EndMenu();
        }

        if (openCreateClassPopup)
        {
            ImGui::OpenPopup("CreateNewClass");
            openCreateClassPopup = false;
        }

        if(ImGui::BeginPopupModal("CreateNewClass", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Class Name", classNameBuffer, sizeof(classNameBuffer));

            if (ImGui::Button("Create Script"))
            {
                std::string hppTemplate = readFile(elix::filesystem::getExecutablePath().string() + "/template/ScriptTemplate.hpp.txt");
                std::string cppTemplate = readFile(elix::filesystem::getExecutablePath().string() + "/template/ScriptTemplate.cpp.txt");

                std::string hppContent = replaceAll(hppTemplate, "{{ClassName}}", classNameBuffer);
                std::string cppContent = replaceAll(cppTemplate, "{{ClassName}}", classNameBuffer);
                
                writeFile(ProjectManager::instance().getCurrentProject()->sourceDir + "/" + classNameBuffer + ".hpp", hppContent);
                writeFile(ProjectManager::instance().getCurrentProject()->sourceDir + "/" + classNameBuffer + ".cpp", cppContent);

                classNameBuffer[0] = '\0';

                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                classNameBuffer[0] = '\0';
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::EndMainMenuBar();
    }
}

void Editor::drawMainScene()
{
    const float windowWidth = ImGui::GetContentRegionAvail().x;
    const float windowHeight = ImGui::GetContentRegionAvail().y;
    const ImVec2 cursorPosition = ImGui::GetCursorScreenPos();
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();

    ImGuiIO &io = ImGui::GetIO();
    float dpiScale = io.DisplayFramebufferScale.x;
    int fbWidth = (int)(contentSize.x * dpiScale);
    int fbHeight = (int)(contentSize.y * dpiScale);

    Engine::s_application->getRenderer()->getFbo()->resize(fbWidth, fbHeight);

    auto fboTexture = Engine::s_application->getRenderer()->getFbo()->getTexture(0);
    ImGui::Image((ImTextureID)(intptr_t)fboTexture, contentSize, ImVec2(0, 1), ImVec2(1, 0));

    ImGui::SetCursorScreenPos(cursorPosition);
    ImGui::InvisibleButton("GizmoInputCatcher", contentSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    ImGui::SetItemAllowOverlap();

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            const auto *const info = static_cast<editorCommon::DraggingInfo*>(payload->Data);

            if (!info)
            {
                ImGui::End();
                return;
            }

            std::filesystem::path path(info->name);

            if (path.extension() == ".hdr")
            {
                auto skybox = std::make_shared<elix::Skybox>();
                skybox->init({});
                skybox->loadFromHDR(path.string());

                Engine::s_application->getScene()->setSkybox(skybox);
            }

            // TODO Replace this shit somehow
            else if (path.extension() == ".fbx" || path.extension() == ".obj")
            {
                auto newGameObject = std::make_shared<GameObject>("test");
                newGameObject->setPosition({0.0f, 0.0f, 0.0f});
                newGameObject->setRotation({0.0f, 0.0f, 0.0f});
                newGameObject->setScale({1.0f, 1.0f, 1.0f});
                newGameObject->addComponent<RigidbodyComponent>(newGameObject);

                if (auto cache = ProjectManager::instance().getAssetsCache())
                {
                    if (auto model = cache->getAsset<elix::AssetModel>(path.string()))
                    {
                        auto meshComponent = newGameObject->addComponent<MeshComponent>(model->getModel());

                        if (meshComponent->getModel()->hasSkeleton())
                            physics::PhysicsController::instance().resizeCollider({1.0f, 2.0f, 1.0f}, newGameObject);
                    }
                    else
                        ELIX_LOG_WARN("Failed to load asset model");
                }
                else
                    ELIX_LOG_WARN("Failed to load cache");

                Engine::s_application->getScene()->addGameObject(newGameObject);
            }

            std::cout << "Dropped asset into scene: " << info->name << std::endl;
        }
        ImGui::EndDragDropTarget();
    }

    if (m_selectedGameObject && m_editorCamera)
        showGuizmo(m_selectedGameObject, cursorPosition.x, cursorPosition.y, windowWidth, windowHeight);

    if (input::Mouse.isLeftButtonPressed())
    {
        if (ImGui::IsItemHovered() && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && m_editorCamera)
        {
            ImVec2 mousePos = ImGui::GetMousePos();

            float localX = mousePos.x - cursorPosition.x;
            float localY = mousePos.y - cursorPosition.y;

            if (localX >= 0 && localY >= 0 && localX < windowWidth && localY < windowHeight)
            {
                float x = (2.0f * localX) / windowWidth - 1.0f;
                float y = 1.0f - (2.0f * localY) / windowHeight;

                glm::vec2 mouseNDC(x, y);

                float aspectRatio = windowWidth / windowHeight;

                glm::mat4 projection = m_editorCamera->getCamera()->getProjectionMatrix();
                glm::mat4 view = m_editorCamera->getCamera()->getViewMatrix();

                glm::vec4 rayClip(mouseNDC.x, mouseNDC.y, -1.0f, 1.0f);
                glm::vec4 rayEye = glm::inverse(projection) * rayClip;
                rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

                glm::vec3 rayWorld = glm::normalize(glm::vec3(glm::inverse(view) * rayEye));
                glm::vec3 origin = glm::vec3(glm::inverse(view)[3]);

                physics::raycasting::Ray ray{};
                ray.maxDistance = 1000.0f;
                ray.direction = rayWorld;
                ray.origin = origin;

                physics::raycasting::RaycastingResult result;

                if (physics::raycasting::shoot(ray, result))
                {
                    auto *actor = result.hit.block.actor;
                    auto *gameObject = static_cast<GameObject *>(actor->userData);

                    if (gameObject)
                        setSelectedGameObject(gameObject);
                }
            }
        }
    }
}

void Editor::showTextEditor()
{

//     TextEditor::LanguageDefinition myLang;
// myLang.mName = "MyLang";

// // Define keywords
// myLang.mKeywords = {"if", "else", "for", "while", "return", "struct", "uniform", "void", ... };

// // Define token regexes
// myLang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>("\"(\\\\.|[^\"])*\"", TextEditor::PaletteIndex::String)); // string literals
// myLang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>("#[a-zA-Z_]+", TextEditor::PaletteIndex::Preprocessor)); // preprocessor
// // add other token regexes...

// // Then set it:
// editor.SetLanguageDefinition(myLang);

    static TextEditor editor;

    // editor.SetAutoCompleteCallback([](const std::string& currentWord) -> std::vector<std::string>
    // {
    //     std::vector<std::string> suggestions;

    //     if (currentWord.starts_with("#include"))
    //     {
    //         suggestions = {
    //             "<iostream>", "<vector>", "<string>", "\"MyComponent.hpp\"", "\"Texture.hpp\""
    //         };
    //     }

    //     return suggestions;
    // });

//     if (m_autoCompleteCallback)
// {
//     std::string currentWord = GetWordUnderCursor(); // ← implement this helper
//     auto suggestions = m_autoCompleteCallback(currentWord);

//     if (!suggestions.empty())
//     {
//         // Show ImGui dropdown here
//         ImGui::Begin("Autocomplete");
//         for (auto& suggestion : suggestions)
//         {
//             if (ImGui::Selectable(suggestion.c_str()))
//             {
//                 InsertText(suggestion.substr(currentWord.length())); // complete word
//             }
//         }
//         ImGui::End();
//     }
// }

    static std::string currentFilePath;

    auto assetPath = m_uiAssetsWindow.getFileEditorPath();

    if (assetPath != currentFilePath && !assetPath.empty())
    {
        currentFilePath = assetPath;

        auto lang = TextEditor::LanguageDefinition::CPlusPlus();
        editor.SetLanguageDefinition(lang);

        std::string text = readFile(currentFilePath);
        editor.SetText(text);
    }

    if(!editor.GetText().empty())
        editor.Render("TextEditor");

    if (ImGui::Button("Save") && !assetPath.empty())
    {
        writeFile(currentFilePath, editor.GetText());
    }
}


void Editor::showViewPort()
{
    ImGui::Begin("Scene View", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse /*| ImGuiWindowFlags_MenuBar*/);

    if (ImGui::BeginTabBar("SceneTabs", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("Scene"))
        {
            drawMainScene();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Script Editor"))
        {
            showTextEditor();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

Editor::~Editor() = default;

void Editor::showAllObjectsInTheScene()
{
    if (!Engine::s_application->getScene())
        return;

    const auto &objects = Engine::s_application->getScene()->getGameObjects();

    ImGui::Begin("Scene hierarchy");

    for (const auto &object : objects)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

        if (object.get() == m_selectedGameObject)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool isNodeOpened = ImGui::TreeNodeEx((void *)(intptr_t)object.get(), flags, "%s", object->getName().c_str());

        if (ImGui::IsItemClicked())
            setSelectedGameObject(object.get());

        if (isNodeOpened)
        {
            // for (auto& child : object->children)
            //     ShowGameObjectNode(child);
            ImGui::TreePop();
        }
    }

    if (isMouseOverEmptySpace())
    {
        ImGui::OpenPopup("CreateMenu");
    }

    if (ImGui::BeginPopup("CreateMenu"))
    {

        if (ImGui::MenuItem("Create Empty"))
        {
            auto newGameObject = std::make_shared<GameObject>("empty_game_object");

            newGameObject->setPosition(glm::vec3(0.0f, 0.0f, 0.0f));

            Engine::s_application->getScene()->addGameObject(newGameObject);
        }

        if (ImGui::BeginMenu("3D Object"))
        {
            if (ImGui::MenuItem("Cube"))
            {
                // auto newObj = CreatePrimitive(PrimitiveType::Cube);
                // m_gameObjects.push_back(newObj);
            }
            if (ImGui::MenuItem("Sphere"))
            {
                // auto newObj = CreatePrimitive(PrimitiveType::Sphere);
                // m_gameObjects.push_back(newObj);
            }

            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }
    ImGui::End();
}

void Editor::showAssetsInfo()
{
    ImGui::Begin("Assets");
    m_uiAssetsWindow.draw();
    ImGui::End();
}

void Editor::drawTerminal()
{
    ImGui::Begin("Terminal");
    m_uiTerminal.draw();
    ImGui::End();
}

void Editor::drawLogWindow()
{
    ImGui::Begin("Logger");

    m_uiLogger.draw();

    ImGui::End();
}

void Editor::setSelectedGameObject(GameObject *gameObject)
{
    m_selectedGameObject = gameObject;

    if(auto path = Engine::s_application->getRenderer()->getRenderPath<elix::DefaultRender>())
        path->setSelectedGameObject(gameObject);

    if(auto path = Engine::s_application->getRenderer()->getRenderPath<StencilRender>())
        path->setSelectedGameObject(gameObject);

    m_selected = std::make_shared<InspectableGameObject>(m_selectedGameObject);
}

void Editor::showDebugInfo()
{
    showAllObjectsInTheScene();
    showProperties();
    showAssetsInfo();

    ImGui::Begin("Benchmark");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("RAM usage: %s", std::to_string(utilities::getRamUsage()).c_str());

    if (ImGui::Button("Build project"))
    {
        const auto project = ProjectManager::instance().getCurrentProject();

        if (!project)
            return;

        const std::string command = "cmake -S " + project->sourceDir + " -B " + project->buildDir + " && cmake --build " + project->buildDir;

        const auto result = elix::filesystem::executeCommand(command);
        
        ELIX_LOG_INFO(result.second);

        if (result.first == 0)
        {
            // if (!std::filesystem::exists(project->getSourceDir() + "/GameModule.cpp"))
            // {
            //     std::ofstream file(project->getSourceDir() + "/GameModule.cpp");
            //     file << "#include \"VelixFlow/ScriptMacros.hpp\"\n"
            //         << "ELIXIR_IMPLEMENT_GAME_MODULE()\n";
            //     file.close();
            //     ELIX_LOG_WARN("Missing GameModule.cpp — recreated default one.");
            // }

            if (elix::ScriptSystem::loadLibrary(project->buildDir + "libGameLib.so"))
            {
                project->projectLibrary = elix::ScriptSystem::getLibrary();

                for(const auto& scriptName : elix::ScriptSystem::getAvailableScripts())
                {
                    auto script = elix::ScriptSystem::createScript(scriptName);

                    if (!script)
                        ELIX_LOG_ERROR("Could not find script");
                    else
                    {
                        script->onStart();
                        script->onUpdate(0.0f);

                        if(auto reflectedScript = dynamic_cast<elix::ReflectedObject*>(script.get()))
                        {
                            for(const auto& property : reflectedScript->getProperties())
                                ELIX_LOG_INFO(property.first);
                        }
                    }
                    
                    ELIX_LOG_INFO("Script loaded: ", scriptName);
                }
            }
        }
    }

    ImGui::End();
}

void Editor::showProperties()
{
    ImGui::Begin("Properties");

    if(m_selected)
        m_selected->draw();

    ImGui::End();
}

Editor::Editor() = default;
