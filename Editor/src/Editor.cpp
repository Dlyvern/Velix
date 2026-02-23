#include "Editor/Editor.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/RigidBodyComponent.hpp"
#include "Engine/Primitives.hpp"

#include "Engine/Primitives.hpp"
#include "Engine/Components/CollisionComponent.hpp"

#include "Engine/PluginSystem/PluginLoader.hpp"

#include "Editor/FileHelper.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cstring>

#include <GLFW/glfw3.h>

#include <fstream>

#include "ImGuizmo.h"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

Editor::Editor()
{
    m_editorCamera = std::make_shared<engine::Camera>();
}

void Editor::drawGuizmo()
{
    if (!m_selectedEntity)
        return;

    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

    ImVec2 viewportPos = ImGui::GetWindowPos();
    ImVec2 viewportSize = ImGui::GetWindowSize();

    ImGuizmo::SetRect(
        viewportPos.x,
        viewportPos.y,
        viewportSize.x,
        viewportSize.y);

    auto model = m_selectedEntity->getComponent<engine::Transform3DComponent>()->getMatrix();

    ImGuizmo::OPERATION operation = ImGuizmo::OPERATION::TRANSLATE;

    switch (m_currentGuizmoOperation)
    {
    case GuizmoOperation::TRANSLATE:
        operation = ImGuizmo::OPERATION::TRANSLATE;
        break;
    case GuizmoOperation::ROTATE:
        operation = ImGuizmo::OPERATION::ROTATE;
        break;
    case GuizmoOperation::SCALE:
        operation = ImGuizmo::OPERATION::SCALE;
        break;
    }

    ImGuizmo::Manipulate(
        glm::value_ptr(m_editorCamera->getViewMatrix()),
        glm::value_ptr(m_editorCamera->getProjectionMatrix()),
        operation,
        ImGuizmo::LOCAL, // or WORLD
        glm::value_ptr(model));

    if (ImGuizmo::IsUsing())
    {
        auto tc = m_selectedEntity->getComponent<engine::Transform3DComponent>();

        glm::vec3 translation, rotation, scale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(model),
            glm::value_ptr(translation),
            glm::value_ptr(rotation),
            glm::value_ptr(scale));

        tc->setPosition(translation);
        tc->setRotation(glm::radians(rotation));
        tc->setScale(scale);
    }
}

void Editor::initStyle()
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = ImGui::GetStyle().Colors;

    style.WindowRounding = 3.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    style.FramePadding = ImVec2(6, 3);
    style.ItemSpacing = ImVec2(8, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.WindowPadding = ImVec2(8, 8);
    style.CellPadding = ImVec2(4, 2);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f;

    ImVec4 darkColor = ImVec4(0.043f, 0.043f, 0.043f, 1.00f);
    ImVec4 mediumColor = ImVec4(0.078f, 0.078f, 0.078f, 1.00f);
    ImVec4 lightColor = ImVec4(0.117f, 0.117f, 0.117f, 1.00f);
    ImVec4 lighterColor = ImVec4(0.156f, 0.156f, 0.156f, 1.00f);

    ImVec4 blueColor = ImVec4(0.031f, 0.529f, 0.902f, 1.00f);
    ImVec4 blueHover = ImVec4(0.149f, 0.631f, 0.949f, 1.00f);
    ImVec4 blueActive = ImVec4(0.000f, 0.447f, 0.741f, 1.00f);
    ImVec4 orangeColor = ImVec4(0.902f, 0.361f, 0.055f, 1.00f);

    ImVec4 text = ImVec4(0.800f, 0.800f, 0.800f, 1.00f);
    ImVec4 textDim = ImVec4(0.600f, 0.600f, 0.600f, 1.00f);
    ImVec4 textHighlight = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = textDim;
    colors[ImGuiCol_WindowBg] = darkColor;
    colors[ImGuiCol_ChildBg] = mediumColor;
    colors[ImGuiCol_PopupBg] = mediumColor;
    colors[ImGuiCol_Border] = ImVec4(0.200f, 0.200f, 0.200f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

    colors[ImGuiCol_FrameBg] = lightColor;
    colors[ImGuiCol_FrameBgHovered] = lighterColor;
    colors[ImGuiCol_FrameBgActive] = blueActive;

    colors[ImGuiCol_TitleBg] = darkColor;
    colors[ImGuiCol_TitleBgActive] = mediumColor;
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.043f, 0.043f, 0.043f, 0.70f);

    colors[ImGuiCol_MenuBarBg] = mediumColor;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.043f, 0.043f, 0.043f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = lighterColor;
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.250f, 0.250f, 0.250f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = blueColor;

    colors[ImGuiCol_CheckMark] = blueColor;
    colors[ImGuiCol_SliderGrab] = blueColor;
    colors[ImGuiCol_SliderGrabActive] = blueHover;

    colors[ImGuiCol_Button] = lightColor;
    colors[ImGuiCol_ButtonHovered] = lighterColor;
    colors[ImGuiCol_ButtonActive] = blueColor;

    colors[ImGuiCol_Header] = lightColor;
    colors[ImGuiCol_HeaderHovered] = lighterColor;
    colors[ImGuiCol_HeaderActive] = blueColor;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.200f, 0.200f, 0.200f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered] = blueColor;
    colors[ImGuiCol_ResizeGripActive] = blueHover;

    colors[ImGuiCol_Tab] = mediumColor;
    colors[ImGuiCol_TabHovered] = blueColor;
    colors[ImGuiCol_TabActive] = lightColor;
    colors[ImGuiCol_TabUnfocused] = mediumColor;
    colors[ImGuiCol_TabUnfocusedActive] = lightColor;

    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.031f, 0.529f, 0.902f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = orangeColor;
    colors[ImGuiCol_NavHighlight] = blueColor;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.000f, 1.000f, 1.000f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.043f, 0.043f, 0.043f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.043f, 0.043f, 0.043f, 0.75f);

    colors[ImGuiCol_Separator] = ImVec4(0.200f, 0.200f, 0.200f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = blueColor;
    colors[ImGuiCol_SeparatorActive] = blueHover;

    colors[ImGuiCol_TableHeaderBg] = lightColor;
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.150f, 0.150f, 0.150f, 0.50f);
    colors[ImGuiCol_TableRowBg] = mediumColor;
    colors[ImGuiCol_TableRowBgAlt] = lightColor;

    colors[ImGuiCol_PlotLines] = blueColor;
    colors[ImGuiCol_PlotLinesHovered] = blueHover;
    colors[ImGuiCol_PlotHistogram] = blueColor;
    colors[ImGuiCol_PlotHistogramHovered] = blueHover;

    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("./resources/fonts/JetBrainsMono-Regular.ttf", 16.0f);

    m_resourceStorage.loadNeededResources();

    m_assetsWindow = std::make_shared<AssetsWindow>(&m_resourceStorage, m_requestedMaterialPreviewJobs);

    m_entityIdBuffer = core::Buffer::createShared(sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                  core::memory::MemoryUsage::CPU_TO_GPU);

    auto window = core::VulkanContext::getContext()->getSwapchain()->getWindow();
    GLFWwindow *windowHandler = window->getRawHandler();

    // glfwSetWindowAttrib(windowHandler, GLFW_DECORATED, !m_isDockingWindowFullscreen);
    if (m_isDockingWindowFullscreen)
    {
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        glfwSetWindowPos(windowHandler, 0, 0);
        glfwSetWindowSize(windowHandler, mode->width, mode->height);
    }

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(core::VulkanContext::getContext()->getDevice(), &samplerInfo, nullptr, &m_defaultSampler) != VK_SUCCESS)
        throw std::runtime_error("failed to create texture sampler!");

    m_textEditor.SetPalette(TextEditor::GetDarkPalette());
    m_textEditor.SetShowWhitespaces(false);
}

