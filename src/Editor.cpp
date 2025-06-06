#include "Editor.hpp"
#include <ElixirCore/MeshComponent.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include <unordered_set>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <ElixirCore/LightManager.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdlib>
#include "ElixirCore/LightComponent.hpp"

#include "ElixirCore/AnimatorComponent.hpp"
#include "ElixirCore/AssetsManager.hpp"
#include "CameraManager.hpp"
#include "ElixirCore/Mouse.hpp"
#include "ElixirCore/Keyboard.hpp"
#include "ElixirCore/Raycasting.hpp"
#include "Renderer.hpp"
#include "UIInputText.hpp"
#include "UILight.hpp"
#include "UIMaterial.hpp"
#include "UIMesh.hpp"
#include "UITransform.hpp"
#include "ElixirCore/ScriptsLoader.hpp"
#include "ElixirCore/RigidbodyComponent.hpp"
#include "ElixirCore/SceneManager.hpp"
#include "ElixirCore/SkeletalMeshComponent.hpp"
#include "ElixirCore/StaticMeshComponent.hpp"
#include "ElixirCore/WindowsManager.hpp"
#include "ElixirCore/Utilities.hpp"

#include <unistd.h>
#include <pwd.h>
#include <ElixirCore/AssetsLoader.hpp>
#include <ElixirCore/Logger.hpp>

#include "ProjectManager.hpp"

void BeginDockSpace()
{
    ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
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
        ImGuiID dockIdRight   = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, nullptr, &dockMainId);
        ImGuiID dockIdLeft    = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.20f, nullptr, &dockMainId);
        ImGuiID dockIdBottom  = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.25f, nullptr, &dockMainId);
        ImGuiID dockIdCenter  = dockMainId;

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

Editor& Editor::instance()
{
    static Editor instance;
    return instance;
}

