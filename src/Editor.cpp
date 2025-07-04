#include "Editor.hpp"
#include <ElixirCore/MeshComponent.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include <unordered_set>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstdlib>
#include "ElixirCore/LightComponent.hpp"
#include "ElixirCore/ParticleComponent.hpp"
#include "ElixirCore/AnimatorComponent.hpp"
#include "ElixirCore/Mouse.hpp"
#include "ElixirCore/Keyboard.hpp"
#include "ElixirCore/Raycasting.hpp"
#include "UIInputText.hpp"
#include "UILight.hpp"
#include "UIMaterial.hpp"
#include "UIMesh.hpp"
#include "UITransform.hpp"
#include "ElixirCore/RigidbodyComponent.hpp"
#include "ElixirCore/Utilities.hpp"
#include "ElixirCore/Filesystem.hpp"
#include "ElixirCore/LibrariesLoader.hpp"
#include <ElixirCore/ReflectedObject.hpp>
#include "InspectableGameObject.hpp"

#include <ElixirCore/ScriptsRegister.hpp>

#include <unistd.h>
#include <pwd.h>
#include <ElixirCore/AssetsLoader.hpp>
#include <ElixirCore/Logger.hpp>

#include <ElixirCore/DefaultRender.hpp>
#include "StencilRender.hpp"

#include "ProjectManager.hpp"

