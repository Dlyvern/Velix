#include "Editor/Editor.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"

#include "Engine/PluginSystem/PluginLoader.hpp"

#include "Editor/FileHelper.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <iostream>
#include <cstring>

#include <GLFW/glfw3.h>

static bool g_draggingWindow = false;
static ImVec2 g_dragStartMouse;
static int g_dragStartWindowX, g_dragStartWindowY;

ELIX_NESTED_NAMESPACE_BEGIN(editor)

Editor::Editor()
{
    
}

void Editor::initStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 14.0f;
    style.TabBorderSize = 1.0f;

    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.WindowPadding = ImVec2(10, 10);
    style.PopupRounding = 5.0f;

    ImVec4 accent       = ImVec4(0.25f, 0.55f, 1.00f, 1.00f);
    ImVec4 darkBG      = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    ImVec4 panelBG     = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    ImVec4 lightPanel  = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    ImVec4 highlight    = ImVec4(1.00f, 0.35f, 0.10f, 1.00f);

    colors[ImGuiCol_Text]               = ImVec4(0.85f, 0.88f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled]       = ImVec4(0.45f, 0.47f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]           = darkBG;
    colors[ImGuiCol_ChildBg]            = panelBG;
    colors[ImGuiCol_PopupBg]            = ImVec4(0.08f, 0.08f, 0.09f, 0.98f);
    colors[ImGuiCol_Border]             = ImVec4(0.18f, 0.18f, 0.20f, 0.60f);
    colors[ImGuiCol_BorderShadow]       = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg]            = panelBG;
    colors[ImGuiCol_FrameBgHovered]     = lightPanel;
    colors[ImGuiCol_FrameBgActive]      = accent;
    colors[ImGuiCol_TitleBg]            = darkBG;
    colors[ImGuiCol_TitleBgActive]      = panelBG;
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0, 0, 0, 0.60f);
    colors[ImGuiCol_MenuBarBg]          = panelBG;
    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.05f, 0.05f, 0.06f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.22f, 0.24f, 0.26f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]= accent;
    colors[ImGuiCol_ScrollbarGrabActive]= ImVec4(0.00f, 0.55f, 1.00f, 1.00f);
    colors[ImGuiCol_CheckMark]          = accent;
    colors[ImGuiCol_SliderGrab]         = accent;
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.20f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]             = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonActive]       = accent;
    colors[ImGuiCol_Header]             = panelBG;
    colors[ImGuiCol_HeaderHovered]      = lightPanel;
    colors[ImGuiCol_HeaderActive]       = accent;
    colors[ImGuiCol_ResizeGrip]         = ImVec4(0.12f, 0.12f, 0.14f, 0.60f);
    colors[ImGuiCol_ResizeGripHovered]  = accent;
    colors[ImGuiCol_ResizeGripActive]   = accent;
    colors[ImGuiCol_Tab]                = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TabHovered]         = accent;
    colors[ImGuiCol_TabActive]          = ImVec4(0.17f, 0.17f, 0.20f, 1.00f);
    colors[ImGuiCol_TabUnfocused]       = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]     = ImVec4(0.00f, 0.40f, 1.00f, 0.30f);
    colors[ImGuiCol_DragDropTarget]     = highlight;
    colors[ImGuiCol_NavHighlight]       = accent;
    colors[ImGuiCol_ModalWindowDimBg]   = ImVec4(0.07f, 0.07f, 0.07f, 0.80f);

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("./resources/fonts/JetBrainsMono-Regular.ttf", 16.0f);

    auto vulkanContext = core::VulkanContext::getContext();
    auto device = vulkanContext->getDevice();
    auto physicalDevice = vulkanContext->getPhysicalDevice();
    auto graphicsQueue = vulkanContext->getGraphicsQueue();

    auto commandPool = core::CommandPool::createShared(vulkanContext->getDevice(), core::VulkanContext::getContext()->getGraphicsFamily());

    m_logoTexture = std::make_shared<engine::TextureImage>();
    m_folderTexture = std::make_shared<engine::TextureImage>();
    m_fileTexture = std::make_shared<engine::TextureImage>();

    m_logoTexture->load("./resources/textures/VelixFire.png", commandPool);
    m_logoDescriptorSet = ImGui_ImplVulkan_AddTexture(m_logoTexture->vkSampler(), m_logoTexture->vkImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_folderTexture->load("./resources/textures/folder.png", commandPool);
    m_folderDescriptorSet = ImGui_ImplVulkan_AddTexture(m_folderTexture->vkSampler(), m_folderTexture->vkImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_fileTexture->load("./resources/textures/file.png", commandPool);
    m_fileDescriptorSet = ImGui_ImplVulkan_AddTexture(m_fileTexture->vkSampler(), m_fileTexture->vkImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Editor::showDockSpace()
{
    static bool dockspaceOpen = true;
    constexpr static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;

    if (m_isDockingWindowFullscreen)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
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

        ImGuiID titleBarDock = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Up, 0.06f, nullptr, &dockMainId);
        ImGui::DockBuilderDockWindow("TitleBar", titleBarDock);

        ImGuiID toolbarDock = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Up, 0.06f, nullptr, &dockMainId);
        ImGui::DockBuilderDockWindow("Toolbar", toolbarDock);

        ImGuiID dockIdRight = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, nullptr, &dockMainId);

        ImGuiID dockIdRightTop = dockIdRight;
        ImGuiID dockIdRightBottom = ImGui::DockBuilderSplitNode(dockIdRightTop, ImGuiDir_Down, 0.5f, nullptr, &dockIdRightTop);

        ImGuiID dockIdDown = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.25f, nullptr, &dockMainId);
        ImGuiID dockIdAssets = dockIdDown;
        ImGuiID dockIdBottomPanel = ImGui::DockBuilderSplitNode(dockIdAssets, ImGuiDir_Down, 0.3f, nullptr, &dockIdAssets);

        ImGui::DockBuilderDockWindow("BottomPanel", dockIdBottomPanel);
        ImGui::DockBuilderDockWindow("Assets", dockIdAssets);
        ImGui::DockBuilderDockWindow("Hierarchy", dockIdRightTop);
        ImGui::DockBuilderDockWindow("Details", dockIdRightBottom);
        ImGui::DockBuilderDockWindow("Viewport", dockMainId);

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

    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::SetCursorPos(ImVec2(4, (ImGui::GetWindowHeight() - 30) * 0.5f));
    ImVec2 logoSize = ImVec2(50, 30);
    ImGui::Image(m_logoDescriptorSet, logoSize);
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

    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar();

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y + ImGui::GetItemRectSize().y));
    
    if (ImGui::BeginPopup("FilePopup"))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        // static bool isNewProjectPopUpOpened = false;
        // static bool isOpenProjectPopUpOpened = false;

        // //TODO REMOVE THIS WHEN VELIX_INSTALLER IS READY, THIS IS TEMPORARY SOLUTION FOR DEVELOPMENT
        // if(ImGui::Button("Open project"))
        // {
        //     isOpenProjectPopUpOpened = true;
        // }

        // if(ImGui::Button("Create project"))
        // {
        //     isNewProjectPopUpOpened = true;
        // };

        // if(isOpenProjectPopUpOpened)
        // {
        //     ImGui::OpenPopup("Open project");
        //     isOpenProjectPopUpOpened = false;
        // }

        // if(isNewProjectPopUpOpened)
        // {
        //     ImGui::OpenPopup("Create New Project");
        //     isNewProjectPopUpOpened = false;
        // }

        // if (ImGui::BeginPopupModal("Open project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        // {
        //     static std::filesystem::path documentDirectory = FileHelper::getDocumentsDirectory() + "/";
        //     static std::string tmpClickedDirectory;

        //     for(const auto& entry : std::filesystem::recursive_directory_iterator(documentDirectory))
        //     {
        //         if(entry.is_directory())
        //             continue;
        //         else if(entry.is_regular_file())
        //             if(entry.path().extension() == ".elixirproject")
        //             {
        //                 auto parentPath = entry.path().parent_path();

        //                 if(parentPath.string() == m_currentProject.directory)
        //                     continue;

        //                 if(ImGui::Button(parentPath.string().c_str()))
        //                     tmpClickedDirectory = entry.path().parent_path().string();
        //             }
        //     }

        //     ImGui::Spacing();

        //     if(ImGui::Button("Open", ImVec2(120, 0)))
        //     {
        //         m_currentProject.directory = tmpClickedDirectory;
        //         tmpClickedDirectory.clear();
        //         ImGui::CloseCurrentPopup();
        //     }

        //     ImGui::SameLine();

        //     if (ImGui::Button("Cancel", ImVec2(120, 0)))
        //         ImGui::CloseCurrentPopup();

        //     ImGui::EndPopup();
        // }

        // if (ImGui::BeginPopupModal("Create New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        // {
        //     static bool firstTime = true;

        //     static char projectName[128] = "";
        //     static char projectPath[256] = "";

        //     if(firstTime)
        //     {
        //         std::string documentDirectory = FileHelper::getDocumentsDirectory() + "/";
        //         std::strncpy(projectPath, documentDirectory.c_str(), sizeof(projectPath) - 1);
        //         projectPath[sizeof(projectPath) - 1] = '\0';
        //     }

        //     ImGui::InputText("Project Name", projectName, IM_ARRAYSIZE(projectName));
        //     ImGui::InputText("Project Path", projectPath, IM_ARRAYSIZE(projectPath));

        //     ImGui::Spacing();

        //     if (ImGui::Button("Create", ImVec2(120, 0)))
        //     {
        //         std::string projectTemplateDir = FileHelper::getExecutablePath().string() + "/resources/projectTemplate";
                
        //         std::string executablePath = FileHelper::getExecutablePath();
        //         std::string projectDir(projectPath);
        //         projectDir += projectName;

        //         try
        //         {
        //             std::filesystem::copy(projectTemplateDir, projectDir,
        //             std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);

        //             std::cout << "Directory copied successfully\n";
        //         }
        //         catch (const std::filesystem::filesystem_error& e)
        //         {
        //             std::cerr << "Error copying directory: " << e.what() << '\n';
        //         }

        //         m_currentProject.directory = projectDir;

        //         ImGui::CloseCurrentPopup();
        //     }

        //     ImGui::SameLine();

        //     if (ImGui::Button("Cancel", ImVec2(120, 0)))
        //     {
        //         ImGui::CloseCurrentPopup();
        //     }

        //     ImGui::EndPopup();
        // }

        if (ImGui::Button("New Scene")) {}
        if (ImGui::Button("Open...")) {}
        if (ImGui::Button("Save"))
        {
            if (m_scene)
                m_scene->saveSceneToFile("./resources/scenes/default_scene.scene"); //!Kinda funny, fix me
        }

        ImGui::Separator();
        if (ImGui::Button("Exit")) {}
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
    
    auto window = core::VulkanContext::getContext()->getSwapchain()->getWindow();
    GLFWwindow* windowHandler = window->getRawHandler();

    if (ImGui::Button("_", ImVec2(buttonSize, buttonSize * 0.9f)))
        window->iconify();
    
    ImGui::SameLine();

    if (ImGui::Button("[]", ImVec2(buttonSize, buttonSize * 0.9f)))
    {
        m_isDockingWindowFullscreen = !m_isDockingWindowFullscreen;
        glfwSetWindowAttrib(windowHandler, GLFW_DECORATED, !m_isDockingWindowFullscreen);
        if (m_isDockingWindowFullscreen)
        {
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowPos(windowHandler, 0, 0);
            glfwSetWindowSize(windowHandler, mode->width, mode->height);
        }
        else
            window->setSize(1280, 720);
    }

    ImGui::SameLine();

    if (ImGui::Button("X", ImVec2(buttonSize, buttonSize * 0.9f)))
        window->close();

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::End();

    ImGui::PopStyleVar(2);
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
        const std::vector<std::string> selectionModes = {"Translate", "Rotate", "Scale"};
        static int gizmoMode = 0;

        // ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(50, 200));
        if (ImGui::BeginCombo("##Selection mode", selectionModes[gizmoMode].c_str()))
        {
            for (int i = 0; i < selectionModes.size(); ++i)
            {
                const bool isSelected = (gizmoMode == i);

                if (ImGui::Selectable(selectionModes[i].c_str(), isSelected))
                    gizmoMode = i;
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        // ImGui::SameLine(200);
        ImGui::SameLine();

        static bool isPlaying = false;              
        if (ImGui::Button(isPlaying ? "Pause" : "Play"))
        {
            isPlaying = !isPlaying;
        }

        ImGui::SameLine();

        if (ImGui::Button("Stop"))
        {
            isPlaying = false;
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

    if (ImGui::Button("Build Project"))
    {
        if(m_currentProject.directory.empty())
        {
            std::cerr << "Project directory is empty" << std::endl;
            ImGui::End();
            return;
        }

        //!It won't work on machine wihtout cmake, we need to provide a compiler along with the engine
        std::string cmakeBuildCommand = "cmake --build " + m_currentProject.directory + "/build" + " --config Release";

        std::string cmakeCommand = 
        "cmake -S " + m_currentProject.directory + "/build" + 
        " -B " + m_currentProject.directory + "/build" +
        " -DCMAKE_PREFIX_PATH=" + FileHelper::getExecutablePath().string();

        std::cout << cmakeCommand << std::endl;

        auto cmakeResult = FileHelper::executeCommand(cmakeCommand);

        std::cout << cmakeResult.second << std::endl;

        if(cmakeResult.first != 0)
        {
            ImGui::End();
            return;
        }

        auto cmakeBuildResult = FileHelper::executeCommand(cmakeBuildCommand);

        std::cerr << cmakeBuildResult.second << std::endl;

        if(cmakeBuildResult.first != 0)
        {
            std::cerr << "Failed to build project" << std::endl;
            // std::cerr << cmakeBuildResult.second << std::endl;
            ImGui::End();
            return;
        }

        std::cout << "Successfully built project" << std::endl;

        std::string extension = SHARED_LIB_EXTENSION;

        engine::LibraryHandle library = engine::PluginLoader::loadLibrary(m_currentProject.directory + "/build/" + "libGameModule" + extension);

        if(!library)
        {
            std::cerr << "Failed to get a library" << std::endl;
            ImGui::End();
            return;
        }

        auto function = engine::PluginLoader::getFunction<engine::ScriptsRegister&(*)()>("getScriptsRegister", library);

        if(function)
        {
            engine::ScriptsRegister& scriptsRegister = function();

            if(scriptsRegister.getScripts().empty())
                std::cerr << "Sripts are empty" << std::endl;

            for(const auto& scriptRegister : scriptsRegister.getScripts())
            {
                auto script = scriptsRegister.createScript(scriptRegister.first);

                if(!script)
                {
                    std::cerr << "Failed to get script" << std::endl;
                    continue;
                }

                script->onStart();
                script->onUpdate(0.0f);
            }
        }
        else
            std::cerr << "Failed to get hello function" << std::endl;

        engine::PluginLoader::closeLibrary(library);
    }

    ImGui::SameLine();
    
    if(ImGui::Button("Assets"))
    {
        m_showAssetsWindow = !m_showAssetsWindow;
    }

    ImGui::End();
}

void Editor::drawFrame(VkDescriptorSet viewportDescriptorSet)
{
    showDockSpace();

    drawCustomTitleBar();
    drawToolBar();

    if(viewportDescriptorSet)
        drawViewport(viewportDescriptorSet);

    drawAssets();
    drawBottomPanel();
    drawHierarchy();
    drawDetails();
}

void Editor::drawDetails()
{
    ImGui::Begin("Details");

    if(!m_selectedEntity)
        return ImGui::End();
        
    char buffer[128];
    std::strncpy(buffer, m_selectedEntity->getName().c_str(), sizeof(buffer));
    if (ImGui::InputText("##Name", buffer, sizeof(buffer)))
        m_selectedEntity->setName(std::string(buffer));

    for(const auto& [_, component] : m_selectedEntity->getSingleComponents())
    {
        if(auto transformComponent = dynamic_cast<engine::Transform3DComponent*>(component.get()))
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

                    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(100,100,100,255));
                    if (ImGui::Button("R"))
                        position = glm::vec3(0.0f);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();

                    ImGui::DragFloat3("##Position", &position.x, 0.5f);

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
                    if (ImGui::DragFloat3("##Rotation", &euler.x, 0.5f))
                        transformComponent->setEulerDegrees(euler);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Scale");
                    ImGui::TableSetColumnIndex(1);
                    auto scale = transformComponent->getScale();
                    if (ImGui::DragFloat3("##Scale", &scale.x, 0.1f))
                        transformComponent->setScale(scale);

                    ImGui::EndTable();
                }
            }
        }
        else if(auto lightComponent = dynamic_cast<engine::LightComponent*>(component.get()))
        {
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto lightType = lightComponent->getLightType();
                auto light = lightComponent->getLight();

                static const std::vector<const char*> lightTypes
                {
                    "Directional",
                    "Spot",
                    "Point"
                };

                static int currentLighType = 0;

                switch(lightType)
                {
                    case engine::LightComponent::LightType::DIRECTIONAL: currentLighType = 0; break;
                    case engine::LightComponent::LightType::SPOT: currentLighType = 1; break;
                    case engine::LightComponent::LightType::POINT: currentLighType = 2; break;
                };

                if(ImGui::Combo("Light type", &currentLighType, lightTypes.data(), lightTypes.size()))
                {
                    if(currentLighType == 0)
                        lightComponent->changeLightType(engine::LightComponent::LightType::DIRECTIONAL);
                    else if(currentLighType == 1)
                        lightComponent->changeLightType(engine::LightComponent::LightType::SPOT);
                    else if(currentLighType == 2)
                        lightComponent->changeLightType(engine::LightComponent::LightType::POINT);
                    
                    lightType = lightComponent->getLightType();
                    light = lightComponent->getLight();
                }

                ImGui::DragFloat3("Light position", &light->position.x, 0.1f, 0.0f);
                ImGui::ColorEdit3("Light color", &light->color.x);
                ImGui::DragFloat("Light strength", &light->strength, 0.1f, 0.0f);

                if(lightType == engine::LightComponent::LightType::POINT)
                {
                    auto pointLight = dynamic_cast<engine::PointLight*>(light.get());
                    ImGui::DragFloat("Light radius", &pointLight->radius, 0.1, 0.0f, 360.0f);
                }
                else if(lightType == engine::LightComponent::LightType::DIRECTIONAL)
                {
                    auto directionalLight = dynamic_cast<engine::DirectionalLight*>(light.get());
                    ImGui::DragFloat3("Light direction", &directionalLight->direction.x);
                }
                else if(lightType == engine::LightComponent::LightType::SPOT)
                {
                    auto spotLight = dynamic_cast<engine::SpotLight*>(light.get());
                    ImGui::DragFloat3("Light direction", &spotLight->direction.x);
                    ImGui::DragFloat("Inner", &spotLight->innerAngle);
                    ImGui::DragFloat("Outer", &spotLight->outerAngle);
                }
            }
        }
        else if(auto staticComponent = dynamic_cast<engine::StaticMeshComponent*>(component.get()))
        {
            if (ImGui::CollapsingHeader("Static mesh", ImGuiTreeNodeFlags_DefaultOpen))
            {
            }
        }
    }

    ImGui::End();
}

