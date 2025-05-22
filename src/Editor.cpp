#include "Editor.hpp"

#include <glad.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include <unordered_set>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glm/gtc/type_ptr.hpp>

#include "ElixirCore/AnimatorComponent.hpp"
#include "ElixirCore/AssetsManager.hpp"
#include "CameraManager.hpp"
#include "DebugLine.hpp"
#include "ElixirCore/Mouse.hpp"
#include "ElixirCore/Keyboard.hpp"
#include "ElixirCore/Raycasting.hpp"
#include "Renderer.hpp"
#include "ElixirCore/ScriptsLoader.hpp"
#include "ElixirCore/RigidbodyComponent.hpp"
#include "ElixirCore/SceneManager.hpp"
#include "ElixirCore/SkeletalMeshComponent.hpp"
#include "ElixirCore/StaticMeshComponent.hpp"
#include "ElixirCore/WindowsManager.hpp"
#include "ElixirCore/Utilities.hpp"

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

        ImGui::DockBuilderDockWindow("Scene hierarchy", dockIdLeft);
        ImGui::DockBuilderDockWindow("Properties", dockIdRight);
        ImGui::DockBuilderDockWindow("Assets", dockIdBottom);
        ImGui::DockBuilderDockWindow("Benchmark", dockIdLeftBottom);
        // ImGui::DockBuilderDockWindow("Scene View", dockIdCenter);

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
        if (SceneManager::instance().getCurrentScene()->deleteGameObject(m_selectedGameObject))
            m_selectedGameObject = nullptr;

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::V) && m_savedGameObject)
    {
        SceneManager::instance().getCurrentScene()->addGameObject(m_savedGameObject);
        m_selectedGameObject = m_savedGameObject.get();
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
        // newGameObject->addComponent<RigidbodyComponent>(newGameObject);

        if (m_selectedGameObject->hasComponent<SkeletalMeshComponent>())
        {
            newGameObject->addComponent<SkeletalMeshComponent>(m_selectedGameObject->getComponent<SkeletalMeshComponent>()->getModel());
            // physics::PhysicsController::instance().resizeCollider({1.0f, 2.0f, 1.0f}, newGameObject);
        }
        else if (m_selectedGameObject->hasComponent<StaticMeshComponent>())
            newGameObject->addComponent<StaticMeshComponent>(m_selectedGameObject->getComponent<StaticMeshComponent>()->getModel());

        newGameObject->overrideMaterials = m_selectedGameObject->overrideMaterials;

        m_savedGameObject = newGameObject;
    }

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::Z))
        m_actionsManager.undo();

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::Y))
        m_actionsManager.redo();

    if (input::Keyboard.isKeyPressed(input::KeyCode::LeftCtrl) && input::Keyboard.isKeyReleased(input::KeyCode::S))
        SceneManager::saveObjectsIntoFile(SceneManager::instance().getCurrentScene()->getGameObjects(), filesystem::getMapsFolderPath().string() + "/test_scene.json");

    if (input::Keyboard.isKeyReleased(input::KeyCode::W))
        m_transformMode = TransformMode::Translate;

    if (input::Keyboard.isKeyReleased(input::KeyCode::E))
        m_transformMode = TransformMode::Scale;

    if (input::Keyboard.isKeyReleased(input::KeyCode::R))
        m_transformMode = TransformMode::Rotate;

    if (input::Mouse.isLeftButtonPressed())
    {
        if (ImGuiIO& io = ImGui::GetIO(); !io.WantCaptureMouse && !ImGuizmo::IsUsing())
        {
            glm::vec2 mouseNDC;

            auto* camera = CameraManager::getInstance().getActiveCamera();
            double xpos, ypos;
            glfwGetCursorPos(window->getOpenGLWindow(), &xpos, &ypos);

            float x = (2.0f * xpos) / static_cast<float>(window->getWidth()) - 1.0f;
            float y = 1.0f - (2.0f * ypos) / static_cast<float>(window->getHeight());

            mouseNDC = glm::vec2(x, y);

            float aspectRatio = static_cast<float>(window->getWidth()) / static_cast<float>(window->getHeight());

            glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
            glm::mat4 view = camera->getViewMatrix();

            glm::vec4 rayClip(mouseNDC.x, mouseNDC.y, -1.0f, 1.0f);
            glm::vec4 rayEye = glm::inverse(projection) * rayClip;
            rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

            glm::vec3 rayWorld = glm::normalize(glm::vec3(glm::inverse(view) * rayEye));

            glm::vec3 origin = glm::vec3(glm::inverse(view)[3]);

            // physics::raycasting::Ray ray{};
            // physics::raycasting::RaycastingResult result;
            //
            // ray.maxDistance = 1000.0f;
            // ray.direction = rayWorld;
            // ray.origin = origin;
            //
            // if (physics::raycasting::shoot(ray, result))
            // {
            //     const auto* actor = result.hit.block.actor;
            //
            //     auto* gameObject = static_cast<GameObject*>(actor->userData);
            //
            //     m_selectedGameObject = gameObject;
            // }
        }
    }

    if (m_selectedGameObject)
    {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        ImGuizmo::BeginFrame();

        glm::mat4 modelMatrix = m_selectedGameObject->getTransformMatrix();

        const auto* window = window::WindowsManager::instance().getCurrentWindow();

        float aspectRatio = static_cast<float>(window->getWidth()) / static_cast<float>(window->getHeight());

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

        ImGuizmo::Manipulate(glm::value_ptr(viewMatrix), glm::value_ptr(projMatrix),
                             op, currentGizmoMode,
                             glm::value_ptr(modelMatrix));

        if (ImGuizmo::IsUsing())
        {
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

}

void Editor::update()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();

    if (!m_gameLibrary)
        m_gameLibrary = ScriptsLoader::instance().loadLibrary("../libTestGame.so");

    updateInput();
    BeginDockSpace();
    // showViewPort();
    showDebugInfo();

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

void Editor::showViewPort()
{
    ImGui::Begin("Scene View", nullptr,
             ImGuiWindowFlags_NoTitleBar |
             ImGuiWindowFlags_NoCollapse |
             ImGuiWindowFlags_NoMove |
             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("MousePos: %.1f, %.1f", io.MousePos.x, io.MousePos.y);
    ImGui::Text("MouseDown: %s", io.MouseDown[0] ? "true" : "false");
    ImGui::Text("Input: %s", io.WantCaptureMouse ? "true" : "false");
    ImGui::Text("ImGuizmo IsOver: %s, IsUsing: %s",
                ImGuizmo::IsOver() ? "true" : "false",
                ImGuizmo::IsUsing() ? "true" : "false");

    const float windowWidth = ImGui::GetContentRegionAvail().x;
    const float windowHeight = ImGui::GetContentRegionAvail().y;
    const ImVec2 cursorPosition = ImGui::GetCursorScreenPos();
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();

    Renderer::instance().rescaleBuffer(contentSize.x, contentSize.y);
    glViewport(0, 0, static_cast<int>(contentSize.x), static_cast<int>(contentSize.y));

    auto fboTexture = Renderer::instance().getFrameBufferTexture();
    ImGui::Image((ImTextureID)(intptr_t)fboTexture, contentSize, ImVec2(0, 1), ImVec2(1, 0));

    ImGui::SetCursorScreenPos(cursorPosition);
    ImGui::InvisibleButton("GizmoInputCatcher", contentSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    ImGui::SetItemAllowOverlap();

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

                // physics::raycasting::Ray ray{};
                // ray.maxDistance = 1000.0f;
                // ray.direction = rayWorld;
                // ray.origin = origin;
                //
                // physics::raycasting::RaycastingResult result;
                //
                // if (physics::raycasting::shoot(ray, result))
                // {
                //     auto* actor = result.hit.block.actor;
                //     auto* gameObject = static_cast<GameObject*>(actor->userData);
                //
                //     if (gameObject)
                //         m_selectedGameObject = gameObject;
                // }
            }
        }
    }

    ImGui::End();

    // if (m_selectedGameObject)
    // {
    //     ImGuizmo::BeginFrame();
    //     ImGuizmo::SetOrthographic(false);
    //     ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    //     ImGuizmo::SetRect(cursorPosition.x, cursorPosition.y, windowWidth, windowHeight);
    //     glm::mat4 modelMatrix = m_selectedGameObject->getTransformMatrix();
    //
    //     float aspectRatio = windowWidth / windowHeight;
    //
    //     glm::mat4 viewMatrix = CameraManager::getInstance().getActiveCamera()->getViewMatrix();
    //     glm::mat4 projMatrix = CameraManager::getInstance().getActiveCamera()->getProjectionMatrix(aspectRatio);
    //
    //     ImGuizmo::OPERATION operation{ImGuizmo::TRANSLATE};
    //
    //     switch (m_transformMode)
    //     {
    //         case TransformMode::Translate: operation = ImGuizmo::TRANSLATE; break;
    //         case TransformMode::Rotate:    operation = ImGuizmo::ROTATE;    break;
    //         case TransformMode::Scale:     operation = ImGuizmo::SCALE;     break;
    //     }
    //
    //     ImGuizmo::Manipulate(
    //         glm::value_ptr(viewMatrix),
    //         glm::value_ptr(projMatrix),
    //         operation,
    //         ImGuizmo::WORLD,
    //         glm::value_ptr(modelMatrix),
    //         nullptr,
    //         nullptr,
    //         nullptr
    //     );
    //
    //     if (ImGuizmo::IsUsing())
    //     {
    //         glm::vec3 translation, rotation, scale;
    //         ImGuizmo::DecomposeMatrixToComponents(
    //             glm::value_ptr(modelMatrix),
    //             glm::value_ptr(translation),
    //             glm::value_ptr(rotation),
    //             glm::value_ptr(scale)
    //         );
    //
    //         m_selectedGameObject->setPosition(translation);
    //         m_selectedGameObject->setRotation(rotation);
    //         m_selectedGameObject->setScale(scale);
    //     }
    // }

}