#include "Engine.hpp"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "../libraries/2ndParty/ImGuiColorTextEdit/TextEditor.h"

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

    style.Alpha = 1.0;
    style.WindowRounding = 3;
    style.GrabRounding = 1;
    style.GrabMinSize = 20;
    style.FrameRounding = 3;


    style.Colors[ImGuiCol_Text] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.00f, 0.40f, 0.41f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.00f, 1.00f, 1.00f, 0.65f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.44f, 0.80f, 0.80f, 0.18f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.44f, 0.80f, 0.80f, 0.27f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.44f, 0.81f, 0.86f, 0.66f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.18f, 0.21f, 0.73f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.54f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.27f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.22f, 0.29f, 0.30f, 0.71f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 1.00f, 1.00f, 0.44f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.74f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 1.00f, 0.68f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 1.00f, 1.00f, 0.36f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.76f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.00f, 0.65f, 0.65f, 0.46f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.01f, 1.00f, 1.00f, 0.43f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.62f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 1.00f, 1.00f, 0.33f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.42f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.74f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 1.00f, 1.00f, 0.22f);

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
        Engine::s_application->getScene()->saveSceneToFile(project->getEntryScene());
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
                
                writeFile(ProjectManager::instance().getCurrentProject()->getSourceDir() + "/" + classNameBuffer + ".hpp", hppContent);
                writeFile(ProjectManager::instance().getCurrentProject()->getSourceDir() + "/" + classNameBuffer + ".cpp", cppContent);

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
            const auto *const info = static_cast<DraggingInfo *>(payload->Data);

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

    if (m_fileEditorPath != currentFilePath && !m_fileEditorPath.empty())
    {
        currentFilePath = m_fileEditorPath;

        auto lang = TextEditor::LanguageDefinition::CPlusPlus();
        editor.SetLanguageDefinition(lang);

        std::string text = readFile(currentFilePath);
        editor.SetText(text);
    }

    if(!editor.GetText().empty())
        editor.Render("TextEditor");

    if (ImGui::Button("Save") && !m_fileEditorPath.empty())
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
    static auto folderTexture = ProjectManager::instance().getAssetsCache()->getAsset<elix::AssetTexture>(elix::filesystem::getExecutablePath().string() + "/resources/textures/folder.png");
    static auto fileTexture = ProjectManager::instance().getAssetsCache()->getAsset<elix::AssetTexture>(elix::filesystem::getExecutablePath().string() + "/resources/textures/file.png");

    if (folderTexture && !folderTexture->getTexture()->isBaked())
    {
        folderTexture->getTexture()->create();
        folderTexture->getTexture()->addDefaultParameters();
        folderTexture->getTexture()->bake();
    }
    if (fileTexture && !fileTexture->getTexture()->isBaked())
    {
        fileTexture->getTexture()->create();
        fileTexture->getTexture()->addDefaultParameters();
        fileTexture->getTexture()->bake();
    }

    ImTextureID folderIcon = folderTexture ? static_cast<ImTextureID>(static_cast<intptr_t>(folderTexture->getTexture()->getId())) : 0;
    ImTextureID fileIcon = fileTexture ? static_cast<ImTextureID>(static_cast<intptr_t>(fileTexture->getTexture()->getId())) : 0;

    ImGui::Begin("Assets");
    const float iconSize = 64.0f;
    const float padding = 16.0f;

    // TODO CHANGE IT LATER
    if (m_assetsPath.empty())
        m_assetsPath = ProjectManager::instance().getCurrentProject()->getFullPath();

    if (m_assetsPath.has_parent_path() && m_assetsPath != ProjectManager::instance().getCurrentProject()->getFullPath())
        if (ImGui::Button(".."))
            m_assetsPath = m_assetsPath.parent_path();

    float cellSize = iconSize + padding;
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = std::max(1, (int)(panelWidth / cellSize));

    int itemIndex = 0;
    ImGui::Columns(columnCount, nullptr, false);

    const std::unordered_set<std::string> restrictedExtensions{
       ".elixirproject"
    };

    for (const auto& entry : std::filesystem::directory_iterator(m_assetsPath))
    {
        const auto& path = entry.path();

        if (!is_directory(path))
        {
            auto ext = path.extension().string();

            if (restrictedExtensions.contains(ext))
                continue;
        }

        std::string name = path.filename().string();

        ImGui::PushID(name.c_str());

        ImTextureID icon = entry.is_directory() ? folderIcon : fileIcon;

        ImGui::ImageButton("", icon,
                           ImVec2(iconSize, iconSize),
                           ImVec2(0, 0), ImVec2(1, 1),
                           ImVec4(0, 0, 0, 0),
                           ImVec4(1, 1, 1, 1));


        if (ImGui::BeginPopupContextItem("AssetContextMenu"))
        {
            if (ImGui::MenuItem("Open in Editor"))
            {
                std::string fullPath = path.string();

                if (fullPath.ends_with(".cpp") || fullPath.ends_with(".hpp"))
                    m_fileEditorPath = fullPath;
            }

            if (ImGui::MenuItem("Show in File Manager"))
            {
                #ifdef _WIN32
                        std::string cmd = "explorer /select,\"" + path.string() + "\"";
                #elif __APPLE__
                        std::string cmd = "open -R \"" + path.string() + "\"";
                #else
                        std::string cmd = "xdg-open \"" + path.parent_path().string() + "\"";
                #endif
                        system(cmd.c_str());
            }

            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            m_draggingInfo.name = path.string();

            ImGui::SetDragDropPayload("ASSET_PATH", &m_draggingInfo, sizeof(m_draggingInfo));
            ImGui::Text("Dragging: %s", name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
        {
            if (entry.is_directory())
            {
            }
            else
            {
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            if (entry.is_directory())
            {
                m_assetsPath = path;
            }
            else
            {
            }
        }

        ImGui::TextWrapped("%s", name.c_str());

        ImGui::NextColumn();
        ImGui::PopID();
        itemIndex++;
    }

    ImGui::Columns(1);
    ImGui::End();
}

void Editor::drawTerminal()
{
    if (ImGui::Begin("Terminal"))
    {
        static char commandBuffer[256] = "";
        static std::vector<std::string> history;

        ImGui::BeginChild("TerminalOutput", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
        for (const auto &line : history)
        {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::EndChild();

        ImGui::PushItemWidth(-1);

        if (ImGui::InputText("##Command", commandBuffer, IM_ARRAYSIZE(commandBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            history.push_back("> " + std::string(commandBuffer));

            constexpr int kBufferSize = 128;
            std::array<char, kBufferSize> buffer{};
            std::string result;

#ifdef _WIN32
            std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
            std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(commandBuffer, "r"), pclose);
#endif

            if (pipe)
            {
                while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
                {
                    result += buffer.data();
                }

                if (!result.empty())
                {
                    history.push_back(result);
                }

                commandBuffer[0] = '\0';
                ImGui::SetKeyboardFocusHere(-1);
            }
        }

        ImGui::PopItemWidth();
    }

    ImGui::End();
}

void Editor::drawLogWindow()
{
    if (ImGui::Begin("Logger"))
    {
        // Add clear button
        // if (ImGui::Button("Clear")) {
        //     Logger::instance().clear();
        // }
        ImGui::SameLine();

        // TODO: Add filtering
        //  ImGui::Checkbox("Info", &show_info);
        //  ImGui::SameLine();
        //  ImGui::Checkbox("Warnings", &show_warnings);
        //  ImGui::SameLine();
        //  ImGui::Checkbox("Errors", &show_errors);
        //  Add copy button
        //  if (ImGui::Button("Copy to Clipboard"))
        //  {
        //      copyLogsToClipboard();
        //  }

        ImGui::Separator();

        ImGui::BeginChild("LogContent", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        const auto messages = elix::Logger::instance().getMessages();
        for (const auto &msg : messages)
        {
            auto time_t = std::chrono::system_clock::to_time_t(msg.timestamp);
            char time_str[20];
            std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&time_t));

            ImGui::TextDisabled("[%s] ", time_str);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(msg.color.r, msg.color.g, msg.color.b, 1.0f));
            ImGui::TextUnformatted(msg.message.c_str());
            ImGui::PopStyleColor();
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
    }
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

        const std::string command = "cmake -S " + project->getSourceDir() + " -B " + project->getBuildDir() + " && cmake --build " + project->getBuildDir();

        constexpr int kBufferSize = 128;
        std::array<char, kBufferSize> buffer;
        std::string result;

#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
#endif

        if (!pipe)
            ELIX_LOG_ERROR("Failed to execute command");

        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        {
            result += buffer.data();
        }

        ELIX_LOG_INFO(result.c_str());

        if (const int result = std::system(command.c_str()); result == 0)
        {
            void *const library = elix::LibrariesLoader::loadLibrary(project->getBuildDir() + "libGameLib.so");

            if (library)
            {
                using GetScriptsRegisterFunc = ScriptsRegister *(*)();

                auto getFunction = (GetScriptsRegisterFunc)elix::LibrariesLoader::getFunction("getScriptsRegister", library);

                if (!getFunction)
                {
                    ELIX_LOG_ERROR("Could not get function 'getScriptsRegister'");
                    return;
                }

                ScriptsRegister *s = getFunction();

                using InitFunc = const char **(*)(int *);

                InitFunc function = (InitFunc)elix::LibrariesLoader::getFunction("initScripts", library);

                if (!function)
                {
                    ELIX_LOG_ERROR("Could not get function 'initScripts'");
                    return;
                }

                int count = 0;
                const char **scripts = function(&count);

                for (int i = 0; i < count; ++i)
                {
                    std::string scriptName = scripts[i];

                    auto script = s->createScript(scriptName);

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
                    project->setProjectLibrary(library);
                    ELIX_LOG_INFO("Script loaded: ", scriptName);
                }

                elix::LibrariesLoader::closeLibrary(library);
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