void Editor::addOnModeChangedCallback(const std::function<void(EditorMode)> &function)
{
    m_onModeChangedCallbacks.push_back(function);
}

void Editor::showDockSpace()
{
    static bool dockspaceOpen = true;
    constexpr static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;

    if (m_isDockingWindowFullscreen)
    {
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }

    ImGui::Begin("Velix dockSpace", &dockspaceOpen, windowFlags);

    if (m_isDockingWindowFullscreen)
        ImGui::PopStyleVar(2);

    ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

    ImGui::End();

    static bool first_time = true;

    if (first_time)
    {
        first_time = false;

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

        ImGuiID dockMainId = dockspaceId;

        ImGuiID titleBarDock = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Up, 0.04f, nullptr, &dockMainId);
        ImGui::DockBuilderDockWindow("TitleBar", titleBarDock);

        ImGuiID toolbarDock = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Up, 0.03f, nullptr, &dockMainId);
        ImGui::DockBuilderDockWindow("Toolbar", toolbarDock);

        ImGuiID dockIdRight = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.18f, nullptr, &dockMainId);

        ImGuiID dockIdRightTop = dockIdRight;
        ImGuiID dockIdRightBottom = ImGui::DockBuilderSplitNode(dockIdRightTop, ImGuiDir_Down, 0.5f, nullptr, &dockIdRightTop);

        ImGuiID dockIdBottomPanel = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.08f, nullptr, &dockMainId);

        ImGui::DockBuilderDockWindow("BottomPanel", dockIdBottomPanel);
        ImGui::DockBuilderDockWindow("Hierarchy", dockIdRightTop);
        ImGui::DockBuilderDockWindow("Details", dockIdRightBottom);
        ImGui::DockBuilderDockWindow("Viewport", dockMainId);
        m_centerDockId = dockMainId;

        ImGui::DockBuilderFinish(dockspaceId);
    }
}