Editor::~Editor() = default;

void Editor::showAllObjectsInTheScene()
{
    const auto& objects = SceneManager::instance().getCurrentScene()->getGameObjects();

    ImGui::Begin("Scene hierarchy");

    for (const auto& object : objects)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

        if (object.get() == m_selectedGameObject)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool isNodeOpened = ImGui::TreeNodeEx((void*)(intptr_t)object.get(), flags, "%s", object->getName().c_str());

        if (ImGui::IsItemClicked())
            m_selectedGameObject = object.get();

        if (isNodeOpened)
        {
            // for (auto& child : object->children)
            //     ShowGameObjectNode(child);
            ImGui::TreePop();
        }
    }

    ImGui::End();

    ImGui::Begin("Benchmark");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("RAM usage: %s", std::to_string(utilities::getRamUsage()).c_str());
    ImGui::End();
}

void Editor::showAssetsInfo()
{
    static GLitch::Texture folderTexture = AssetsManager::instance().loadTexture(filesystem::getTexturesFolderPath().string() + "/folder.png");
    static GLitch::Texture fileTexture = AssetsManager::instance().loadTexture(filesystem::getTexturesFolderPath().string() + "/file.png");

    if (!folderTexture.isBaked())
        folderTexture.bake();
    if (!fileTexture.isBaked())
        fileTexture.bake();

    static ImTextureID folderIcon = static_cast<ImTextureID>(static_cast<intptr_t>(folderTexture.getId()));
    static ImTextureID fileIcon = static_cast<ImTextureID>(static_cast<intptr_t>(fileTexture.getId()));

    ImGui::Begin("Assets");
    const float iconSize = 64.0f;
    const float padding = 16.0f;

    if (m_assetsPath.has_parent_path() && m_assetsPath != filesystem::getResourcesFolderPath())
        if (ImGui::Button(".."))
            m_assetsPath = m_assetsPath.parent_path();

    float cellSize = iconSize + padding;
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = std::max(1, (int)(panelWidth / cellSize));

    int itemIndex = 0;
    ImGui::Columns(columnCount, nullptr, false);
    const std::vector<std::string> allowedExtensions{".png", ".mat", ".fbx", ".anim", ".obj"};

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
                    auto material = AssetsManager::instance().getMaterialByName(entry.path().filename().string());

                    //TODO: make this better
                    if (material)
                    {
                        m_selectedMaterial = material;
                        m_selectedGameObject = nullptr;
                    }
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

void Editor::showDebugInfo()
{
    showAllObjectsInTheScene();
    showProperties();
    showAssetsInfo();
}

    // ImGui::GetWindowDrawList()->AddImage(
    //     (ImTextureID)(intptr_t)fboTexture,
    //     ImVec2(pos.x, pos.y),
    //     ImVec2(pos.x + windowWidth, pos.y + windowHeight),
    //     ImVec2(0, 1), ImVec2(1, 0)
    // );

    // ImGui::SetCursorScreenPos(cursorPosition);
    // if (ImGui::InvisibleButton("SceneDropArea", contentSize, ImGuiButtonFlags_AllowOverlap))
    // {
    //     std::cout << "Left button pressed" << std::endl;
    // }


    // ImGuizmo::SetOrthographic(false);
    // ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    // ImGuizmo::SetRect(cursorPosition.x, cursorPosition.y, windowWidth, windowHeight);
    // ImGui::SetCursorScreenPos(cursorPosition);
    // ImGuizmo::Enable(true);

    // showGuizmosInfo();

    // if (ImGui::BeginDragDropTarget())
    // {
    //     if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
    //     {
    //         const auto* const info = static_cast<DraggingInfo*>(payload->Data);
    //
    //         if (!info)
    //         {
    //             ImGui::End();
    //             return;
    //         }
    //
    //         std::filesystem::path path(info->name);
    //
    //         //TODO Replace this shit somehow
    //         if (path.extension() == ".fbx" || path.extension() == ".obj")
    //         {
    //             ImVec2 mousePos = ImGui::GetMousePos();
    //
    //             float localX = mousePos.x - cursorPosition.x;
    //             float localY = mousePos.y - cursorPosition.y;
    //
    //             if (localX >= 0 && localY >= 0 && localX < windowWidth && localY < windowHeight)
    //             {
    //                 float x = (2.0f * localX) / windowWidth - 1.0f;
    //                 float y = 1.0f - (2.0f * localY) / windowHeight;
    //
    //                 std::cout << "X: " << x << std::endl;
    //                 std::cout << "Y: " << y << std::endl;
    //
    //                 glm::vec2 mouseNDC(x, y);
    //             }
    //
    //             auto newGameObject = std::make_shared<GameObject>("test");
    //             newGameObject->setPosition({0.0f, 0.0f, 0.0f});
    //             newGameObject->setRotation({0.0f, 0.0f, 0.0f});
    //             newGameObject->setScale({1.0f, 1.0f, 1.0f});
    //             newGameObject->addComponent<RigidbodyComponent>(newGameObject);
    //
    //             if (auto staticModel = AssetsManager::instance().getStaticModelByName(path.filename()))
    //                 newGameObject->addComponent<StaticMeshComponent>(staticModel);
    //             else if (auto skinnedModel = AssetsManager::instance().getSkinnedModelByName(path.filename()))
    //             {
    //                 newGameObject->addComponent<SkeletalMeshComponent>(skinnedModel);
    //                 physics::PhysicsController::instance().resizeCollider({1.0f, 2.0f, 1.0f}, newGameObject);
    //             }
    //
    //             SceneManager::instance().getCurrentScene()->addGameObject(newGameObject);
    //         }
    //
    //         std::cout << "Dropped asset into scene: " << info->name << std::endl;
    //     }
    //     ImGui::EndDragDropTarget();
    // }


void Editor::showMaterialInfo()
{
    if (!m_selectedMaterial)
        return;

    glm::vec3 color = m_selectedMaterial->getBaseColor();

    if (ImGui::ColorEdit3("Base Color##", &color.x))
        m_selectedMaterial->setBaseColor(color);

    ImGui::SeparatorText("Textures");

    using TexType = GLitch::Texture::TextureType;

    for (const auto& textureType : {TexType::Diffuse, TexType::Normal, TexType::Metallic, TexType::Roughness, TexType::AO})
    {
        GLitch::Texture* tex = m_selectedMaterial->getTexture(textureType);
        ImGui::Text("%s: %s", utilities::fromTypeToString(textureType).c_str(), tex ? tex->getName().c_str() : "(none)");
    }
}

void Editor::showObjectInfo()
{
    if (!m_selectedGameObject)
    {
        ImGui::End();
        return;
    }

    static char nameBuffer[128];
    strncpy(nameBuffer, m_selectedGameObject->getName().c_str(), sizeof(nameBuffer));
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

    if (ImGui::InputText("GameObject name", nameBuffer, IM_ARRAYSIZE(nameBuffer)))
        m_selectedGameObject->setName(nameBuffer);

    if (ImGui::BeginTabBar("Tabs"))
    {
        if (ImGui::BeginTabItem("Transform"))
        {
            glm::vec3 position = m_selectedGameObject->getPosition();
            if (ImGui::DragFloat3("Position", &position[0], 0.1f))
                m_selectedGameObject->setPosition(position);

            glm::vec3 rotation = m_selectedGameObject->getRotation();
            if (ImGui::DragFloat3("Rotation", &rotation[0], 1.0f))
                m_selectedGameObject->setRotation(rotation);

            glm::vec3 scale = m_selectedGameObject->getScale();
            if (ImGui::DragFloat3("Scale", &scale[0], 0.1f))
                m_selectedGameObject->setScale(scale);


            common::Model* model{nullptr};

            if (m_selectedGameObject->hasComponent<SkeletalMeshComponent>())
                model = m_selectedGameObject->getComponent<SkeletalMeshComponent>()->getModel();
            else if (m_selectedGameObject->hasComponent<StaticMeshComponent>())
                model = m_selectedGameObject->getComponent<StaticMeshComponent>()->getModel();

            if (model)
            {
                for (unsigned int meshIndex = 0; meshIndex < model->getMeshesSize(); meshIndex++)
                {
                    auto* mesh = model->getMesh(meshIndex);

                    auto* material = mesh->getMaterial();

                    if (!material)
                        continue;

                    const std::string header = "Mesh " + std::to_string(meshIndex);

                    if (ImGui::CollapsingHeader(header.c_str()))
                    {
                        glm::vec3 color = material->getBaseColor();

                        if (ImGui::ColorEdit3(("Base Color##" + std::to_string(meshIndex)).c_str(), &color.x))
                            material->setBaseColor(color);

                        ImGui::SeparatorText("Textures");

                        using TexType = GLitch::Texture::TextureType;

                        for (auto textureType : {TexType::Diffuse, TexType::Normal, TexType::Metallic, TexType::Roughness, TexType::AO})
                        {
                            GLitch::Texture* tex = material->getTexture(textureType);
                            ImGui::Text("%s: %s", utilities::fromTypeToString(textureType).c_str(), tex ? tex->getName().c_str() : "(none)");
                        }

                        const auto& allMaterials = AssetsManager::instance().getAllMaterials(); // returns vector<Material*>
                        std::vector<std::string> materialNames;
                        int currentIndex = -1;

                        for (size_t i = 0; i < allMaterials.size(); ++i)
                        {
                            const auto& name = allMaterials[i]->getName();
                            materialNames.push_back(name);

                            Material* activeMaterial = m_selectedGameObject->overrideMaterials.contains(meshIndex)
                            ? m_selectedGameObject->overrideMaterials[meshIndex]
                            : mesh->getMaterial();

                            if (name == activeMaterial->getName())
                                currentIndex = static_cast<int>(i);
                        }

                        if (ImGui::BeginCombo(("Material##" + std::to_string(meshIndex)).c_str(),
                                              currentIndex >= 0 ? materialNames[currentIndex].c_str() : "(none)"))
                        {
                            for (size_t i = 0; i < materialNames.size(); ++i)
                            {
                                bool isSelected = (currentIndex == static_cast<int>(i));
                                if (ImGui::Selectable(materialNames[i].c_str(), isSelected))
                                {
                                    auto* newMaterial = allMaterials[i];
                                    m_selectedGameObject->overrideMaterials[meshIndex] = newMaterial;
                                    // mesh->setMaterial(newMaterial);
                                }

                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }
                }
            }

            std::vector<std::string> allModelNames;
            std::vector<const char*> convertedModelNames;
            std::string currentModelName;

            if (m_selectedGameObject->hasComponent<SkeletalMeshComponent>())
            {
                currentModelName = m_selectedGameObject->getComponent<SkeletalMeshComponent>()->getModel()->getName();
                allModelNames = AssetsManager::instance().getAllSkinnedModelsNames();
            }
            else if (m_selectedGameObject->hasComponent<StaticMeshComponent>())
            {
                currentModelName = m_selectedGameObject->getComponent<StaticMeshComponent>()->getModel()->getName();
                allModelNames = AssetsManager::instance().getAllStaticModelsNames();
            }

             for (const auto& modelName : allModelNames)
                convertedModelNames.push_back(modelName.c_str());

            ImGui::Text("Model %s", currentModelName.c_str());
            ImGui::SameLine();

            auto i = std::ranges::find_if(convertedModelNames,
        [&currentModelName](const std::string& modelName){return currentModelName == modelName;});

            m_selectedModelIndex = std::distance(convertedModelNames.begin(), i);

            if (ImGui::Combo("##Model combo", &m_selectedModelIndex, convertedModelNames.data(), static_cast<int>(convertedModelNames.size())))
            {
                    if (m_selectedGameObject->hasComponent<SkeletalMeshComponent>())
                        if (auto m = AssetsManager::instance().getSkinnedModelByName(convertedModelNames[m_selectedModelIndex]))
                            m_selectedGameObject->getComponent<SkeletalMeshComponent>()->setModel(m);
                    if (m_selectedGameObject->hasComponent<StaticMeshComponent>())
                        if (auto m = AssetsManager::instance().getStaticModelByName(convertedModelNames[m_selectedModelIndex]))
                            m_selectedGameObject->getComponent<StaticMeshComponent>()->setModel(m);
            }

            ImGui::EndTabItem();
        }

        if (m_selectedGameObject->hasComponent<SkeletalMeshComponent>() && ImGui::BeginTabItem("Bones"))
        {
            displayBonesHierarchy(&m_selectedGameObject->getComponent<SkeletalMeshComponent>()->getModel()->getSkeleton());
            ImGui::EndTabItem();
        }

        if (m_selectedGameObject->hasComponent<SkeletalMeshComponent>() && ImGui::BeginTabItem("Animation"))
        {
            auto component = m_selectedGameObject->getComponent<SkeletalMeshComponent>();

            for (const auto& anim : component->getModel()->getAnimations())
            {
                if (ImGui::Button(anim.name.c_str()))
                    if (auto* animation = component->getModel()->getAnimation(anim.name))
                    {
                        if (!m_selectedGameObject->hasComponent<AnimatorComponent>())
                            m_selectedGameObject->addComponent<AnimatorComponent>();

                        animation->skeletonForAnimation = &component->getModel()->getSkeleton();
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
        showMaterialInfo();

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