void Editor::updateInput()
{
    auto* window = window::WindowsManager::instance().getCurrentWindow();

    if (input::Keyboard.isKeyReleased(input::KeyCode::DELETE) && m_selectedGameObject)
    {
        if (SceneManager::instance().getCurrentScene())
        {
            if (SceneManager::instance().getCurrentScene()->deleteGameObject(m_selectedGameObject))
                setSelectedGameObject(nullptr);
        }
    }

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::V) && m_savedGameObject)
    {
        SceneManager::instance().getCurrentScene()->addGameObject(m_savedGameObject);
        setSelectedGameObject(m_savedGameObject.get());
    }

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::C) && m_selectedGameObject)
    {
        std::unordered_set<std::string> existingNames;

        for (const auto& obj : SceneManager::instance().getCurrentScene()->getGameObjects())
            existingNames.insert(obj->getName());

        std::string uniqueName = utilities::generateUniqueName(m_selectedGameObject->getName(), existingNames);

        auto newGameObject = std::make_shared<GameObject>(uniqueName);

        newGameObject->setPosition(m_selectedGameObject->getPosition());
        newGameObject->setRotation(m_selectedGameObject->getRotation());
        newGameObject->setScale(m_selectedGameObject->getScale());
        newGameObject->addComponent<RigidbodyComponent>(newGameObject);

        // if (m_selectedGameObject->hasComponent<SkeletalMeshComponent>())
        // {
        //     newGameObject->addComponent<SkeletalMeshComponent>(m_selectedGameObject->getComponent<SkeletalMeshComponent>()->getModel());
        //     physics::PhysicsController::instance().resizeCollider({1.0f, 2.0f, 1.0f}, newGameObject);
        // }
        // else if (m_selectedGameObject->hasComponent<StaticMeshComponent>())
        //     newGameObject->addComponent<StaticMeshComponent>(m_selectedGameObject->getComponent<StaticMeshComponent>()->getModel());

        newGameObject->overrideMaterials = m_selectedGameObject->overrideMaterials;

        m_savedGameObject = newGameObject;
    }

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::Z))
        m_actionsManager.undo();

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::Y))
        m_actionsManager.redo();

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::S))
    {
        const auto project = ProjectManager::instance().getCurrentProject();

        //TODO CHANGE IT LATER 'project->getEntryScene()'
        SceneManager::saveSceneToFile(SceneManager::instance().getCurrentScene().get(), project->getEntryScene());
    }

    if (input::Keyboard.isKeyReleased(input::KeyCode::W))
        m_transformMode = TransformMode::Translate;

    if (input::Keyboard.isKeyReleased(input::KeyCode::E))
        m_transformMode = TransformMode::Scale;

    if (input::Keyboard.isKeyReleased(input::KeyCode::R))
        m_transformMode = TransformMode::Rotate;

    // if (input::Mouse.isLeftButtonPressed())
    // {
    //     if (ImGuiIO& io = ImGui::GetIO(); !io.WantCaptureMouse && !ImGuizmo::IsUsing())
    //     {
    //         glm::vec2 mouseNDC;
    //
    //         auto* camera = CameraManager::getInstance().getActiveCamera();
    //         double xpos, ypos;
    //         glfwGetCursorPos(window->getOpenGLWindow(), &xpos, &ypos);
    //
    //         float x = (2.0f * xpos) / static_cast<float>(window->getWidth()) - 1.0f;
    //         float y = 1.0f - (2.0f * ypos) / static_cast<float>(window->getHeight());
    //
    //         mouseNDC = glm::vec2(x, y);
    //
    //         float aspectRatio = static_cast<float>(window->getWidth()) / static_cast<float>(window->getHeight());
    //
    //         glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
    //         glm::mat4 view = camera->getViewMatrix();
    //
    //         glm::vec4 rayClip(mouseNDC.x, mouseNDC.y, -1.0f, 1.0f);
    //         glm::vec4 rayEye = glm::inverse(projection) * rayClip;
    //         rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    //
    //         glm::vec3 rayWorld = glm::normalize(glm::vec3(glm::inverse(view) * rayEye));
    //
    //         glm::vec3 origin = glm::vec3(glm::inverse(view)[3]);
    //
    //         physics::raycasting::Ray ray{};
    //         physics::raycasting::RaycastingResult result;
    //
    //         ray.maxDistance = 1000.0f;
    //         ray.direction = rayWorld;
    //         ray.origin = origin;
    //
    //         if (physics::raycasting::shoot(ray, result))
    //         {
    //             const auto* actor = result.hit.block.actor;
    //
    //             auto* gameObject = static_cast<GameObject*>(actor->userData);
    //
    //             m_selectedGameObject = gameObject;
    //         }
    //     }
    // }
    //
    // if (m_selectedGameObject)
    // {
    //     ImGuizmo::SetOrthographic(false);
    //     ImGuizmo::SetDrawlist();
    //     ImGuizmo::SetRect(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    //     ImGuizmo::BeginFrame();
    //
    //     glm::mat4 modelMatrix = m_selectedGameObject->getTransformMatrix();
    //
    //     const auto* window = window::WindowsManager::instance().getCurrentWindow();
    //
    //     float aspectRatio = static_cast<float>(window->getWidth()) / static_cast<float>(window->getHeight());
    //
    //     glm::mat4 viewMatrix = CameraManager::getInstance().getActiveCamera()->getViewMatrix();
    //     glm::mat4 projMatrix = CameraManager::getInstance().getActiveCamera()->getProjectionMatrix(aspectRatio);
    //
    //     static ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;
    //
    //     ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    //
    //     switch (m_transformMode)
    //     {
    //         case TransformMode::Translate: op = ImGuizmo::TRANSLATE; break;
    //         case TransformMode::Rotate:    op = ImGuizmo::ROTATE;    break;
    //         case TransformMode::Scale:     op = ImGuizmo::SCALE;     break;
    //     }
    //
    //     ImGuizmo::Manipulate(glm::value_ptr(viewMatrix), glm::value_ptr(projMatrix),
    //                          op, currentGizmoMode,
    //                          glm::value_ptr(modelMatrix));
    //
    //     if (ImGuizmo::IsUsing())
    //     {
    //         glm::vec3 translation, rotation, scale;
    //         ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(modelMatrix),
    //                                               glm::value_ptr(translation),
    //                                               glm::value_ptr(rotation),
    //                                               glm::value_ptr(scale));
    //         m_selectedGameObject->setPosition(translation);
    //         m_selectedGameObject->setRotation(rotation);
    //         m_selectedGameObject->setScale(scale);
    //     }
    // }

}