void Editor::drawCustomTitleBar()
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::Begin("TitleBar", nullptr, windowFlags);

    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle &style = ImGui::GetStyle();

    ImGui::SetCursorPos(ImVec2(4, (ImGui::GetWindowHeight() - 30) * 0.5f));
    ImVec2 logoSize = ImVec2(50, 30);
    ImGui::Image(m_resourceStorage.getTextureDescriptorSet("./resources/textures/VelixFire.png"), logoSize);

    ImGui::SameLine(0, 10);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    if (ImGui::Button("File"))
    {
        if (ImGui::IsPopupOpen("FilePopup"))
            ImGui::CloseCurrentPopup();
        else
            ImGui::OpenPopup("FilePopup");
    }

    ImGui::SameLine(0, 10);

    if (ImGui::Button("Tools"))
    {
        if (ImGui::IsPopupOpen("CreateNewClassPopup"))
            ImGui::CloseCurrentPopup();
        else
            ImGui::OpenPopup("CreateNewClassPopup");
    }

    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar();

    // ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui:s:GetItemRectMin().y + ImGui::GetItemRectSize().y));

    if (ImGui::BeginPopup("CreateNewClassPopup"))
    {
        // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        if (ImGui::Button("Create new C++ class"))
        {
            if (ImGui::IsPopupOpen("CreateNewClass"))
                ImGui::CloseCurrentPopup();
            else
                ImGui::OpenPopup("CreateNewClass");
        }

        if (ImGui::BeginPopup("CreateNewClass"))
        {
            static char newClassName[256];

            ImGui::InputTextWithHint("##CreateClass", "New c++ class name...", newClassName, sizeof(newClassName));
            ImGui::Separator();

            if (ImGui::Button("Create"))
            {
                std::ifstream hppFileTemplate("./resources/scripts_template/ScriptTemplate.hpp.txt");
                std::ifstream cppFileTemplate("./resources/scripts_template/ScriptTemplate.cpp.txt");

                std::string hppString(std::istreambuf_iterator<char>{hppFileTemplate}, {});
                std::string cppString(std::istreambuf_iterator<char>{cppFileTemplate}, {});

                hppFileTemplate.close();
                cppFileTemplate.close();

                std::size_t pos = 0;

                std::string token = "ClassName";
                std::string className(newClassName);

                while ((pos = hppString.find(token, pos)) != std::string::npos)
                {
                    hppString.replace(pos, token.length(), className);
                    pos += className.length();
                }

                pos = 0;

                while ((pos = cppString.find(token, pos)) != std::string::npos)
                {
                    cppString.replace(pos, token.length(), className);
                    pos += className.length();
                }

                std::string sourceFolder = m_currentProject.lock()->sourcesDir;

                if (sourceFolder.back() != '/')
                    sourceFolder += '/';

                std::ofstream hppCreateFile(sourceFolder + className + ".hpp");
                std::ofstream cppCreateFile(sourceFolder + className + ".cpp");

                hppCreateFile << hppString << std::endl;
                cppCreateFile << cppString << std::endl;

                hppCreateFile.close();
                cppCreateFile.close();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            // ImGui::PopStyleColor(1);
            ImGui::EndPopup();
        }

        if (ImGui::Button("Build Project"))
        {
            // if(m_currentProject.directory.empty())
            // {
            //     std::cerr << "Project directory is empty" << std::endl;
            //     ImGui::End();
            //     return;
            // }

            // //!It won't work on machine wihtout cmake, we need to provide a compiler along with the engine
            // std::string cmakeBuildCommand = "cmake --build " + m_currentProject.directory + "/build" + " --config Release";

            // std::string cmakeCommand =
            // "cmake -S " + m_currentProject.directory + "/build" +
            // " -B " + m_currentProject.directory + "/build" +
            // " -DCMAKE_PREFIX_PATH=" + FileHelper::getExecutablePath().string();

            // std::cout << cmakeCommand << std::endl;

            // auto cmakeResult = FileHelper::executeCommand(cmakeCommand);

            // std::cout << cmakeResult.second << std::endl;

            // if(cmakeResult.first != 0)
            // {
            //     ImGui::End();
            //     return;
            // }

            // auto cmakeBuildResult = FileHelper::executeCommand(cmakeBuildCommand);

            // std::cerr << cmakeBuildResult.second << std::endl;

            // if(cmakeBuildResult.first != 0)
            // {
            //     std::cerr << "Failed to build project" << std::endl;
            //     // std::cerr << cmakeBuildResult.second << std::endl;
            //     ImGui::End();
            //     return;
            // }

            // std::cout << "Successfully built project" << std::endl;

            // std::string extension = SHARED_LIB_EXTENSION;

            // engine::LibraryHandle library = engine::PluginLoader::loadLibrary(m_currentProject.directory + "/build/" + "libGameModule" + extension);

            // if(!library)
            // {
            //     std::cerr << "Failed to get a library" << std::endl;
            //     ImGui::End();
            //     return;
            // }

            // auto function = engine::PluginLoader::getFunction<engine::ScriptsRegister&(*)()>("getScriptsRegister", library);

            // if(function)
            // {
            //     engine::ScriptsRegister& scriptsRegister = function();

            //     if(scriptsRegister.getScripts().empty())
            //         std::cerr << "Sripts are empty" << std::endl;

            //     for(const auto& scriptRegister : scriptsRegister.getScripts())
            //     {
            //         auto script = scriptsRegister.createScript(scriptRegister.first);

            //         if(!script)
            //         {
            //             std::cerr << "Failed to get script" << std::endl;
            //             continue;
            //         }

            //         script->onStart();
            //         script->onUpdate(0.0f);
            //     }
            // }
            // else
            //     std::cerr << "Failed to get hello function" << std::endl;

            // engine::PluginLoader::closeLibrary(library);
        }

        ImGui::EndPopup();

        // ImGui::PopStyleColor(1);
    }

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y + ImGui::GetItemRectSize().y));
    auto window = core::VulkanContext::getContext()->getSwapchain()->getWindow();
    GLFWwindow *windowHandler = window->getRawHandler();

    if (ImGui::BeginPopup("FilePopup"))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        if (ImGui::Button("New scene"))
        {
            if (ImGui::IsPopupOpen("CreateNewScene"))
                ImGui::CloseCurrentPopup();
            else
                ImGui::OpenPopup("CreateNewScene");
        }

        if (ImGui::BeginPopup("CreateNewScene"))
        {
            // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            static char sceneBuffer[256];
            ImGui::InputTextWithHint("##NewScene", "New scene name...", sceneBuffer, sizeof(sceneBuffer));
            ImGui::Separator();

            ImGui::Button("Create");

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            // ImGui::PopStyleColor(1);
            ImGui::EndPopup();
        }

        if (ImGui::Button("Open scene"))
        {
            // TODO open my own file editor
        }
        if (ImGui::Button("Save"))
        {
            if (m_scene && m_currentProject.lock())
                m_scene->saveSceneToFile(m_currentProject.lock()->entryScene);
        }

        ImGui::Separator();
        if (ImGui::Button("Exit"))
            window->close(); // Ha-ha-ha-ha Kill it slower dumbass
        ImGui::PopStyleColor(1);

        ImGui::EndPopup();
    }

    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    if (ImGui::Button("Edit"))
    {
        if (ImGui::IsPopupOpen("EditPopup"))
            ImGui::CloseCurrentPopup();
        else
            ImGui::OpenPopup("EditPopup");
    }

    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar();

    if (ImGui::BeginPopup("EditPopup"))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        ImGui::Button("Undo Ctrl+Z");
        ImGui::Button("Redo Ctrl+Y");

        ImGui::PopStyleColor(1);
        ImGui::EndPopup();
    }

    float windowWidth = ImGui::GetWindowWidth();
    float buttonSize = ImGui::GetFrameHeight();
    ImGui::SameLine(windowWidth - buttonSize * 3 - 30);

    if (ImGui::Button("_", ImVec2(buttonSize, buttonSize * 0.9f)))
        window->iconify();

    ImGui::SameLine();

    if (ImGui::Button("[]", ImVec2(buttonSize, buttonSize * 0.9f)))
        m_isDockingWindowFullscreen = !m_isDockingWindowFullscreen;

    ImGui::SameLine();

    if (ImGui::Button("X", ImVec2(buttonSize, buttonSize * 0.9f)))
        window->close();

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::End();

    ImGui::PopStyleVar(2);
}