void Editor::drawViewport(VkDescriptorSet viewportDescriptorSet)
{
    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&windowClass);

    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_None);

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    ImGui::Image(viewportDescriptorSet, ImVec2(viewportPanelSize.x, viewportPanelSize.y));
    ImGui::End();
}

void Editor::drawAssets()
{
    if(!m_showAssetsWindow)
        return;
    
    ImGui::Begin("Assets");

    if(m_currentProject.directory.empty())
    {
        ImGui::End();
        return;
    }

    float windowWidth = ImGui::GetContentRegionAvail().x;
    float itemWidth = 80.0f;
    int columns = (int)(windowWidth / itemWidth);
    
    if (columns < 1) 
        columns = 1;

    ImGui::Columns(columns, "AssetsColumns", false);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    for(const auto& entry : std::filesystem::recursive_directory_iterator(m_currentProject.directory))
    {
        std::string id = entry.path().string();
        ImGui::PushID(id.c_str());

        ImGui::BeginGroup();

        if(entry.is_directory())
        {
            ImGui::ImageButton(id.c_str(), m_folderDescriptorSet, ImVec2(50, 50));

            std::string folderName = entry.path().filename().string();

            ImGui::TextWrapped("%s", folderName.c_str());
        }
        else if(entry.is_regular_file())
        {
            //Todo later
            // VkDescriptorSet fileIcon = getFileIconForExtension(entry.path().extension().string());

            ImGui::ImageButton(id.c_str(), m_fileDescriptorSet, ImVec2(50, 50));

            std::string fileName = entry.path().filename().string();

            ImGui::TextWrapped("%s", fileName.c_str());
        }

        ImGui::EndGroup();

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", entry.path().filename().string().c_str());
        }

        ImGui::PopID();

        ImGui::NextColumn();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::Columns(1);

    ImGui::End();
}

void Editor::drawHierarchy()
{
    ImGui::Begin("Hierarchy");

    if(!m_scene)
        return ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

    for(auto& entity : m_scene->getEntities())
    {
        // ImGui::PushID(entity->getID());

        auto entityName = entity->getName().c_str();
        
        bool selected = (entity == m_selectedEntity);

        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;

        if (selected)
            nodeFlags |= ImGuiTreeNodeFlags_Selected;

        bool nodeOpen = ImGui::TreeNodeEx(entityName, nodeFlags);

        if (ImGui::IsItemClicked())
            m_selectedEntity = entity;

        if(nodeOpen)
            ImGui::TreePop();

        // ImGui::PopID();
    }

    ImGui::PopStyleVar(2);

    ImGui::End();
}

ELIX_NESTED_NAMESPACE_END