void Editor::update()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();

    if (m_state == State::Editor)
    {
        ImGuizmo::BeginFrame();
        showEditor();
    }
    else
        showStart();

    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (const ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
}

std::string getHome()
{
    return (std::getenv("HOME") + std::string("/Documents/ElixirProjects"));
}

void Editor::showStart()
{
    const ImGuiIO& io = ImGui::GetIO();
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

        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.is_directory())
            {
                if (ImGui::Button(entry.path().filename().string().c_str()))
                {
                    auto project = new Project();

                    if (!ProjectManager::instance().loadConfigInProject(entry.path().string() + "/Project.elixirproject", project))
                    {
                        LOG_ERROR("Failed to load project");
                        delete project;
                        continue;
                    }

                    if (!ProjectManager::instance().loadProject(project))
                    {
                        LOG_ERROR("Failed to load project");
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
                LOG_ERROR("Failed to create project");
                ImGui::End();
                return;
            }

            if (!ProjectManager::instance().loadProject(project))
            {
                LOG_ERROR("Failed to load project");
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

void Editor::showMenuBar()
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::MenuItem("New Project");
            ImGui::MenuItem("Open Project");
            ImGui::MenuItem("Save Scene");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void Editor::showViewPort()
{
    ImGui::Begin("Scene View", nullptr,
             ImGuiWindowFlags_NoTitleBar |
             ImGuiWindowFlags_NoCollapse |
             ImGuiWindowFlags_NoMove |
             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse /*| ImGuiWindowFlags_MenuBar*/);

    // ImGuiIO& io = ImGui::GetIO();
    // ImGui::Text("MousePos: %.1f, %.1f", io.MousePos.x, io.MousePos.y);
    // ImGui::Text("MouseDown: %s", io.MouseDown[0] ? "true" : "false");
    // ImGui::Text("Input: %s", io.WantCaptureMouse ? "true" : "false");
    // ImGui::Text("ImGuizmo IsOver: %s, IsUsing: %s",
    //             ImGuizmo::IsOver() ? "true" : "false",
    //             ImGuizmo::IsUsing() ? "true" : "false");

    const float windowWidth = ImGui::GetContentRegionAvail().x;
    const float windowHeight = ImGui::GetContentRegionAvail().y;
    const ImVec2 cursorPosition = ImGui::GetCursorScreenPos();
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();

    ImGuiIO& io = ImGui::GetIO();
    float dpiScale = io.DisplayFramebufferScale.x;
    int fbWidth = (int)(contentSize.x * dpiScale);
    int fbHeight = (int)(contentSize.y * dpiScale);

    Renderer::instance().rescaleBuffer(fbWidth, fbHeight);
    window::MainWindow::setViewport(0, 0, static_cast<int>(contentSize.x), static_cast<int>(contentSize.y));

    auto fboTexture = Renderer::instance().getFrameBufferTexture();
    ImGui::Image((ImTextureID)(intptr_t)fboTexture, contentSize, ImVec2(0, 1), ImVec2(1, 0));

    ImGui::SetCursorScreenPos(cursorPosition);
    ImGui::InvisibleButton("GizmoInputCatcher", contentSize,
    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    ImGui::SetItemAllowOverlap();

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            const auto* const info = static_cast<DraggingInfo*>(payload->Data);

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

                SceneManager::instance().getCurrentScene()->setSkybox(skybox);
            }

            //TODO Replace this shit somehow
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
                        newGameObject->addComponent<MeshComponent>(model->getModel());
                    else
                        LOG_WARN("Failed to load asset model");
                }
                else
                    LOG_WARN("Failed to load cache");

                // if (auto staticModel = AssetsManager::instance().getStaticModelByName(path.filename()))
                //     newGameObject->addComponent<StaticMeshComponent>(staticModel);
                // else if (auto skinnedModel = AssetsManager::instance().getSkinnedModelByName(path.filename()))
                // {
                //     newGameObject->addComponent<SkeletalMeshComponent>(skinnedModel);
                //     physics::PhysicsController::instance().resizeCollider({1.0f, 2.0f, 1.0f}, newGameObject);
                // }

                SceneManager::instance().getCurrentScene()->addGameObject(newGameObject);
            }

            std::cout << "Dropped asset into scene: " << info->name << std::endl;
        }
        ImGui::EndDragDropTarget();
    }

    if (m_selectedGameObject)
    {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(cursorPosition.x, cursorPosition.y, windowWidth, windowHeight);
        glm::mat4 modelMatrix = m_selectedGameObject->getTransformMatrix();

        float aspectRatio = windowWidth / windowHeight;

        glm::mat4 viewMatrix = CameraManager::getInstance().getActiveCamera()->getViewMatrix();
        glm::mat4 projMatrix = CameraManager::getInstance().getActiveCamera()->getProjectionMatrix(aspectRatio);

        ImGuizmo::OPERATION operation{ImGuizmo::TRANSLATE};

        switch (m_transformMode)
        {
            case TransformMode::Translate: operation = ImGuizmo::TRANSLATE; break;
            case TransformMode::Rotate:    operation = ImGuizmo::ROTATE;    break;
            case TransformMode::Scale:     operation = ImGuizmo::SCALE;     break;
        }

        ImGuizmo::Manipulate(
            glm::value_ptr(viewMatrix),
            glm::value_ptr(projMatrix),
            operation,
            ImGuizmo::WORLD,
            glm::value_ptr(modelMatrix),
            nullptr,
            nullptr,
            nullptr
        );

        if (ImGuizmo::IsUsing())
        {
            glm::vec3 translation, rotation, scale;
            ImGuizmo::DecomposeMatrixToComponents(
                glm::value_ptr(modelMatrix),
                glm::value_ptr(translation),
                glm::value_ptr(rotation),
                glm::value_ptr(scale)
            );

            m_selectedGameObject->setPosition(translation);
            m_selectedGameObject->setRotation(rotation);
            m_selectedGameObject->setScale(scale);
        }
    }

    if (input::Mouse.isLeftButtonPressed())
    {
        if (ImGui::IsItemHovered() && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            ImVec2 mousePos = ImGui::GetMousePos();

            float localX = mousePos.x - cursorPosition.x;
            float localY = mousePos.y - cursorPosition.y;

            if (localX >= 0 && localY >= 0 && localX < windowWidth && localY < windowHeight)
            {
                float x = (2.0f * localX) / windowWidth - 1.0f;
                float y = 1.0f - (2.0f * localY) / windowHeight;

                glm::vec2 mouseNDC(x, y);

                float aspectRatio = windowWidth / windowHeight;

                auto* camera = CameraManager::getInstance().getActiveCamera();

                glm::mat4 projection = camera->getProjectionMatrix(aspectRatio);
                glm::mat4 view = camera->getViewMatrix();

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
                    auto* actor = result.hit.block.actor;
                    auto* gameObject = static_cast<GameObject*>(actor->userData);

                    if (gameObject)
                        setSelectedGameObject(gameObject);
                }
            }
        }
    }

    ImGui::End();
}