void Editor::changeMode(EditorMode mode)
{
    m_currentMode = mode;

    for (const auto &callback : m_onModeChangedCallbacks)
        if (callback)
            callback(mode);
}

void Editor::drawToolBar()
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    static bool showBenchmark = false;

    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&windowClass);

    ImGui::Begin("Toolbar", nullptr, windowFlags);

    if (ImGui::BeginMenuBar())
    {
        // TODO make it better
        const std::vector<std::string> selectionModes = {"Translate", "Rotate", "Scale"};
        static int guizmoMode = 0;

        switch (m_currentGuizmoOperation)
        {
        case GuizmoOperation::TRANSLATE:
            guizmoMode = 0;
            break;
        case GuizmoOperation::ROTATE:
            guizmoMode = 1;
            break;
        case GuizmoOperation::SCALE:
            guizmoMode = 2;
            break;
        }

        ImGui::PushItemWidth(120.0f);
        // ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(50, 200));
        if (ImGui::BeginCombo("##Selection mode", selectionModes[guizmoMode].c_str()))
        {
            for (int i = 0; i < selectionModes.size(); ++i)
            {
                const bool isSelected = (guizmoMode == i);

                if (ImGui::Selectable(selectionModes[i].c_str(), isSelected))
                {
                    guizmoMode = i;

                    if (guizmoMode == 0)
                        m_currentGuizmoOperation = GuizmoOperation::TRANSLATE;
                    else if (guizmoMode == 1)
                        m_currentGuizmoOperation = GuizmoOperation::ROTATE;
                    else if (guizmoMode == 2)
                        m_currentGuizmoOperation = GuizmoOperation::SCALE;
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        // ImGui::SameLine(200);
        ImGui::SameLine();

        std::string playText;

        if (m_currentMode == EditorMode::PAUSE || m_currentMode == EditorMode::EDIT)
            playText = "Play";

        else if (m_currentMode == EditorMode::PLAY)
            playText = "Pause";

        if (ImGui::Button(playText.c_str()))
        {
            if (m_currentMode == Editor::EDIT || m_currentMode == EditorMode::PAUSE)
            {
                changeMode(EditorMode::PLAY);
            }
            else if (m_currentMode == EditorMode::PLAY)
            {
                changeMode(EditorMode::PAUSE);
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Stop"))
        {
            changeMode(EditorMode::EDIT);
        }

        if (ImGui::Button("Benchmark"))
        {
            if (ImGui::IsPopupOpen("BenchmarkPopup"))
                ImGui::CloseCurrentPopup();
            else
                ImGui::OpenPopup("BenchmarkPopup");
        }

        ImVec2 buttonPos = ImGui::GetItemRectMin();
        ImVec2 buttonSize = ImGui::GetItemRectSize();
        ImGui::SetNextWindowPos(ImVec2(buttonPos.x, buttonPos.y + buttonSize.y));

        if (ImGui::BeginPopup("BenchmarkPopup"))
        {
            float fps = ImGui::GetIO().Framerate;
            ImGui::Text("FPS: %.1f", fps);
            ImGui::Text("Frame time: %.3f ms", 1000.0f / fps);
            ImGui::Text("VRAM usage: %ld mB", core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM());
            ImGui::Text("RAM usage: %ld mB", core::VulkanContext::getContext()->getDevice()->getTotalUsedRAM());

            ImGui::EndPopup();
        }

        ImGui::EndMenuBar();
    }

    ImGui::End();
}

void Editor::drawBottomPanel()
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoTitleBar;

    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&windowClass);

    ImGui::Begin("BottomPanel", nullptr, flags);

    if (ImGui::Button("Assets"))
        m_showAssetsWindow = !m_showAssetsWindow;

    ImGui::SameLine();

    if (ImGui::Button("Terminal with logs"))
        m_showTerminal = !m_showTerminal;

    ImGui::End();
}

void Editor::drawFrame(VkDescriptorSet viewportDescriptorSet)
{
    handleInput();

    showDockSpace();

    drawCustomTitleBar();
    drawToolBar();

    if (viewportDescriptorSet)
        drawViewport(viewportDescriptorSet);
    drawDocument();
    drawAssets();
    drawBottomPanel();
    drawHierarchy();
    drawDetails();
}

std::vector<engine::AdditionalPerFrameData> Editor::getRenderData()
{
    if (!m_selectedEntity)
        return {};

    static engine::Entity *prevEntity{nullptr};

    engine::AdditionalPerFrameData data;

    engine::DrawItem entity;
    entity.transform = m_selectedEntity->getComponent<engine::Transform3DComponent>()->getMatrix();
    entity.transform = glm::scale(entity.transform, glm::vec3(1.05f));
    entity.graphicsPipelineKey.shader = engine::ShaderId::Wireframe;

    entity.graphicsPipelineKey.cull = engine::CullMode::None;
    entity.graphicsPipelineKey.polygonMode = VK_POLYGON_MODE_LINE;
    entity.graphicsPipelineKey.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    // entity.graphicsPipelineKey.polygonMode = VK_POLYGON_MODE_FILL;
    // entity.graphicsPipelineKey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // entity.graphicsPipelineKey.depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    // entity.graphicsPipelineKey.depthTest = false;
    // entity.graphicsPipelineKey.depthWrite = false;

    if (prevEntity == m_selectedEntity)
    {
        entity.meshes.push_back(m_selectedObjectMesh);

        data.drawItems.push_back(entity);
    }
    else
    {
        prevEntity = m_selectedEntity;

        std::vector<elix::engine::CPUMesh> meshes;
        if (auto c = m_selectedEntity->getComponent<engine::StaticMeshComponent>())
            meshes = c->getMeshes();
        else if (auto c = m_selectedEntity->getComponent<engine::SkeletalMeshComponent>())
            meshes = c->getMeshes();

        // TODO FIX THIS SHIT
        for (const auto &m : meshes)
            m_selectedObjectMesh = engine::GPUMesh::createFromMesh(m);

        entity.meshes.push_back(m_selectedObjectMesh);

        data.drawItems.push_back(entity);
    }

    return {data};
}

void Editor::drawDocument()
{
    std::string windowName = "./imgui.ini";

    if (m_centerDockId != 0)
        ImGui::SetNextWindowDockID(m_centerDockId, ImGuiCond_Always);

    if (!ImGui::Begin(windowName.c_str()))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Save"))
    {
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(true ? "*" : "");

    m_textEditor.Render("TextEditor");

    ImGui::End();
}

void Editor::setSelectedEntity(engine::Entity *entity)
{
    m_selectedEntity = entity;
}

void Editor::handleInput()
{
    ImGuiIO &io = ImGui::GetIO();

    if (!io.WantCaptureKeyboard)
    {
    }

    const bool isCtrlDown = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);

    if (isCtrlDown && ImGui::IsKeyPressed(ImGuiKey_C, false))
    {
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_selectedEntity)
    {
        m_scene->destroyEntity(m_selectedEntity);
        setSelectedEntity(nullptr);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && m_currentMode != EditorMode::EDIT)
        changeMode(EditorMode::EDIT);

    if (isCtrlDown && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        if (m_currentProject.lock())
            m_scene->saveSceneToFile(m_currentProject.lock()->entryScene);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_W))
    {
        m_currentGuizmoOperation = GuizmoOperation::TRANSLATE;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_E))
    {
        m_currentGuizmoOperation = GuizmoOperation::ROTATE;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_R))
    {
        m_currentGuizmoOperation = GuizmoOperation::SCALE;
    }
}

void Editor::drawDetails()
{
    ImGui::Begin("Details");

    if (!m_selectedEntity)
        return ImGui::End();

    char buffer[128];
    std::strncpy(buffer, m_selectedEntity->getName().c_str(), sizeof(buffer));
    if (ImGui::InputText("##Name", buffer, sizeof(buffer)))
        m_selectedEntity->setName(std::string(buffer));

    ImGui::SameLine();

    if (ImGui::Button("Add component"))
    {
        if (ImGui::IsPopupOpen("AddComponentPopup"))
            ImGui::CloseCurrentPopup();
        else
            ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup"))
    {
        ImGui::Text("Scripting");

        ImGui::Button("New C++ class");

        ImGui::Separator();

        ImGui::Text("Common");

        if (ImGui::Button("Camera"))
        {
            m_selectedEntity->addComponent<engine::CameraComponent>();
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("RigidBody"))
        {
            auto transformation = m_selectedEntity->getComponent<engine::Transform3DComponent>();
            auto position = transformation->getPosition();
            physx::PxTransform transform(physx::PxVec3(position.x, position.y, position.z));
            auto rigid = m_scene->getPhysicsScene().createDynamic(transform);
            auto rigidComponent = m_selectedEntity->addComponent<engine::RigidBodyComponent>(rigid);

            if (auto collisionComponent = m_selectedEntity->getComponent<engine::CollisionComponent>())
            {
                if (collisionComponent->getActor())
                {
                    m_scene->getPhysicsScene().removeActor(*collisionComponent->getActor(), true, true);
                    collisionComponent->removeActor();
                }

                rigidComponent->getRigidActor()->attachShape(*collisionComponent->getShape());

                physx::PxRigidBodyExt::updateMassAndInertia(*rigid, 10.0f);
            }
        }

        if (ImGui::Button("Collision"))
        {
            auto transformation = m_selectedEntity->getComponent<engine::Transform3DComponent>();
            auto position = transformation->getPosition();
            auto shape = m_scene->getPhysicsScene().createShape(physx::PxBoxGeometry(transformation->getScale().x * 0.5f,
                                                                                     transformation->getScale().y * 0.5f, transformation->getScale().z * 0.5f));

            physx::PxTransform transform(physx::PxVec3(position.x, position.y, position.z));

            if (auto rigidComponent = m_selectedEntity->getComponent<engine::RigidBodyComponent>())
            {
                rigidComponent->getRigidActor()->attachShape(*shape);
                auto collisionComponent = m_selectedEntity->addComponent<engine::CollisionComponent>(shape);
            }
            else
            {
                auto staticActor = m_scene->getPhysicsScene().createStatic(transform);
                staticActor->attachShape(*shape);
                auto collisionComponent = m_selectedEntity->addComponent<engine::CollisionComponent>(shape, staticActor);
            }
        }

        ImGui::Button("Audio");

        ImGui::Button("Light");

        ImGui::Separator();

        ImGui::EndPopup();
    }

    for (const auto &[_, component] : m_selectedEntity->getSingleComponents())
    {
        if (auto transformComponent = dynamic_cast<engine::Transform3DComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginTable("TransformTable", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Position");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushID("Position");
                    auto position = transformComponent->getPosition();

                    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(100, 100, 100, 255));
                    if (ImGui::Button("R"))
                        position = glm::vec3(0.0f);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();

                    ImGui::DragFloat3("##Position", &position.x, 0.01f);

                    transformComponent->setPosition(position);

                    // X/Y/Z colored drag
                    // float* values[3] = { &pos.x, &pos.y, &pos.z };
                    // ImVec4 colors[3] = { ImVec4(0.8f,0.2f,0.2f,1.0f), ImVec4(0.2f,0.8f,0.2f,1.0f), ImVec4(0.2f,0.2f,0.8f,1.0f) };
                    // ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4,2));
                    // for (int i = 0; i < 3; i++)
                    // {
                    //     ImGui::PushStyleColor(ImGuiCol_Text, colors[i]);
                    //     ImGui::DragFloat(i==0 ? "##X" : (i==1?"##Y":"##Z"), values[i], 0.1f);
                    //     ImGui::PopStyleColor();
                    //     if(i<2) ImGui::SameLine();
                    // }
                    // ImGui::PopStyleVar();
                    // transformComponent->setPosition(pos);
                    ImGui::PopID();

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Rotation");
                    ImGui::TableSetColumnIndex(1);
                    auto euler = transformComponent->getEulerDegrees();
                    if (ImGui::DragFloat3("##Rotation", &euler.x, 0.1f))
                        transformComponent->setEulerDegrees(euler);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Scale");
                    ImGui::TableSetColumnIndex(1);
                    auto scale = transformComponent->getScale();
                    if (ImGui::DragFloat3("##Scale", &scale.x, 0.01f, 0.0f, 100.0f))
                        transformComponent->setScale(scale);

                    ImGui::EndTable();
                }
            }
        }
        else if (auto lightComponent = dynamic_cast<engine::LightComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto lightType = lightComponent->getLightType();
                auto light = lightComponent->getLight();

                static const std::vector<const char *> lightTypes{
                    "Directional",
                    "Spot",
                    "Point"};

                static int currentLighType = 0;

                switch (lightType)
                {
                case engine::LightComponent::LightType::DIRECTIONAL:
                    currentLighType = 0;
                    break;
                case engine::LightComponent::LightType::SPOT:
                    currentLighType = 1;
                    break;
                case engine::LightComponent::LightType::POINT:
                    currentLighType = 2;
                    break;
                };

                if (ImGui::Combo("Light type", &currentLighType, lightTypes.data(), lightTypes.size()))
                {
                    if (currentLighType == 0)
                        lightComponent->changeLightType(engine::LightComponent::LightType::DIRECTIONAL);
                    else if (currentLighType == 1)
                        lightComponent->changeLightType(engine::LightComponent::LightType::SPOT);
                    else if (currentLighType == 2)
                        lightComponent->changeLightType(engine::LightComponent::LightType::POINT);

                    lightType = lightComponent->getLightType();
                    light = lightComponent->getLight();
                }

                ImGui::DragFloat3("Light position", &light->position.x, 0.1f, 0.0f);
                ImGui::ColorEdit3("Light color", &light->color.x);
                ImGui::DragFloat("Light strength", &light->strength, 0.1f, 0.0f, 150.0f);

                if (lightType == engine::LightComponent::LightType::POINT)
                {
                    auto pointLight = dynamic_cast<engine::PointLight *>(light.get());
                    ImGui::DragFloat("Light radius", &pointLight->radius, 0.1, 0.0f, 360.0f);
                }
                else if (lightType == engine::LightComponent::LightType::DIRECTIONAL)
                {
                    auto directionalLight = dynamic_cast<engine::DirectionalLight *>(light.get());
                    ImGui::DragFloat3("Light direction", &directionalLight->direction.x, 0.01);
                }
                else if (lightType == engine::LightComponent::LightType::SPOT)
                {
                    auto spotLight = dynamic_cast<engine::SpotLight *>(light.get());
                    ImGui::DragFloat3("Light direction", &spotLight->direction.x);
                    ImGui::DragFloat("Inner", &spotLight->innerAngle);
                    ImGui::DragFloat("Outer", &spotLight->outerAngle);
                }
            }
        }
        else if (auto staticComponent = dynamic_cast<engine::StaticMeshComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Static mesh", ImGuiTreeNodeFlags_DefaultOpen))
            {
            }
        }
        else if (auto skeletalMeshComponent = dynamic_cast<engine::SkeletalMeshComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Skeletal mesh", ImGuiTreeNodeFlags_DefaultOpen))
            {
            }
        }
        else if (auto cameraComponent = dynamic_cast<engine::CameraComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto camera = cameraComponent->getCamera();
                float yaw = camera->getYaw();
                float pitch = camera->getPitch();
                glm::vec3 position = camera->getPosition();
                float fov = camera->getFOV();

                if (ImGui::DragFloat("Yaw", &yaw))
                    camera->setYaw(yaw);

                if (ImGui::DragFloat("Pitch", &pitch))
                    camera->setPitch(pitch);

                if (ImGui::DragFloat("FOV", &fov))
                    camera->setFOV(fov);

                if (ImGui::DragFloat3("Camera position", &position.x, 0.1f, 0.0f))
                    camera->setPosition(position);
            }
        }
        else if (auto rigidBodyComponent = dynamic_cast<engine::RigidBodyComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("RigidBody", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool isKinematic = true;

                if (ImGui::Checkbox("Kinematic", &isKinematic))
                {
                    rigidBodyComponent->setKinematic(isKinematic);
                }
            }
        }
        else if (auto collisionComponent = dynamic_cast<engine::CollisionComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen))
            {
            }
        }
    }

    ImGui::End();
}