Editor::~Editor() = default;

void Editor::showAllObjectsInTheScene()
{
    if (!SceneManager::instance().getCurrentScene())
        return;

    const auto& objects = SceneManager::instance().getCurrentScene()->getGameObjects();

    ImGui::Begin("Scene hierarchy");

    for (const auto& object : objects)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

        if (object.get() == m_selectedGameObject)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool isNodeOpened = ImGui::TreeNodeEx((void*)(intptr_t)object.get(), flags, "%s", object->getName().c_str());

        if (ImGui::IsItemClicked())
            setSelectedGameObject(object.get());

        if (isNodeOpened)
        {
            // for (auto& child : object->children)
            //     ShowGameObjectNode(child);
            ImGui::TreePop();
        }
    }

    ImGui::End();
}

void Editor::showAssetsInfo()
{
    static auto folderTexture = ProjectManager::instance().getAssetsCache()->getAsset<elix::AssetTexture>(filesystem::getTexturesFolderPath().string() + "/folder.png");
    static auto fileTexture = ProjectManager::instance().getAssetsCache()->getAsset<elix::AssetTexture>(filesystem::getTexturesFolderPath().string() + "/file.png");

    if (folderTexture && !folderTexture->getTexture()->isBaked())
        folderTexture->getTexture()->bake();
    if (fileTexture && !fileTexture->getTexture()->isBaked())
        fileTexture->getTexture()->bake();

    static ImTextureID folderIcon = folderTexture ? static_cast<ImTextureID>(static_cast<intptr_t>(folderTexture->getTexture()->getId())) : 0;
    static ImTextureID fileIcon = fileTexture ? static_cast<ImTextureID>(static_cast<intptr_t>(fileTexture->getTexture()->getId())) : 0;

    ImGui::Begin("Assets");
    const float iconSize = 64.0f;
    const float padding = 16.0f;

    //TODO CHANGE IT LATER
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
    const std::vector<std::string> allowedExtensions{".png", ".mat", ".fbx", ".anim", ".obj", ".hdr", ".cpp", ".hpp", ".h"};

    for (const auto& entry : std::filesystem::directory_iterator(m_assetsPath))
    {
        const auto& path = entry.path();

        if (!is_directory(path))
        {
            bool allow{false};

            for (const auto& extension : allowedExtensions)
                if (extension ==  path.extension())
                {
                    allow = true;
                    break;
                }

            if (!allow)
            {
                ImGui::End();
                return;
            }
        }

        std::string name = path.filename().string();

        ImGui::PushID(name.c_str());

        ImTextureID icon = entry.is_directory() ? folderIcon : fileIcon;

        ImGui::ImageButton("", icon,
        ImVec2(iconSize, iconSize),
        ImVec2(0, 0), ImVec2(1, 1),
        ImVec4(0, 0, 0, 0),
        ImVec4(1, 1, 1, 1));

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
                if (entry.path().extension() == ".mat")
                {
                    // auto material = AssetsManager::instance().getMaterialByName(entry.path().filename().string());

                    // TODO: make this better
                    // if (material)
                    // {
                        // m_selectedMaterial = material;
                        // m_selectedGameObject = nullptr;
                    // }
                }
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


void Editor::showGuizmosInfo()
{
    if (!m_selectedGameObject)
        return;

    // ImGuizmo::SetOrthographic(false);
    //
    // ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    //
    // ImVec2 contentPos = ImGui::GetCursorScreenPos();
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    // ImGuizmo::SetRect(contentPos.x, contentPos.y, contentSize.x, contentSize.y);

    glm::mat4 modelMatrix = m_selectedGameObject->getTransformMatrix();

    float aspectRatio = contentSize.x / contentSize.y;

    glm::mat4 viewMatrix = CameraManager::getInstance().getActiveCamera()->getViewMatrix();
    glm::mat4 projMatrix = CameraManager::getInstance().getActiveCamera()->getProjectionMatrix(aspectRatio);

    static ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;

    switch (m_transformMode)
    {
        case TransformMode::Translate: op = ImGuizmo::TRANSLATE; break;
        case TransformMode::Rotate:    op = ImGuizmo::ROTATE;    break;
        case TransformMode::Scale:     op = ImGuizmo::SCALE;     break;
    }

    ImGui::Text("Window hovered: %d", ImGui::IsWindowHovered());
    ImGui::Text("WantCaptureMouse: %d", ImGui::GetIO().WantCaptureMouse);
    ImGui::Text("MousePos: (%.1f, %.1f)", ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);

    ImGui::Text("Imguizmo is over: %s", ImGuizmo::IsOver() ? "true" : "false");


    ImGuizmo::Manipulate(glm::value_ptr(viewMatrix), glm::value_ptr(projMatrix),
                     op, currentGizmoMode,
                     glm::value_ptr(modelMatrix));

    if (ImGuizmo::IsOver())
    {
        ImGui::GetIO().WantCaptureMouse = false;
    }


    if (ImGuizmo::IsUsing())
    {
        ImGui::Text("ImGuizmo result:");

        glm::vec3 translation, rotation, scale;
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(modelMatrix),
                                              glm::value_ptr(translation),
                                              glm::value_ptr(rotation),
                                              glm::value_ptr(scale));
        m_selectedGameObject->setPosition(translation);
        m_selectedGameObject->setRotation(rotation);
        m_selectedGameObject->setScale(scale);
    }
}

void Editor::drawTerminal()
{
    if (ImGui::Begin("Terminal"))
    {
        static char commandBuffer[256] = "";
        static std::vector<std::string> history;

        ImGui::BeginChild("TerminalOutput", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
        for (const auto& line : history) {
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
    if (ImGui::Begin("Logger")) {
        // Add clear button
        // if (ImGui::Button("Clear")) {
        //     Logger::instance().clear();
        // }
        ImGui::SameLine();

        //TODO: Add filtering
        // ImGui::Checkbox("Info", &show_info);
        // ImGui::SameLine();
        // ImGui::Checkbox("Warnings", &show_warnings);
        // ImGui::SameLine();
        // ImGui::Checkbox("Errors", &show_errors);
        // Add copy button
        // if (ImGui::Button("Copy to Clipboard"))
        // {
        //     copyLogsToClipboard();
        // }

        ImGui::Separator();

        ImGui::BeginChild("LogContent", ImVec2(0, 0), false,
                         ImGuiWindowFlags_HorizontalScrollbar);

        const auto messages = elix::Logger::instance().getMessages();
        for (const auto& msg : messages) {
            // Timestamp
            auto time_t = std::chrono::system_clock::to_time_t(msg.timestamp);
            char time_str[20];
            std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&time_t));

            ImGui::TextDisabled("[%s] ", time_str);
            ImGui::SameLine();

            // Colored message
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(msg.color.r, msg.color.g, msg.color.b, 1.0f));
            ImGui::TextUnformatted(msg.message.c_str());
            ImGui::PopStyleColor();
        }

        // Auto-scroll
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void Editor::setSelectedGameObject(GameObject *gameObject)
{
    m_selectedGameObject = gameObject;
    Renderer::instance().setSelectedGameObject(m_selectedGameObject);
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
            LOG_ERROR("Failed to execute command");

        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        {
            result += buffer.data();
        }

        LOG_INFO(result.c_str());

        if (const int result = std::system(command.c_str()); result == 0)
        {
            void* const library = ScriptsLoader::instance().loadLibrary(project->getBuildDir() + "libGameLib.so");

            if (library)
            {
                ScriptsLoader::instance().library = library;

                using GetScriptsRegisterFunc = ScriptsRegister* (*)();

                auto getFunction = (GetScriptsRegisterFunc)ScriptsLoader::instance().getFunction("getScriptsRegister", ScriptsLoader::instance().library);

                if (!getFunction)
                {
                    LOG_ERROR("Could not get function 'getScriptsRegister'");
                    return;
                }

                ScriptsRegister* s = getFunction();

                using InitFunc = const char**(*)(int*);

                InitFunc function = (InitFunc)ScriptsLoader::instance().getFunction("initScripts", library);

                if (!function)
                {
                    LOG_ERROR("Could not get function 'initScripts'");
                    return;
                }

                int count = 0;
                const char** scripts = function(&count);

                for (int i = 0; i < count; ++i)
                {
                    std::string scriptName = scripts[i];

                    auto script = s->createScript(scriptName);

                    if (!script)
                        LOG_ERROR("Could not find script");
                    else
                    {
                        script->onStart();
                        script->onUpdate(0.0f);
                    }

                    LOG_INFO("Script loaded: %s", scriptName);
                }
            }
        }
    }

    ImGui::End();

}


void Editor::showObjectInfo()
{
    if (!m_selectedGameObject)
    {
        ImGui::End();
        return;
    }

    std::string objectName = m_selectedGameObject->getName();

    if (UIInputText::draw(objectName))
        m_selectedGameObject->setName(objectName);

    if (ImGui::BeginTabBar("Tabs"))
    {
        if (ImGui::BeginTabItem("Transform"))
        {
            UITransform::draw(m_selectedGameObject);

            // const common::Model* model{nullptr};
            //
            // if (m_selectedGameObject->hasComponent<SkeletalMeshComponent>())
            //     model = m_selectedGameObject->getComponent<SkeletalMeshComponent>()->getModel();
            // else if (m_selectedGameObject->hasComponent<StaticMeshComponent>())
            //     model = m_selectedGameObject->getComponent<StaticMeshComponent>()->getModel();
            // if (model)
            //     for (int meshIndex = 0; meshIndex < model->getMeshesSize(); meshIndex++)
            //        UIMesh::draw(model->getMesh(meshIndex), meshIndex, m_selectedGameObject);


            // m_selectedGameObject->overrideMaterials
            if (m_selectedGameObject->hasComponent<MeshComponent>())
                if (auto model = m_selectedGameObject->getComponent<MeshComponent>()->getModel())
                    for (int meshIndex = 0; meshIndex < model->getNumMeshes(); meshIndex++)
                        UIMesh::draw(model->getMesh(meshIndex), meshIndex, m_selectedGameObject);


        //     std::vector<std::string> allModelNames;
        //     std::vector<const char*> convertedModelNames;
        //     std::string currentModelName;
        //
        //     if (m_selectedGameObject->hasComponent<SkeletalMeshComponent>())
        //     {
        //         currentModelName = m_selectedGameObject->getComponent<SkeletalMeshComponent>()->getModel()->getName();
        //         allModelNames = AssetsManager::instance().getAllSkinnedModelsNames();
        //     }
        //     else if (m_selectedGameObject->hasComponent<StaticMeshComponent>())
        //     {
        //         currentModelName = m_selectedGameObject->getComponent<StaticMeshComponent>()->getModel()->getName();
        //         allModelNames = AssetsManager::instance().getAllStaticModelsNames();
        //     }
        //
        //      for (const auto& modelName : allModelNames)
        //         convertedModelNames.push_back(modelName.c_str());
        //
        //     ImGui::Text("Model %s", currentModelName.c_str());
        //     ImGui::SameLine();
        //
        //     auto i = std::ranges::find_if(convertedModelNames,
        // [&currentModelName](const std::string& modelName){return currentModelName == modelName;});
        //
        //     m_selectedModelIndex = std::distance(convertedModelNames.begin(), i);
        //
        //     if (ImGui::Combo("##Model combo", &m_selectedModelIndex, convertedModelNames.data(), static_cast<int>(convertedModelNames.size())))
        //     {
        //         if (m_selectedGameObject->hasComponent<SkeletalMeshComponent>())
        //             if (auto m = AssetsManager::instance().getSkinnedModelByName(convertedModelNames[m_selectedModelIndex]))
        //                 m_selectedGameObject->getComponent<SkeletalMeshComponent>()->setModel(m);
        //         if (m_selectedGameObject->hasComponent<StaticMeshComponent>())
        //             if (auto m = AssetsManager::instance().getStaticModelByName(convertedModelNames[m_selectedModelIndex]))
        //                 m_selectedGameObject->getComponent<StaticMeshComponent>()->setModel(m);
        //     }

            ImGui::SeparatorText("Components");

            if (m_selectedGameObject->hasComponent<AnimatorComponent>())
            {
                ImGui::CollapsingHeader("Animator");
            }

            if (m_selectedGameObject->hasComponent<LightComponent>())
            {
                if (ImGui::CollapsingHeader("Light"))
                {
                    UILight::draw(m_selectedGameObject->getComponent<LightComponent>()->getLight());
                }
            }

            if (m_selectedGameObject->hasComponent<ScriptComponent>())
            {
                auto scriptComponent = m_selectedGameObject->getComponent<ScriptComponent>();

                const auto& scripts = scriptComponent->getScripts();

                if (ImGui::CollapsingHeader("Scripts"))
                {
                    for (const auto& [scriptName, script] : scripts)
                    {
                        ImGui::Text("%s", scriptName.c_str());

                        ImGui::SameLine();

                        if (ImGui::Button("Simulate script"))
                            scriptComponent->setUpdateScripts(true);
                    }

                    ImGui::Button("Attach script");
                }
            }

            if (ImGui::Button("Add Component"))
                ImGui::OpenPopup("AddComponentPopup");

            static char searchBuffer[128] = "";

            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                ImGui::InputTextWithHint("##search", "Search...", searchBuffer, IM_ARRAYSIZE(searchBuffer));

                ImGui::Separator();

                const std::vector<std::string> availableComponents = {
                    "Animator",
                    "Script",
                    "Light"
                };

                for (const auto& comp : availableComponents)
                {
                    if (strlen(searchBuffer) == 0 || comp.find(searchBuffer) != std::string::npos)
                    {
                        if (ImGui::MenuItem(comp.c_str()))
                        {
                            if (comp == "Animator" && !m_selectedGameObject->hasComponent<AnimatorComponent>())
                                m_selectedGameObject->addComponent<AnimatorComponent>();
                            else if (comp == "Script" && !m_selectedGameObject->hasComponent<ScriptComponent>())
                                m_selectedGameObject->addComponent<ScriptComponent>();
                            else if (comp == "Light" && !m_selectedGameObject->hasComponent<LightComponent>())
                            {
                                m_selectedGameObject->addComponent<LightComponent>(lighting::Light{});
                                LightManager::instance().addLight(m_selectedGameObject->getComponent<LightComponent>()->getLight());
                            }

                            ImGui::CloseCurrentPopup();
                            break;
                        }
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::EndTabItem();
        }

        if (m_selectedGameObject->hasComponent<MeshComponent>() && ImGui::BeginTabItem("Bones"))
        {
            displayBonesHierarchy(m_selectedGameObject->getComponent<MeshComponent>()->getModel()->getSkeleton());
            ImGui::EndTabItem();
        }

        if (m_selectedGameObject->hasComponent<MeshComponent>() && ImGui::BeginTabItem("Animation"))
        {
            auto component = m_selectedGameObject->getComponent<MeshComponent>();

            for (const auto& anim : component->getModel()->getAnimations())
            {
                if (ImGui::Button(anim->name.c_str()))
                    if (auto* animation = component->getModel()->getAnimation(anim->name))
                    {
                        if (!m_selectedGameObject->hasComponent<AnimatorComponent>())
                            m_selectedGameObject->addComponent<AnimatorComponent>();

                        animation->skeletonForAnimation = component->getModel()->getSkeleton();
                        m_selectedGameObject->getComponent<AnimatorComponent>()->playAnimation(animation);
                    }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

void Editor::showProperties()
{
    ImGui::Begin("Properties");

    if (m_selectedGameObject)
        showObjectInfo();
    else if (m_selectedMaterial)
        UIMaterial::draw(m_selectedMaterial);

    // if (m_gameLibrary)
    // {
    //     using InitFunc = const char**(*)(int*);
    //
    //     InitFunc function = (InitFunc)ScriptsLoader::instance().getFunction("initScripts", m_gameLibrary);
    //
    //     int count = 0;
    //     const char** scripts = function(&count);
    //
    //     for (int i = 0; i < count; ++i)
    //     {
    //         std::string scriptName = scripts[i];
    //         ImGui::Text("Script: %s", scriptName.c_str());
    //         // updateScript(scriptName.c_str(), 0.f);
    //     }
    //
    //
    //
    // }

    ImGui::End();
}

void Editor::displayBonesHierarchy(Skeleton* skeleton, common::BoneInfo* parent)
{
    if (!skeleton)
        return;

    if (const auto bone = parent ? parent : skeleton->getParent(); ImGui::TreeNode(bone->name.c_str()))
    {
        for (const int& childBone : bone->children)
        {
            displayBonesHierarchy(skeleton, skeleton->getBone(childBone));
        }

        ImGui::TreePop();
    }
}


Editor::Editor() = default;