engine::Camera::SharedPtr Editor::getCurrentCamera()
{
    return m_editorCamera;
}

void Editor::addOnViewportChangedCallback(const std::function<void(uint32_t width, uint32_t height)> &function)
{
    m_onViewportWindowResized.push_back(function);
}

void Editor::drawViewport(VkDescriptorSet viewportDescriptorSet)
{
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_None);

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    ImGui::Image(viewportDescriptorSet, ImVec2(viewportPanelSize.x, viewportPanelSize.y));

    drawGuizmo();

    uint32_t x = static_cast<uint32_t>(viewportPanelSize.x);
    uint32_t y = static_cast<uint32_t>(viewportPanelSize.y);

    bool sizeChanged = (m_viewportSizeX != x || m_viewportSizeY != y);

    if (sizeChanged)
    {
        m_viewportSizeX = x;
        m_viewportSizeY = y;

        for (const auto &function : m_onViewportWindowResized)
            if (function)
                function(m_viewportSizeX, m_viewportSizeY);
    }

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGuiIO &io = ImGui::GetIO();

    // if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_objectIdColorImage)
    // {
    //     ImVec2 mouse = ImGui::GetMousePos();

    //     ImVec2 vpMin = ImGui::GetItemRectMin();
    //     ImVec2 vpMax = ImGui::GetItemRectMax();
    //     ImVec2 vpSize = ImVec2(vpMax.x - vpMin.x, vpMax.y - vpMin.y);

    //     float u = (mouse.x - vpMin.x) / vpSize.x;
    //     float v = (mouse.y - vpMin.y) / vpSize.y;

    //     uint32_t x = uint32_t(u * m_viewportSizeX);
    //     uint32_t y = uint32_t(v * m_viewportSizeY);

    //     std::cout << "First\n";
    //     m_objectIdColorImage->getImage()->insertImageMemoryBarrier(
    //         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    //         VK_ACCESS_TRANSFER_READ_BIT,
    //         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    //         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    //         VK_PIPELINE_STAGE_TRANSFER_BIT,
    //         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});

    //     vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());

    //     m_entityIdBuffer->copyImageToBuffer(m_objectIdColorImage->getImage()->vk(), {int32_t(x), int32_t(y), 0});

    //     vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());

    //     std::cout << "Second\n";
    //     m_objectIdColorImage->getImage()->insertImageMemoryBarrier(
    //         VK_ACCESS_TRANSFER_READ_BIT,
    //         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    //         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    //         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //         VK_PIPELINE_STAGE_TRANSFER_BIT,
    //         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    //         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});

    //     vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());

    //     uint32_t *data;
    //     m_entityIdBuffer->map(reinterpret_cast<void *&>(data));

    //     uint32_t pickedId = data[0];

    //     m_selectedEntity = m_scene->getEntityById(pickedId);

    //     std::cout << pickedId << '\n';

    //     m_entityIdBuffer->unmap();
    // }

    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);

        glm::vec2 mouseDelta(io.MouseDelta.x, io.MouseDelta.y);

        float yaw = m_editorCamera->getYaw();
        float pitch = m_editorCamera->getPitch();

        yaw += mouseDelta.x * m_mouseSensitivity;
        pitch -= mouseDelta.y * m_mouseSensitivity;

        m_editorCamera->setYaw(yaw);
        m_editorCamera->setPitch(pitch);

        float velocity = m_movementSpeed * io.DeltaTime;
        glm::vec3 position = m_editorCamera->getPosition();

        const glm::vec3 forward = m_editorCamera->getForward();
        const glm::vec3 right = glm::normalize(glm::cross(forward, m_editorCamera->getUp()));

        if (ImGui::IsKeyDown(ImGuiKey_W))
            position += forward * velocity;
        if (ImGui::IsKeyDown(ImGuiKey_S))
            position -= forward * velocity;
        if (ImGui::IsKeyDown(ImGuiKey_A))
            position -= right * velocity;
        if (ImGui::IsKeyDown(ImGuiKey_D))
            position += right * velocity;
        if (ImGui::IsKeyDown(ImGuiKey_E))
            position += m_editorCamera->getUp() * velocity;
        if (ImGui::IsKeyDown(ImGuiKey_Q))
            position -= m_editorCamera->getUp() * velocity;

        m_editorCamera->setPosition(position);
        m_editorCamera->updateCameraVectors();

        io.WantCaptureMouse = true;
        io.WantCaptureKeyboard = true;
    }

    ImGui::End();
}

void Editor::drawAssets()
{
    if (!m_showAssetsWindow || !m_assetsWindow)
        return;

    ImGui::Begin("Assets");

    if (!m_currentProject.lock())
    {
        ImGui::End();
        return;
    }

    m_assetsWindow->draw();

    ImGui::End();
}

void Editor::drawHierarchy()
{
    ImGui::Begin("Hierarchy");

    if (!m_scene)
        return ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

    for (auto &entity : m_scene->getEntities())
    {
        // ImGui::PushID(entity->getID());

        auto entityName = entity->getName().c_str();

        bool selected = (entity.get() == m_selectedEntity);

        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;

        if (selected)
            nodeFlags |= ImGuiTreeNodeFlags_Selected;

        bool nodeOpen = ImGui::TreeNodeEx(entityName, nodeFlags);

        if (ImGui::IsItemClicked())
            setSelectedEntity(entity.get());

        if (nodeOpen)
            ImGui::TreePop();

        // ImGui::PopID();
    }

    ImGui::PopStyleVar(2);

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGuiIO &io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("HierarchyPopup");
    }

    if (ImGui::BeginPopup("HierarchyPopup"))
    {
        if (ImGui::Button("Add entity"))
        {
            ImGui::OpenPopup("EntityAddingPopup");
        }

        if (ImGui::BeginPopup("EntityAddingPopup"))
        {
            if (ImGui::Button("Cube"))
            {
                auto entity = m_scene->addEntity("New entity");
                engine::CPUMesh mesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(engine::cube::vertices, engine::cube::indices);
                entity->addComponent<engine::StaticMeshComponent>(std::vector<engine::CPUMesh>{mesh});
            }
            if (ImGui::Button("Sphere"))
            {
                float radius = 1.0f;
                int sectorCount = 32;
                int stackCount = 16;
                std::vector<engine::vertex::Vertex3D> vertices;
                std::vector<uint32_t> indices;

                float sectorStep = 2 * M_PI / sectorCount;
                float stackStep = M_PI / stackCount;

                for (int i = 0; i <= stackCount; ++i)
                {
                    float stackAngle = M_PI / 2 - i * stackStep;
                    float xy = radius * cosf(stackAngle);
                    float z = radius * sinf(stackAngle);

                    for (int j = 0; j <= sectorCount; ++j)
                    {
                        float sectorAngle = j * sectorStep;

                        engine::vertex::Vertex3D vertex;
                        vertex.position.x = xy * cosf(sectorAngle);
                        vertex.position.y = xy * sinf(sectorAngle);
                        vertex.position.z = z;

                        vertex.normal = glm::normalize(vertex.position);

                        vertex.textureCoordinates.x = (float)j / sectorCount;
                        vertex.textureCoordinates.y = (float)i / stackCount;

                        vertices.push_back(vertex);
                    }
                }
                for (int i = 0; i < stackCount; ++i)
                {
                    int k1 = i * (sectorCount + 1);
                    int k2 = k1 + sectorCount + 1;

                    for (int j = 0; j < sectorCount; ++j, ++k1, ++k2)
                    {
                        if (i != 0)
                        {
                            indices.push_back(k1);
                            indices.push_back(k2);
                            indices.push_back(k1 + 1);
                        }

                        if (i != (stackCount - 1))
                        {
                            indices.push_back(k1 + 1);
                            indices.push_back(k2);
                            indices.push_back(k2 + 1);
                        }
                    }
                }
                auto entity = m_scene->addEntity("New entity");

                engine::CPUMesh mesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(vertices, indices);
                entity->addComponent<engine::StaticMeshComponent>(std::vector<engine::CPUMesh>{mesh});
            }

            ImGui::EndPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}
ELIX_NESTED_NAMESPACE_END