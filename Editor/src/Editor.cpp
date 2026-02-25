#include "Editor/Editor.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/RigidBodyComponent.hpp"
#include "Engine/Primitives.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Render/ObjectIdEncoding.hpp"
#include "Engine/Shaders/ShaderCompiler.hpp"

#include "Engine/Primitives.hpp"
#include "Engine/Components/CollisionComponent.hpp"

#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/Utilities/ImageUtilities.hpp"

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
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <cmath>
#include <sstream>
#include <unordered_set>

#include "ImGuizmo.h"
#include <nlohmann/json.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace
{
    std::string toLowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return text;
    }

    bool isEditableTextPath(const std::filesystem::path &path)
    {
        static const std::unordered_set<std::string> editableExtensions = {
            ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese", ".glsl", ".hlsl", ".fx",
            ".cpp", ".cxx", ".cc", ".c", ".hpp", ".hh", ".hxx", ".h", ".inl",
            ".json", ".scene", ".yaml", ".yml", ".ini", ".cfg", ".toml",
            ".txt", ".md", ".cmake", ".elixproject"};

        const std::string extension = toLowerCopy(path.extension().string());
        return editableExtensions.find(extension) != editableExtensions.end();
    }

    const TextEditor::LanguageDefinition &jsonLanguageDefinition()
    {
        static bool initialized = false;
        static TextEditor::LanguageDefinition languageDefinition;

        if (!initialized)
        {
            initialized = true;
            languageDefinition.mName = "JSON";
            languageDefinition.mCaseSensitive = true;
            languageDefinition.mCommentStart = "/*";
            languageDefinition.mCommentEnd = "*/";
            languageDefinition.mSingleLineComment = "//";
            languageDefinition.mAutoIndentation = true;
            languageDefinition.mKeywords = {"true", "false", "null"};

            languageDefinition.mTokenRegexStrings = {
                {R"("([^"\\]|\\.)*")", TextEditor::PaletteIndex::String},
                {R"([-+]?[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?)", TextEditor::PaletteIndex::Number},
                {R"([{}\[\],:])", TextEditor::PaletteIndex::Punctuation}};
        }

        return languageDefinition;
    }

    const TextEditor::LanguageDefinition &iniLanguageDefinition()
    {
        static bool initialized = false;
        static TextEditor::LanguageDefinition languageDefinition;

        if (!initialized)
        {
            initialized = true;
            languageDefinition.mName = "INI/Config";
            languageDefinition.mCaseSensitive = false;
            languageDefinition.mCommentStart = "";
            languageDefinition.mCommentEnd = "";
            languageDefinition.mSingleLineComment = ";";
            languageDefinition.mAutoIndentation = false;

            languageDefinition.mTokenRegexStrings = {
                {R"(\[[^\]]+\])", TextEditor::PaletteIndex::PreprocIdentifier},
                {R"([A-Za-z_][A-Za-z0-9_\.\-]*(?=\s*=))", TextEditor::PaletteIndex::Identifier},
                {R"("([^"\\]|\\.)*")", TextEditor::PaletteIndex::String},
                {R"([-+]?[0-9]+(\.[0-9]+)?)", TextEditor::PaletteIndex::Number},
                {R"(=)", TextEditor::PaletteIndex::Punctuation}};
        }

        return languageDefinition;
    }

    bool computeLocalBoundsFromMeshes(const std::vector<elix::engine::CPUMesh> &meshes, glm::vec3 &outMin, glm::vec3 &outMax)
    {
        outMin = glm::vec3(std::numeric_limits<float>::max());
        outMax = glm::vec3(std::numeric_limits<float>::lowest());
        bool hasVertexData = false;

        for (const auto &mesh : meshes)
        {
            if (mesh.vertexStride < sizeof(glm::vec3) || mesh.vertexData.empty())
                continue;

            const size_t vertexCount = mesh.vertexData.size() / mesh.vertexStride;
            const uint8_t *basePtr = mesh.vertexData.data();

            for (size_t i = 0; i < vertexCount; ++i)
            {
                glm::vec3 position{0.0f};
                std::memcpy(&position, basePtr + i * mesh.vertexStride, sizeof(glm::vec3));

                outMin = glm::min(outMin, position);
                outMax = glm::max(outMax, position);
                hasVertexData = true;
            }
        }

        return hasVertexData;
    }
} // namespace

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
    ImVec4 *colors = style.Colors;

    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding = 3.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.WindowPadding = ImVec2(10, 10);
    style.CellPadding = ImVec2(6, 4);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 9.0f;

    const ImVec4 bg0 = ImVec4(0.070f, 0.073f, 0.078f, 1.000f);
    const ImVec4 bg1 = ImVec4(0.095f, 0.100f, 0.108f, 1.000f);
    const ImVec4 bg2 = ImVec4(0.120f, 0.126f, 0.136f, 1.000f);
    const ImVec4 bg3 = ImVec4(0.155f, 0.164f, 0.176f, 1.000f);

    const ImVec4 accentBlue = ImVec4(0.120f, 0.420f, 0.900f, 1.000f);
    const ImVec4 accentBlueHover = ImVec4(0.180f, 0.500f, 0.950f, 1.000f);
    const ImVec4 accentBlueActive = ImVec4(0.090f, 0.350f, 0.780f, 1.000f);
    const ImVec4 accentOrange = ImVec4(0.930f, 0.490f, 0.130f, 1.000f);

    colors[ImGuiCol_Text] = ImVec4(0.880f, 0.900f, 0.930f, 1.000f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.520f, 0.560f, 0.620f, 1.000f);
    colors[ImGuiCol_WindowBg] = bg0;
    colors[ImGuiCol_ChildBg] = bg1;
    colors[ImGuiCol_PopupBg] = bg1;
    colors[ImGuiCol_Border] = ImVec4(0.245f, 0.255f, 0.280f, 0.900f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);

    colors[ImGuiCol_FrameBg] = bg2;
    colors[ImGuiCol_FrameBgHovered] = bg3;
    colors[ImGuiCol_FrameBgActive] = accentBlueActive;

    colors[ImGuiCol_TitleBg] = bg0;
    colors[ImGuiCol_TitleBgActive] = bg1;
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(bg0.x, bg0.y, bg0.z, 0.700f);
    colors[ImGuiCol_MenuBarBg] = bg1;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(bg0.x, bg0.y, bg0.z, 0.800f);
    colors[ImGuiCol_ScrollbarGrab] = bg3;
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.250f, 0.265f, 0.290f, 1.000f);
    colors[ImGuiCol_ScrollbarGrabActive] = accentBlue;

    colors[ImGuiCol_CheckMark] = accentBlueHover;
    colors[ImGuiCol_SliderGrab] = accentBlue;
    colors[ImGuiCol_SliderGrabActive] = accentBlueHover;

    colors[ImGuiCol_Button] = bg2;
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.195f, 0.206f, 0.222f, 1.000f);
    colors[ImGuiCol_ButtonActive] = accentBlueActive;

    colors[ImGuiCol_Header] = bg2;
    colors[ImGuiCol_HeaderHovered] = bg3;
    colors[ImGuiCol_HeaderActive] = accentBlueActive;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.220f, 0.230f, 0.250f, 0.600f);
    colors[ImGuiCol_ResizeGripHovered] = accentBlue;
    colors[ImGuiCol_ResizeGripActive] = accentBlueHover;

    colors[ImGuiCol_Tab] = bg1;
    colors[ImGuiCol_TabHovered] = ImVec4(0.190f, 0.205f, 0.240f, 1.000f);
    colors[ImGuiCol_TabActive] = bg2;
    colors[ImGuiCol_TabUnfocused] = bg1;
    colors[ImGuiCol_TabUnfocusedActive] = bg2;

    colors[ImGuiCol_TextSelectedBg] = ImVec4(accentBlue.x, accentBlue.y, accentBlue.z, 0.400f);
    colors[ImGuiCol_DragDropTarget] = accentOrange;
    colors[ImGuiCol_NavHighlight] = accentBlue;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.000f, 1.000f, 1.000f, 0.650f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.060f, 0.065f, 0.072f, 0.300f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.060f, 0.065f, 0.072f, 0.750f);

    colors[ImGuiCol_Separator] = ImVec4(0.235f, 0.245f, 0.270f, 0.800f);
    colors[ImGuiCol_SeparatorHovered] = accentBlue;
    colors[ImGuiCol_SeparatorActive] = accentBlueHover;

    colors[ImGuiCol_TableHeaderBg] = bg2;
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.260f, 0.275f, 0.300f, 1.000f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.180f, 0.192f, 0.210f, 0.600f);
    colors[ImGuiCol_TableRowBg] = bg1;
    colors[ImGuiCol_TableRowBgAlt] = bg2;

    colors[ImGuiCol_PlotLines] = accentBlue;
    colors[ImGuiCol_PlotLinesHovered] = accentBlueHover;
    colors[ImGuiCol_PlotHistogram] = accentBlue;
    colors[ImGuiCol_PlotHistogramHovered] = accentBlueHover;

    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("./resources/fonts/JetBrainsMono-Regular.ttf", 16.0f);

    m_resourceStorage.loadNeededResources();

    m_assetsWindow = std::make_shared<AssetsWindow>(&m_resourceStorage, m_assetsPreviewSystem);
    m_assetsWindow->setOnMaterialOpenRequest([this](const std::filesystem::path &path)
                                             { openMaterialEditor(path); });
    m_assetsWindow->setOnTextAssetOpenRequest([this](const std::filesystem::path &path)
                                              { openTextDocument(path); });

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

    m_defaultSampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

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
    m_dockSpaceId = dockspaceId;
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

        ImGuiID bottomStripDock = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.04f, nullptr, &dockMainId);
        ImGui::DockBuilderDockWindow("BottomPanel", bottomStripDock);

        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.20f, nullptr, &dockMainId);

        ImGuiID dockRightTop = dockRight;
        ImGuiID dockRightBottom = ImGui::DockBuilderSplitNode(dockRightTop, ImGuiDir_Down, 0.5f, nullptr, &dockRightTop);

        ImGui::DockBuilderDockWindow("Hierarchy", dockRightTop);
        ImGui::DockBuilderDockWindow("Details", dockRightBottom);

        ImGuiID assetsDock = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.25f, nullptr, &dockMainId);
        m_assetsPanelsDockId = assetsDock;
        ImGui::DockBuilderDockWindow("Assets", assetsDock);

        ImGui::DockBuilderDockWindow("Viewport", dockMainId);
        m_centerDockId = dockMainId;
        ImGui::DockBuilderFinish(dockspaceId);

        if (ImGuiDockNode *node = ImGui::DockBuilderGetNode(bottomStripDock))
        {
            node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
            node->LocalFlags |= ImGuiDockNodeFlags_NoSplit;
            node->LocalFlags |= ImGuiDockNodeFlags_NoResize;
            node->LocalFlags |= ImGuiDockNodeFlags_NoDockingOverMe;
        }
    }
}

void Editor::syncAssetsAndTerminalDocking()
{
    if (m_dockSpaceId == 0 || m_assetsPanelsDockId == 0)
        return;

    const bool assetsVisible = m_showAssetsWindow;
    const bool terminalVisible = m_showTerminal;

    if (assetsVisible == m_lastDockedAssetsVisibility && terminalVisible == m_lastDockedTerminalVisibility)
        return;

    m_lastDockedAssetsVisibility = assetsVisible;
    m_lastDockedTerminalVisibility = terminalVisible;

    if (!ImGui::DockBuilderGetNode(m_assetsPanelsDockId))
        return;

    ImGui::DockBuilderRemoveNodeChildNodes(m_assetsPanelsDockId);

    if (assetsVisible && terminalVisible)
    {
        ImGuiID leftNode = m_assetsPanelsDockId;
        ImGuiID rightNode = ImGui::DockBuilderSplitNode(leftNode, ImGuiDir_Right, 0.5f, nullptr, &leftNode);

        ImGui::DockBuilderDockWindow("Assets", leftNode);
        ImGui::DockBuilderDockWindow("Terminal with logs", rightNode);
    }
    else if (assetsVisible)
        ImGui::DockBuilderDockWindow("Assets", m_assetsPanelsDockId);
    else if (terminalVisible)
        ImGui::DockBuilderDockWindow("Terminal with logs", m_assetsPanelsDockId);

    ImGui::DockBuilderFinish(m_dockSpaceId);
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

                std::string token = "{{ClassName}}";
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

                ImGui::CloseCurrentPopup();

                m_notificationManager.showInfo("Successfully created a new c++ class");
                VX_EDITOR_INFO_STREAM("Created new script class: " << className);
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
            //     VX_EDITOR_ERROR_STREAM("Project directory is empty" << std::endl);
            //     ImGui::End();
            //     return;
            // }

            // //!It won't work on machine without cmake, we need to provide a compiler along with the engine
            // std::string cmakeBuildCommand = "cmake --build " + m_currentProject.directory + "/build" + " --config Release";

            // std::string cmakeCommand =
            // "cmake -S " + m_currentProject.directory + "/build" +
            // " -B " + m_currentProject.directory + "/build" +
            // " -DCMAKE_PREFIX_PATH=" + FileHelper::getExecutablePath().string();

            // VX_EDITOR_INFO_STREAM(cmakeCommand << std::endl);

            // auto cmakeResult = FileHelper::executeCommand(cmakeCommand);

            // VX_EDITOR_INFO_STREAM(cmakeResult.second << std::endl);

            // if(cmakeResult.first != 0)
            // {
            //     ImGui::End();
            //     return;
            // }

            // auto cmakeBuildResult = FileHelper::executeCommand(cmakeBuildCommand);

            // VX_EDITOR_ERROR_STREAM(cmakeBuildResult.second << std::endl);

            // if(cmakeBuildResult.first != 0)
            // {
            //     VX_EDITOR_ERROR_STREAM("Failed to build project" << std::endl);
            //     // VX_EDITOR_ERROR_STREAM(cmakeBuildResult.second << std::endl);
            //     ImGui::End();
            //     return;
            // }

            // VX_EDITOR_INFO_STREAM("Successfully built project" << std::endl);

            // std::string extension = SHARED_LIB_EXTENSION;

            // engine::LibraryHandle library = engine::PluginLoader::loadLibrary(m_currentProject.directory + "/build/" + "libGameModule" + extension);

            // if(!library)
            // {
            //     VX_EDITOR_ERROR_STREAM("Failed to get a library" << std::endl);
            //     ImGui::End();
            //     return;
            // }

            // auto function = engine::PluginLoader::getFunction<engine::ScriptsRegister&(*)()>("getScriptsRegister", library);

            // if(function)
            // {
            //     engine::ScriptsRegister& scriptsRegister = function();

            //     if(scriptsRegister.getScripts().empty())
            //         VX_EDITOR_ERROR_STREAM("Sripts are empty" << std::endl);

            //     for(const auto& scriptRegister : scriptsRegister.getScripts())
            //     {
            //         auto script = scriptsRegister.createScript(scriptRegister.first);

            //         if(!script)
            //         {
            //             VX_EDITOR_ERROR_STREAM("Failed to get script" << std::endl);
            //             continue;
            //         }

            //         script->onStart();
            //         script->onUpdate(0.0f);
            //     }
            // }
            // else
            //     VX_EDITOR_ERROR_STREAM("Failed to get hello function" << std::endl);

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
            auto project = m_currentProject.lock();
            if (m_scene && project)
            {
                m_scene->saveSceneToFile(project->entryScene);
                m_notificationManager.showInfo("Scene saved");
                VX_EDITOR_INFO_STREAM("Scene saved to: " << project->entryScene);
            }
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
    if (m_currentMode == mode)
        return;

    m_currentMode = mode;

    switch (mode)
    {
    case EditorMode::EDIT:
        VX_EDITOR_INFO_STREAM("Switched editor mode to EDIT");
        m_notificationManager.showInfo("Mode: Edit");
        break;
    case EditorMode::PLAY:
        VX_EDITOR_INFO_STREAM("Switched editor mode to PLAY");
        m_notificationManager.showSuccess("Mode: Play");
        break;
    case EditorMode::PAUSE:
        VX_EDITOR_INFO_STREAM("Switched editor mode to PAUSE");
        m_notificationManager.showWarning("Mode: Pause");
        break;
    }

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

            ImGui::Separator();
            ImGui::Text("Render Graph (frame #%llu)", static_cast<unsigned long long>(m_renderGraphProfilingData.frameIndex));
            ImGui::Text("Total draw calls: %u", m_renderGraphProfilingData.totalDrawCalls);
            ImGui::Text("Total CPU pass time: %.3f ms", m_renderGraphProfilingData.cpuTotalTimeMs);

            if (m_renderGraphProfilingData.gpuTimingAvailable)
                ImGui::Text("Total GPU pass time: %.3f ms", m_renderGraphProfilingData.gpuTotalTimeMs);
            else
                ImGui::TextDisabled("GPU timing unavailable on this GPU/queue");

            if (ImGui::BeginTable("RenderGraphPassStats", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Pass");
                ImGui::TableSetupColumn("Exec");
                ImGui::TableSetupColumn("Draws");
                ImGui::TableSetupColumn("CPU (ms)");
                ImGui::TableSetupColumn("GPU (ms)");
                ImGui::TableHeadersRow();

                for (const auto &passData : m_renderGraphProfilingData.passes)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(passData.passName.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", passData.executions);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%u", passData.drawCalls);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.3f", passData.cpuTimeMs);

                    ImGui::TableSetColumnIndex(4);
                    if (m_renderGraphProfilingData.gpuTimingAvailable)
                        ImGui::Text("%.3f", passData.gpuTimeMs);
                    else
                        ImGui::TextUnformatted("-");
                }

                ImGui::EndTable();
            }

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
    m_assetsPreviewSystem.beginFrame();

    handleInput();

    showDockSpace();
    syncAssetsAndTerminalDocking();

    drawCustomTitleBar();
    drawToolBar();

    if (viewportDescriptorSet)
        drawViewport(viewportDescriptorSet);

    drawMaterialEditors();

    drawDocument();
    drawAssets();
    drawTerminal();
    drawBottomPanel();
    drawHierarchy();
    drawDetails();

    m_notificationManager.render();
}

void Editor::updateAnimationPreview(float deltaTime)
{
    if (!m_scene || m_currentMode != EditorMode::EDIT)
        return;

    for (const auto &entity : m_scene->getEntities())
    {
        auto *animatorComponent = entity->getComponent<engine::AnimatorComponent>();
        if (!animatorComponent)
            continue;

        animatorComponent->update(deltaTime);
    }
}

static bool drawMaterialTextureSlot(
    const char *slotLabel,
    const std::string &currentTexturePath,
    AssetsPreviewSystem &previewSystem,
    ImVec2 thumbSize,
    bool &openPopupRequest)
{
    bool clicked = false;

    ImGui::PushID(slotLabel);

    auto ds = previewSystem.getOrRequestTexturePreview(currentTexturePath, nullptr);
    ImTextureID texId = (ImTextureID)(uintptr_t)ds;

    if (ImGui::ImageButton(slotLabel, texId, thumbSize))
    {
        openPopupRequest = true;
        clicked = true;
    }

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextUnformatted(slotLabel);

    std::string fileName = currentTexturePath.empty()
                               ? std::string("<None>")
                               : std::filesystem::path(currentTexturePath).filename().string();

    ImGui::TextDisabled("%s", fileName.c_str());

    if (ImGui::Button("Clear"))
    {
        // Caller should interpret empty path as "use default"
        clicked = true;
        // We signal clear by popupRequest=false and caller checks button click separately if needed.
        // If you want explicit behavior, split return enum.
    }
    ImGui::EndGroup();

    // Drag-drop support (optional but very useful)
    if (ImGui::BeginDragDropTarget())
    {
        // Example payload type, adapt to your engine
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            const char *droppedPath = static_cast<const char *>(payload->Data);
            // Caller should handle the actual assignment, this helper only draws.
            // You can redesign helper to output selected path if you want.
            (void)droppedPath;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::PopID();
    return clicked;
}

void Editor::drawMaterialEditors()
{
    auto project = m_currentProject.lock();
    if (!project)
        return;

    for (auto it = m_openMaterialEditors.begin(); it != m_openMaterialEditors.end();)
    {
        auto &matEditor = *it;
        const std::string matPath = matEditor.path.string();

        std::string title = matEditor.path.filename().string();
        if (matEditor.dirty)
            title += "*";

        std::string windowName = title + "###MaterialEditor_" + matPath;

        if (m_centerDockId != 0)
            ImGui::SetNextWindowDockID(m_centerDockId, ImGuiCond_Always);

        bool keepOpen = matEditor.open;

        if (ImGui::Begin(windowName.c_str(), &keepOpen))
        {
            if (project->cache.materialsByPath.find(matPath) == project->cache.materialsByPath.end())
            {
                m_assetsPreviewSystem.getOrRequestMaterialPreview(matPath);
                ImGui::TextDisabled("Loading material...");
                ImGui::End();
                matEditor.open = keepOpen;
                if (!matEditor.open)
                    it = m_openMaterialEditors.erase(it);
                else
                    ++it;
                continue;
            }

            auto &materialAsset = project->cache.materialsByPath[matPath];
            auto gpuMat = materialAsset.gpu;
            auto &cpuMat = materialAsset.cpuData;

            auto &ui = m_materialEditorUiState[matPath];

            if (ImGui::Button("Save"))
            {
                if (saveMaterialToDisk(matEditor.path, cpuMat))
                {
                    matEditor.dirty = false;
                    m_notificationManager.showSuccess("Material saved");
                    VX_EDITOR_INFO_STREAM("Material saved: " << matPath);
                }
                else
                {
                    m_notificationManager.showError("Failed to save material");
                    VX_EDITOR_ERROR_STREAM("Failed to save material: " << matPath);
                }
            }
            ImGui::SameLine();

            if (ImGui::Button("Revert"))
            {
                if (reloadMaterialFromDisk(matEditor.path))
                {
                    matEditor.dirty = false;
                    m_notificationManager.showInfo("Material reloaded from disk");
                    VX_EDITOR_INFO_STREAM("Material reloaded from disk: " << matPath);
                }
                else
                {
                    m_notificationManager.showError("Failed to reload material from disk");
                    VX_EDITOR_ERROR_STREAM("Failed to reload material from disk: " << matPath);
                }
            }
            ImGui::SameLine();

            ImGui::TextDisabled("%s", matPath.c_str());

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto previewDS = m_assetsPreviewSystem.getOrRequestMaterialPreview(matPath);
                ImTextureID previewId = (ImTextureID)(uintptr_t)previewDS;

                ImGui::Image(previewId, ImVec2(160, 160));
                ImGui::SameLine();

                ImGui::BeginGroup();
                ImGui::Text("Material Instance");
                ImGui::Spacing();
                ImGui::Text("Shader: PBR Forward");
                ImGui::Text("Blend: %s",
                            (gpuMat->params().flags & engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK) ? "Alpha Blend" : (gpuMat->params().flags & engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) ? "Masked"
                                                                                                                                                                                                                           : "Opaque");
                ImGui::EndGroup();
            }

            if (ImGui::CollapsingHeader("Surface"))
            {
                auto p = gpuMat->params();

                glm::vec4 baseColor = p.baseColorFactor;
                if (ImGui::ColorEdit4("Base Color", glm::value_ptr(baseColor)))
                {
                    gpuMat->setBaseColorFactor(baseColor);
                    cpuMat.baseColorFactor = baseColor;
                    matEditor.dirty = true;
                }

                glm::vec3 emissive = glm::vec3(p.emissiveFactor);
                if (ImGui::ColorEdit3("Emissive Color", glm::value_ptr(emissive)))
                {
                    gpuMat->setEmissiveFactor(emissive);
                    cpuMat.emissiveFactor = emissive;
                    matEditor.dirty = true;
                }

                float metallic = p.metallicFactor;
                if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f))
                {
                    gpuMat->setMetallic(metallic);
                    cpuMat.metallicFactor = metallic;
                    matEditor.dirty = true;
                }

                float roughness = p.roughnessFactor;
                if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f))
                {
                    gpuMat->setRoughness(roughness);
                    cpuMat.roughnessFactor = roughness;
                    matEditor.dirty = true;
                }

                float aoStrength = p.aoStrength;
                if (ImGui::SliderFloat("AO Strength", &aoStrength, 0.0f, 1.0f))
                {
                    gpuMat->setAoStrength(aoStrength);
                    cpuMat.aoStrength = aoStrength;
                    matEditor.dirty = true;
                }

                float normalScale = p.normalScale;
                if (ImGui::SliderFloat("Normal Strength", &normalScale, 0.0f, 4.0f))
                {
                    gpuMat->setNormalScale(normalScale);
                    cpuMat.normalScale = normalScale;
                    matEditor.dirty = true;
                }
            }

            if (ImGui::CollapsingHeader("UV"))
            {
                auto p = gpuMat->params();

                glm::vec2 uvScale = {p.uvTransform.x, p.uvTransform.y};
                glm::vec2 uvOffset = {p.uvTransform.z, p.uvTransform.w};

                if (ImGui::DragFloat2("UV Scale", glm::value_ptr(uvScale), 0.01f, -100.0f, 100.0f))
                {
                    gpuMat->setUVScale(uvScale);
                    cpuMat.uvScale = uvScale;
                    matEditor.dirty = true;
                }

                if (ImGui::DragFloat2("UV Offset", glm::value_ptr(uvOffset), 0.01f, -100.0f, 100.0f))
                {
                    gpuMat->setUVOffset(uvOffset);
                    cpuMat.uvOffset = uvOffset;
                    matEditor.dirty = true;
                }
            }

            if (ImGui::CollapsingHeader("Advanced"))
            {
                auto p = gpuMat->params();
                uint32_t flags = p.flags;

                bool alphaMask = (flags & engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK) != 0;
                bool alphaBlend = (flags & engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0;

                if (ImGui::Checkbox("Masked", &alphaMask))
                {
                    if (alphaMask)
                        alphaBlend = false;
                    uint32_t newFlags = 0;
                    if (alphaMask)
                        newFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK;
                    if (alphaBlend)
                        newFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND;

                    gpuMat->setFlags(newFlags);
                    cpuMat.flags = newFlags;
                    matEditor.dirty = true;
                }

                if (ImGui::Checkbox("Translucent", &alphaBlend))
                {
                    if (alphaBlend)
                        alphaMask = false;
                    uint32_t newFlags = 0;
                    if (alphaMask)
                        newFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK;
                    if (alphaBlend)
                        newFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND;

                    gpuMat->setFlags(newFlags);
                    cpuMat.flags = newFlags;
                    matEditor.dirty = true;
                }

                float alphaCutoff = p.alphaCutoff;
                if (ImGui::SliderFloat("Alpha Cutoff", &alphaCutoff, 0.0f, 1.0f))
                {
                    gpuMat->setAlphaCutoff(alphaCutoff);
                    cpuMat.alphaCutoff = alphaCutoff;
                    matEditor.dirty = true;
                }
            }

            if (ImGui::CollapsingHeader("Textures"))
            {
                struct TextureSlotRow
                {
                    const char *label;
                    std::string *cpuPath;
                    std::function<void(const std::string &)> assignToGpu;
                };

                TextureSlotRow rows[] =
                    {
                        {"Albedo", &cpuMat.albedoTexture, [&](const std::string &p)
                         {
                             if (p.empty())
                                 gpuMat->setAlbedoTexture(nullptr);
                             else if (auto itTex = project->cache.texturesByPath.find(p); itTex != project->cache.texturesByPath.end())
                                 gpuMat->setAlbedoTexture(itTex->second.gpu);
                         }},
                        {"Normal", &cpuMat.normalTexture, [&](const std::string &p)
                         {
                             if (p.empty())
                                 gpuMat->setNormalTexture(nullptr);
                             else if (auto itTex = project->cache.texturesByPath.find(p); itTex != project->cache.texturesByPath.end())
                                 gpuMat->setNormalTexture(itTex->second.gpu);
                         }},
                        {"ORM", &cpuMat.ormTexture, [&](const std::string &p)
                         {
                             if (p.empty())
                                 gpuMat->setOrmTexture(nullptr);
                             else if (auto itTex = project->cache.texturesByPath.find(p); itTex != project->cache.texturesByPath.end())
                                 gpuMat->setOrmTexture(itTex->second.gpu);
                         }},
                        {"Emissive", &cpuMat.emissiveTexture, [&](const std::string &p)
                         {
                             if (p.empty())
                                 gpuMat->setEmissiveTexture(nullptr);
                             else if (auto itTex = project->cache.texturesByPath.find(p); itTex != project->cache.texturesByPath.end())
                                 gpuMat->setEmissiveTexture(itTex->second.gpu);
                         }},
                    };

                for (auto &row : rows)
                {
                    ImGui::PushID(row.label);

                    ImGui::BeginGroup();

                    auto ds = m_assetsPreviewSystem.getOrRequestTexturePreview(*row.cpuPath, nullptr);
                    ImTextureID texId = (ImTextureID)(uintptr_t)ds;

                    if (ImGui::ImageButton("##thumb", texId, ImVec2(56, 56)))
                    {
                        ui.openTexturePopup = true;
                        ui.texturePopupSlot = row.label;
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);

                            if (project->cache.texturesByPath.find(droppedPath) != project->cache.texturesByPath.end())
                            {
                                *row.cpuPath = droppedPath;
                                row.assignToGpu(droppedPath);
                                matEditor.dirty = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::EndGroup();

                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(row.label);

                    std::string fileName = row.cpuPath->empty()
                                               ? std::string("<Default>")
                                               : std::filesystem::path(*row.cpuPath).filename().string();

                    ImGui::TextWrapped("%s", fileName.c_str());

                    if (ImGui::Button("Use Default"))
                    {
                        row.cpuPath->clear();
                        row.assignToGpu("");
                        matEditor.dirty = true;
                    }

                    ImGui::EndGroup();

                    if (ui.openTexturePopup && ui.texturePopupSlot == row.label)
                    {
                        std::string popupName = std::string("TexturePicker##") + matPath;
                        ImGui::OpenPopup(popupName.c_str());
                        ui.openTexturePopup = false;
                    }

                    std::string popupName = std::string("TexturePicker##") + matPath;
                    if (ImGui::BeginPopup(popupName.c_str()))
                    {
                        ImGui::Text("Select Texture for %s", row.label);
                        ImGui::Separator();

                        ImGui::InputTextWithHint("##Search", "Search textures...", ui.textureFilter, sizeof(ui.textureFilter));
                        ImGui::SameLine();
                        if (ImGui::Button("X"))
                            ui.textureFilter[0] = '\0';

                        ImGui::Separator();

                        if (ImGui::Selectable("<Default>"))
                        {
                            row.cpuPath->clear();
                            row.assignToGpu("");
                            matEditor.dirty = true;
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::BeginChild("TextureScroll", ImVec2(360, 260), true);

                        for (const auto &[texturePath, textureAsset] : project->cache.texturesByPath)
                        {
                            if (ui.textureFilter[0] != '\0')
                            {
                                std::string pathLower = texturePath;
                                std::string filterLower = ui.textureFilter;
                                std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);
                                std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

                                if (pathLower.find(filterLower) == std::string::npos)
                                    continue;
                            }

                            ImGui::PushID(texturePath.c_str());

                            auto texDS = m_assetsPreviewSystem.getOrRequestTexturePreview(texturePath);
                            ImTextureID imguiTexId = (ImTextureID)(uintptr_t)texDS;

                            bool selected = (*row.cpuPath == texturePath);

                            if (selected)
                                ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 200, 255, 255));

                            if (ImGui::ImageButton("##pick", imguiTexId, ImVec2(48, 48)))
                            {
                                *row.cpuPath = texturePath;
                                row.assignToGpu(texturePath);
                                matEditor.dirty = true;
                                ImGui::CloseCurrentPopup();
                            }

                            if (selected)
                                ImGui::PopStyleColor();

                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::Text("%s", texturePath.c_str());
                                ImGui::EndTooltip();
                            }

                            ImGui::SameLine();

                            ImGui::BeginGroup();
                            ImGui::TextWrapped("%s", std::filesystem::path(texturePath).filename().string().c_str());
                            ImGui::TextDisabled("%s", texturePath.c_str());
                            ImGui::EndGroup();

                            ImGui::Separator();
                            ImGui::PopID();
                        }

                        ImGui::EndChild();

                        if (ImGui::Button("Close"))
                            ImGui::CloseCurrentPopup();

                        ImGui::EndPopup();
                    }

                    ImGui::Separator();
                    ImGui::PopID();
                }
            }

            ImGui::Spacing();
            if (ImGui::Button("Open Material Graph"))
            {
                // TODO: open graph editor asset window (separate system)
                // openMaterialGraphEditor(matPath);
            }
        }

        ImGui::End();

        matEditor.open = keepOpen;
        if (!matEditor.open)
            it = m_openMaterialEditors.erase(it);
        else
            ++it;
    }
}

void Editor::drawDocument()
{
    if (!m_showDocumentWindow || m_openDocumentPath.empty())
        return;

    if (!std::filesystem::exists(m_openDocumentPath) || std::filesystem::is_directory(m_openDocumentPath))
    {
        VX_EDITOR_WARNING_STREAM("Document source no longer exists: " << m_openDocumentPath);
        m_notificationManager.showWarning("Opened document was removed");
        m_openDocumentPath.clear();
        m_openDocumentSavedText.clear();
        m_showDocumentWindow = false;
        return;
    }

    std::string windowName = m_openDocumentPath.filename().string() + "###Document";

    if (m_centerDockId != 0)
        ImGui::SetNextWindowDockID(m_centerDockId, ImGuiCond_Always);

    bool keepOpen = m_showDocumentWindow;
    if (!ImGui::Begin(windowName.c_str(), &keepOpen))
    {
        ImGui::End();
        m_showDocumentWindow = keepOpen;
        return;
    }

    const bool isShaderDocument = engine::shaders::ShaderCompiler::isCompilableShaderSource(m_openDocumentPath);
    const bool isDirty = !m_openDocumentPath.empty() && (m_textEditor.GetText() != m_openDocumentSavedText);

    if (ImGui::Button("Save"))
    {
        saveOpenDocument();
    }

    if (isShaderDocument)
    {
        ImGui::SameLine();
        if (ImGui::Button("Compile Shader"))
            compileOpenDocumentShader();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(isDirty ? "*" : "");

    ImGui::SameLine();
    ImGui::TextDisabled("Lang: %s", m_textEditor.GetLanguageDefinition().mName.c_str());

    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_openDocumentPath.string().c_str());

    m_textEditor.Render("TextEditor");

    ImGuiIO &io = ImGui::GetIO();
    const bool isCtrlDown = io.KeyCtrl;
    const bool isShiftDown = io.KeyShift;

    if (isCtrlDown && ImGui::IsKeyPressed(ImGuiKey_S, false))
        saveOpenDocument();

    if (isShaderDocument && isCtrlDown && isShiftDown && ImGui::IsKeyPressed(ImGuiKey_B, false))
        compileOpenDocumentShader();

    ImGui::End();
    m_showDocumentWindow = keepOpen;

    if (!m_showDocumentWindow)
    {
        m_openDocumentPath.clear();
        m_openDocumentSavedText.clear();
        m_textEditor.SetText("");
    }
}

void Editor::openTextDocument(const std::filesystem::path &path)
{
    if (path.empty() || !std::filesystem::exists(path) || std::filesystem::is_directory(path) || !isEditableTextPath(path))
    {
        VX_EDITOR_WARNING_STREAM("Open document failed. Invalid path: " << path);
        return;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        VX_EDITOR_ERROR_STREAM("Failed to open document: " << path);
        m_notificationManager.showError("Failed to open file");
        return;
    }

    std::stringstream stream;
    stream << file.rdbuf();
    file.close();

    m_openDocumentPath = path;
    m_openDocumentSavedText = stream.str();
    m_textEditor.SetText(m_openDocumentSavedText);
    setDocumentLanguageFromPath(path);
    m_showDocumentWindow = true;

    VX_EDITOR_INFO_STREAM("Opened document: " << path);
    m_notificationManager.showInfo("Opened: " + path.filename().string());
}

bool Editor::saveOpenDocument()
{
    if (m_openDocumentPath.empty())
        return false;

    std::ofstream file(m_openDocumentPath, std::ios::trunc);

    if (!file.is_open())
    {
        VX_EDITOR_ERROR_STREAM("Failed to save document: " << m_openDocumentPath);
        m_notificationManager.showError("Failed to save file");
        return false;
    }

    const std::string text = m_textEditor.GetText();
    file << text;
    file.close();

    if (!file.good())
    {
        VX_EDITOR_ERROR_STREAM("Failed to write document: " << m_openDocumentPath);
        m_notificationManager.showError("Failed to write file");
        return false;
    }

    m_openDocumentSavedText = text;
    VX_EDITOR_INFO_STREAM("Saved document: " << m_openDocumentPath);
    m_notificationManager.showSuccess("File saved");

    return true;
}

bool Editor::compileOpenDocumentShader()
{
    if (m_openDocumentPath.empty())
        return false;

    if (!engine::shaders::ShaderCompiler::isCompilableShaderSource(m_openDocumentPath))
    {
        m_notificationManager.showWarning("This file is not a compilable shader");
        return false;
    }

    if (m_textEditor.GetText() != m_openDocumentSavedText)
    {
        if (!saveOpenDocument())
            return false;
    }

    std::string compileError;
    if (!engine::shaders::ShaderCompiler::compileFileToSpv(m_openDocumentPath, &compileError))
    {
        VX_EDITOR_ERROR_STREAM(compileError);
        m_notificationManager.showError("Shader compile failed");
        return false;
    }

    m_pendingShaderReloadRequest = true;
    VX_EDITOR_INFO_STREAM("Compiled shader: " << m_openDocumentPath << " -> " << (m_openDocumentPath.string() + ".spv"));
    m_notificationManager.showSuccess("Shader compiled and reload requested");
    return true;
}

void Editor::setDocumentLanguageFromPath(const std::filesystem::path &path)
{
    const std::string extension = toLowerCopy(path.extension().string());

    if (engine::shaders::ShaderCompiler::isCompilableShaderSource(path))
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::GLSL());
        return;
    }

    if (extension == ".hlsl" || extension == ".fx")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::HLSL());
        return;
    }

    if (extension == ".cpp" || extension == ".cxx" || extension == ".cc" || extension == ".c" ||
        extension == ".hpp" || extension == ".hh" || extension == ".hxx" || extension == ".h")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        return;
    }

    if (extension == ".json" || extension == ".scene")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(jsonLanguageDefinition());
        return;
    }

    if (extension == ".ini" || extension == ".cfg" || extension == ".toml" ||
        extension == ".yaml" || extension == ".yml" || extension == ".elixproject")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(iniLanguageDefinition());
        return;
    }

    if (extension == ".sql")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::SQL());
        return;
    }

    if (extension == ".lua")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
        return;
    }

    if (extension == ".cmake")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::C());
        return;
    }

    m_textEditor.SetColorizerEnable(false);
    m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::C());
}

void Editor::setSelectedEntity(engine::Entity *entity)
{
    if (m_selectedEntity != entity)
        m_selectedMeshSlot.reset();

    m_selectedEntity = entity;

    if (m_selectedEntity)
        VX_EDITOR_DEBUG_STREAM("Selected entity: " << m_selectedEntity->getName() << " (id: " << m_selectedEntity->getId() << ")");
    else
        VX_EDITOR_DEBUG_STREAM("Selection cleared");
}

void Editor::focusSelectedEntity()
{
    if (!m_selectedEntity || !m_editorCamera)
        return;

    auto transformComponent = m_selectedEntity->getComponent<engine::Transform3DComponent>();
    const glm::mat4 model = transformComponent ? transformComponent->getMatrix() : glm::mat4(1.0f);

    glm::vec3 localMin{0.0f};
    glm::vec3 localMax{0.0f};
    bool hasBounds = false;

    if (auto staticMeshComponent = m_selectedEntity->getComponent<engine::StaticMeshComponent>())
        hasBounds = computeLocalBoundsFromMeshes(staticMeshComponent->getMeshes(), localMin, localMax);
    else if (auto skeletalMeshComponent = m_selectedEntity->getComponent<engine::SkeletalMeshComponent>())
        hasBounds = computeLocalBoundsFromMeshes(skeletalMeshComponent->getMeshes(), localMin, localMax);

    glm::vec3 localCenter{0.0f};
    float localRadius = 0.5f;

    if (hasBounds)
    {
        localCenter = (localMin + localMax) * 0.5f;
        localRadius = std::max(0.5f * glm::length(localMax - localMin), 0.1f);
    }

    const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));
    const float maxScale = std::max(
        {glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])), glm::length(glm::vec3(model[2])), 0.001f});
    const float worldRadius = std::max(localRadius * maxScale, 0.25f);

    const float halfFovRadians = glm::radians(std::max(m_editorCamera->getFOV() * 0.5f, 1.0f));
    const float focusDistance = std::max((worldRadius / std::tan(halfFovRadians)) * 1.35f, 1.0f);

    glm::vec3 forward = m_editorCamera->getForward();
    if (glm::length(forward) <= 0.0001f)
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    else
        forward = glm::normalize(forward);

    const glm::vec3 newPosition = worldCenter - forward * focusDistance;
    m_editorCamera->setPosition(newPosition);

    glm::vec3 lookDirection = worldCenter - newPosition;
    if (glm::length(lookDirection) > 0.0001f)
    {
        lookDirection = glm::normalize(lookDirection);
        const float yaw = glm::degrees(std::atan2(lookDirection.z, lookDirection.x));
        const float pitch = glm::degrees(std::asin(glm::clamp(lookDirection.y, -1.0f, 1.0f)));

        m_editorCamera->setYaw(yaw);
        m_editorCamera->setPitch(pitch);
    }
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

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        setSelectedEntity(nullptr);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && m_currentMode != EditorMode::EDIT)
        changeMode(EditorMode::EDIT);

    if (isCtrlDown && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        auto project = m_currentProject.lock();
        if (m_scene && project)
        {
            m_notificationManager.showInfo("Scene saved");
            m_scene->saveSceneToFile(project->entryScene);
            VX_EDITOR_INFO_STREAM("Scene saved to: " << project->entryScene);
        }
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

void Editor::openMaterialEditor(const std::filesystem::path &path)
{
    for (auto &mat : m_openMaterialEditors)
    {
        if (mat.path == path)
        {
            mat.open = true;
            return;
        }
    }

    OpenMaterialEditor editor;
    editor.path = path;
    editor.open = true;
    editor.dirty = false;
    m_openMaterialEditors.push_back(std::move(editor));
}

engine::Texture::SharedPtr Editor::ensureProjectTextureLoaded(const std::string &texturePath)
{
    if (texturePath.empty())
        return nullptr;

    auto project = m_currentProject.lock();
    if (!project)
        return nullptr;

    auto it = project->cache.texturesByPath.find(texturePath);
    if (it != project->cache.texturesByPath.end() && it->second.gpu)
        return it->second.gpu;

    auto texture = std::make_shared<engine::Texture>();
    if (!texture->load(texturePath))
    {
        VX_EDITOR_ERROR_STREAM("Failed to load texture for material: " << texturePath << '\n');
        return nullptr;
    }

    auto &record = project->cache.texturesByPath[texturePath];
    record.path = texturePath;
    record.gpu = texture;
    record.loaded = true;

    return texture;
}

bool Editor::saveMaterialToDisk(const std::filesystem::path &path, const engine::CPUMaterial &cpuMaterial)
{
    nlohmann::json json;

    json["name"] = cpuMaterial.name.empty() ? path.stem().string() : cpuMaterial.name;
    json["texture_path"] = cpuMaterial.albedoTexture;
    json["normal_texture"] = cpuMaterial.normalTexture;
    json["orm_texture"] = cpuMaterial.ormTexture;
    json["emissive_texture"] = cpuMaterial.emissiveTexture;
    json["color"] = {cpuMaterial.baseColorFactor.r, cpuMaterial.baseColorFactor.g, cpuMaterial.baseColorFactor.b, cpuMaterial.baseColorFactor.a};
    json["emissive"] = {cpuMaterial.emissiveFactor.r, cpuMaterial.emissiveFactor.g, cpuMaterial.emissiveFactor.b};
    json["metallic"] = cpuMaterial.metallicFactor;
    json["roughness"] = cpuMaterial.roughnessFactor;
    json["ao_strength"] = cpuMaterial.aoStrength;
    json["normal_scale"] = cpuMaterial.normalScale;
    json["alpha_cutoff"] = cpuMaterial.alphaCutoff;
    json["flags"] = cpuMaterial.flags;
    json["uv_scale"] = {cpuMaterial.uvScale.x, cpuMaterial.uvScale.y};
    json["uv_offset"] = {cpuMaterial.uvOffset.x, cpuMaterial.uvOffset.y};

    std::ofstream file(path);
    if (!file.is_open())
    {
        VX_EDITOR_ERROR_STREAM("Failed to open material for writing: " << path << '\n');
        return false;
    }

    file << std::setw(4) << json << '\n';

    if (!file.good())
    {
        VX_EDITOR_ERROR_STREAM("Failed to write material file: " << path << '\n');
        return false;
    }

    return true;
}

bool Editor::reloadMaterialFromDisk(const std::filesystem::path &path)
{
    auto project = m_currentProject.lock();
    if (!project)
        return false;

    auto materialAsset = engine::AssetsLoader::loadMaterial(path.string());
    if (!materialAsset.has_value())
    {
        VX_EDITOR_ERROR_STREAM("Failed to reload material asset from disk: " << path << '\n');
        return false;
    }

    auto cpuMaterial = materialAsset.value().material;
    if (cpuMaterial.name.empty())
        cpuMaterial.name = path.stem().string();

    auto &record = project->cache.materialsByPath[path.string()];
    record.path = path.string();
    record.cpuData = cpuMaterial;

    if (!record.gpu)
        record.gpu = engine::Material::create(ensureProjectTextureLoaded(cpuMaterial.albedoTexture));

    if (!record.gpu)
        return false;

    record.gpu->setAlbedoTexture(ensureProjectTextureLoaded(cpuMaterial.albedoTexture));
    record.gpu->setNormalTexture(ensureProjectTextureLoaded(cpuMaterial.normalTexture));
    record.gpu->setOrmTexture(ensureProjectTextureLoaded(cpuMaterial.ormTexture));
    record.gpu->setEmissiveTexture(ensureProjectTextureLoaded(cpuMaterial.emissiveTexture));
    record.gpu->setBaseColorFactor(cpuMaterial.baseColorFactor);
    record.gpu->setEmissiveFactor(cpuMaterial.emissiveFactor);
    record.gpu->setMetallic(cpuMaterial.metallicFactor);
    record.gpu->setRoughness(cpuMaterial.roughnessFactor);
    record.gpu->setAoStrength(cpuMaterial.aoStrength);
    record.gpu->setNormalScale(cpuMaterial.normalScale);
    record.gpu->setAlphaCutoff(cpuMaterial.alphaCutoff);
    record.gpu->setFlags(cpuMaterial.flags);
    record.gpu->setUVScale(cpuMaterial.uvScale);
    record.gpu->setUVOffset(cpuMaterial.uvOffset);

    record.texture = ensureProjectTextureLoaded(cpuMaterial.albedoTexture);

    return true;
}

engine::Material::SharedPtr Editor::ensureMaterialLoaded(const std::string &materialPath)
{
    if (materialPath.empty())
        return nullptr;

    auto project = m_currentProject.lock();
    if (!project)
        return nullptr;

    auto it = project->cache.materialsByPath.find(materialPath);
    if (it != project->cache.materialsByPath.end() && it->second.gpu)
        return it->second.gpu;

    m_assetsPreviewSystem.getOrRequestMaterialPreview(materialPath);

    it = project->cache.materialsByPath.find(materialPath);
    if (it != project->cache.materialsByPath.end() && it->second.gpu)
        return it->second.gpu;

    return nullptr;
}

bool Editor::applyMaterialToSelectedEntity(const std::string &materialPath, std::optional<size_t> slot)
{
    if (!m_selectedEntity)
    {
        VX_EDITOR_WARNING_STREAM("Material apply failed. No selected entity.");
        return false;
    }

    auto staticMeshComponent = m_selectedEntity->getComponent<engine::StaticMeshComponent>();
    auto skeletalMeshComponent = m_selectedEntity->getComponent<engine::SkeletalMeshComponent>();

    if (!staticMeshComponent && !skeletalMeshComponent)
    {
        VX_EDITOR_WARNING_STREAM("Material apply failed for entity '" << m_selectedEntity->getName() << "'. Entity has no mesh component.");
        return false;
    }

    auto material = ensureMaterialLoaded(materialPath);
    if (!material)
    {
        VX_EDITOR_ERROR_STREAM("Material apply failed. Could not load material: " << materialPath);
        return false;
    }

    const size_t slotCount = staticMeshComponent ? staticMeshComponent->getMaterialSlotCount() : skeletalMeshComponent->getMaterialSlotCount();
    std::optional<size_t> resolvedSlot = slot;

    if (!resolvedSlot.has_value() && m_selectedMeshSlot.has_value() && m_selectedMeshSlot.value() < slotCount)
        resolvedSlot = static_cast<size_t>(m_selectedMeshSlot.value());

    if (resolvedSlot.has_value())
    {
        if (resolvedSlot.value() >= slotCount)
        {
            VX_EDITOR_WARNING_STREAM("Material apply failed. Slot " << resolvedSlot.value() << " is out of range for entity '" << m_selectedEntity->getName() << "'.");
            return false;
        }

        if (staticMeshComponent)
        {
            staticMeshComponent->setMaterialOverride(resolvedSlot.value(), material);
            staticMeshComponent->setMaterialOverridePath(resolvedSlot.value(), materialPath);
        }
        else
        {
            skeletalMeshComponent->setMaterialOverride(resolvedSlot.value(), material);
            skeletalMeshComponent->setMaterialOverridePath(resolvedSlot.value(), materialPath);
        }

        VX_EDITOR_INFO_STREAM("Applied material '" << materialPath << "' to entity '" << m_selectedEntity->getName() << "' slot " << resolvedSlot.value());
        return true;
    }

    for (size_t index = 0; index < slotCount; ++index)
    {
        if (staticMeshComponent)
        {
            staticMeshComponent->setMaterialOverride(index, material);
            staticMeshComponent->setMaterialOverridePath(index, materialPath);
        }
        else
        {
            skeletalMeshComponent->setMaterialOverride(index, material);
            skeletalMeshComponent->setMaterialOverridePath(index, materialPath);
        }
    }

    VX_EDITOR_INFO_STREAM("Applied material '" << materialPath << "' to all slots of entity '" << m_selectedEntity->getName() << "'.");
    return true;
}

bool Editor::spawnEntityFromModelAsset(const std::string &assetPath)
{
    if (!m_scene)
    {
        VX_EDITOR_ERROR_STREAM("Spawn model failed. Scene is null.");
        return false;
    }

    auto modelAsset = engine::AssetsLoader::loadModel(assetPath);
    if (!modelAsset.has_value())
    {
        VX_EDITOR_ERROR_STREAM("Failed to load model asset: " << assetPath);
        return false;
    }

    const std::filesystem::path path(assetPath);
    const std::string entityName = path.stem().empty() ? "Model" : path.stem().string();

    auto entity = m_scene->addEntity(entityName);
    const auto &model = modelAsset.value();

    if (model.skeleton.has_value())
    {
        auto *skeletalMeshComponent = entity->addComponent<engine::SkeletalMeshComponent>(model.meshes, model.skeleton.value());

        if (!model.animations.empty())
        {
            auto *animatorComponent = entity->addComponent<engine::AnimatorComponent>();
            animatorComponent->setAnimations(model.animations, &skeletalMeshComponent->getSkeleton());
            animatorComponent->setSelectedAnimationIndex(0);
        }
    }
    else
        entity->addComponent<engine::StaticMeshComponent>(model.meshes);

    if (auto transform = entity->getComponent<engine::Transform3DComponent>())
    {
        glm::vec3 spawnPosition(0.0f);

        if (m_editorCamera)
            spawnPosition = m_editorCamera->getPosition() + m_editorCamera->getForward() * 3.0f;

        transform->setPosition(spawnPosition);
    }

    setSelectedEntity(entity.get());

    VX_EDITOR_INFO_STREAM("Spawned entity '" << entityName << "' from model: " << assetPath);
    m_notificationManager.showSuccess("Spawned model: " + entityName);

    return true;
}

void Editor::addPrimitiveEntity(const std::string &primitiveName)
{
    if (!m_scene)
    {
        VX_EDITOR_ERROR_STREAM("Add primitive failed. Scene is null.");
        return;
    }

    auto entity = m_scene->addEntity(primitiveName);
    std::vector<engine::CPUMesh> meshes;

    if (primitiveName == "Cube")
    {
        auto mesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(engine::cube::vertices, engine::cube::indices);
        mesh.name = "Cube";
        meshes.push_back(mesh);
    }
    else if (primitiveName == "Sphere")
    {
        std::vector<engine::vertex::Vertex3D> vertices;
        std::vector<uint32_t> indices;
        engine::circle::genereteVerticesAndIndices(vertices, indices);
        auto mesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(vertices, indices);
        mesh.name = "Sphere";
        meshes.push_back(mesh);
    }

    if (!meshes.empty())
    {
        entity->addComponent<engine::StaticMeshComponent>(meshes);

        if (auto transform = entity->getComponent<engine::Transform3DComponent>())
        {
            glm::vec3 spawnPosition(0.0f);

            if (m_editorCamera)
                spawnPosition = m_editorCamera->getPosition() + m_editorCamera->getForward() * 3.0f;

            transform->setPosition(spawnPosition);
        }
    }

    setSelectedEntity(entity.get());

    VX_EDITOR_INFO_STREAM("Added primitive entity: " << primitiveName);
    m_notificationManager.showSuccess("Added primitive: " + primitiveName);
}

void Editor::drawDetails()
{
    ImGui::Begin("Details");

    if (!m_selectedEntity)
    {
        ImGui::Text("Select an object to view details");
        return ImGui::End();
    }

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
                const auto &meshes = staticComponent->getMeshes();

                for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
                {
                    const auto &mesh = meshes[meshIndex];
                    const bool isPickedMeshSlot = m_selectedMeshSlot.has_value() && m_selectedMeshSlot.value() == meshIndex;
                    std::string overridePath = staticComponent->getMaterialOverridePath(meshIndex);
                    const bool hasOverride = !overridePath.empty();

                    std::string materialLabel = hasOverride
                                                    ? std::filesystem::path(overridePath).filename().string()
                                                    : std::string("<Default>");
                    const std::string meshName = mesh.name.empty() ? ("Mesh_" + std::to_string(meshIndex)) : mesh.name;

                    ImGui::PushID(static_cast<int>(meshIndex));
                    ImGui::Text("Slot %zu (%s)", meshIndex, meshName.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", materialLabel.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", isPickedMeshSlot ? "[picked]" : "");

                    VkDescriptorSet previewDescriptorSet = m_assetsPreviewSystem.getPlaceholder();
                    if (hasOverride)
                        previewDescriptorSet = m_assetsPreviewSystem.getOrRequestMaterialPreview(overridePath);

                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, isPickedMeshSlot ? 2.0f : 1.0f);
                    if (isPickedMeshSlot)
                        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 170, 40, 255));
                    ImGui::ImageButton("##MaterialPreview", previewDescriptorSet, ImVec2(52.0f, 52.0f));
                    if (isPickedMeshSlot)
                        ImGui::PopStyleColor();
                    ImGui::PopStyleVar();

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                        m_selectedMeshSlot = static_cast<uint32_t>(meshIndex);

                    if (!hasOverride && ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        if (mesh.material.albedoTexture.empty())
                            ImGui::TextUnformatted("No override material");
                        else
                            ImGui::Text("Mesh Albedo: %s", mesh.material.albedoTexture.c_str());
                        ImGui::EndTooltip();
                    }

                    if (hasOverride && ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Material: %s", overridePath.c_str());
                        ImGui::TextUnformatted("Double-click to open material editor");
                        ImGui::EndTooltip();
                    }

                    if (hasOverride && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        openMaterialEditor(overridePath);

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                            const std::string extension = std::filesystem::path(droppedPath).extension().string();

                            if (extension == ".elixmat")
                            {
                                if (applyMaterialToSelectedEntity(droppedPath, meshIndex))
                                    m_notificationManager.showSuccess("Material applied to slot");
                                else
                                    m_notificationManager.showError("Failed to apply material");
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    ImGui::TextUnformatted("Override Material");

                    if (hasOverride)
                        ImGui::TextWrapped("%s", overridePath.c_str());
                    else
                        ImGui::TextDisabled("<None>");

                    if (hasOverride && ImGui::Button("Open Material Editor"))
                        openMaterialEditor(overridePath);

                    if (hasOverride && ImGui::Button("Clear Override"))
                    {
                        staticComponent->clearMaterialOverride(meshIndex);
                    }

                    ImGui::EndGroup();
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
        }
        else if (auto skeletalMeshComponent = dynamic_cast<engine::SkeletalMeshComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Skeletal mesh", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto &meshes = skeletalMeshComponent->getMeshes();

                for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
                {
                    const auto &mesh = meshes[meshIndex];
                    const bool isPickedMeshSlot = m_selectedMeshSlot.has_value() && m_selectedMeshSlot.value() == meshIndex;
                    std::string overridePath = skeletalMeshComponent->getMaterialOverridePath(meshIndex);
                    const bool hasOverride = !overridePath.empty();

                    std::string materialLabel = hasOverride
                                                    ? std::filesystem::path(overridePath).filename().string()
                                                    : std::string("<Default>");
                    const std::string meshName = mesh.name.empty() ? ("Mesh_" + std::to_string(meshIndex)) : mesh.name;

                    ImGui::PushID(static_cast<int>(meshIndex));
                    ImGui::Text("Slot %zu (%s)", meshIndex, meshName.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", materialLabel.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", isPickedMeshSlot ? "[picked]" : "");

                    VkDescriptorSet previewDescriptorSet = m_assetsPreviewSystem.getPlaceholder();
                    if (hasOverride)
                        previewDescriptorSet = m_assetsPreviewSystem.getOrRequestMaterialPreview(overridePath);

                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, isPickedMeshSlot ? 2.0f : 1.0f);
                    if (isPickedMeshSlot)
                        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 170, 40, 255));
                    ImGui::ImageButton("##MaterialPreview", previewDescriptorSet, ImVec2(52.0f, 52.0f));
                    if (isPickedMeshSlot)
                        ImGui::PopStyleColor();
                    ImGui::PopStyleVar();

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                        m_selectedMeshSlot = static_cast<uint32_t>(meshIndex);

                    if (!hasOverride && ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        if (mesh.material.albedoTexture.empty())
                            ImGui::TextUnformatted("No override material");
                        else
                            ImGui::Text("Mesh Albedo: %s", mesh.material.albedoTexture.c_str());
                        ImGui::EndTooltip();
                    }

                    if (hasOverride && ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Material: %s", overridePath.c_str());
                        ImGui::TextUnformatted("Double-click to open material editor");
                        ImGui::EndTooltip();
                    }

                    if (hasOverride && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        openMaterialEditor(overridePath);

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                            const std::string extension = std::filesystem::path(droppedPath).extension().string();

                            if (extension == ".elixmat")
                            {
                                if (applyMaterialToSelectedEntity(droppedPath, meshIndex))
                                    m_notificationManager.showSuccess("Material applied to slot");
                                else
                                    m_notificationManager.showError("Failed to apply material");
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    ImGui::TextUnformatted("Override Material");

                    if (hasOverride)
                        ImGui::TextWrapped("%s", overridePath.c_str());
                    else
                        ImGui::TextDisabled("<None>");

                    if (hasOverride && ImGui::Button("Open Material Editor"))
                        openMaterialEditor(overridePath);

                    if (hasOverride && ImGui::Button("Clear Override"))
                    {
                        skeletalMeshComponent->clearMaterialOverride(meshIndex);
                    }

                    ImGui::EndGroup();
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
        }
        else if (auto animatorComponent = dynamic_cast<engine::AnimatorComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto &animations = animatorComponent->getAnimations();

                if (animations.empty())
                {
                    ImGui::TextDisabled("No animation clips imported for this model");
                }
                else
                {
                    int selectedAnimationIndex = animatorComponent->getSelectedAnimationIndex();
                    if (selectedAnimationIndex < 0 || selectedAnimationIndex >= static_cast<int>(animations.size()))
                    {
                        selectedAnimationIndex = 0;
                        animatorComponent->setSelectedAnimationIndex(selectedAnimationIndex);
                    }

                    const std::string fallbackName = "<Unnamed>";
                    const std::string &selectedName = animations[selectedAnimationIndex].name.empty() ? fallbackName : animations[selectedAnimationIndex].name;

                    if (ImGui::BeginCombo("Clip", selectedName.c_str()))
                    {
                        for (int animationIndex = 0; animationIndex < static_cast<int>(animations.size()); ++animationIndex)
                        {
                            const std::string &animationName = animations[animationIndex].name.empty() ? fallbackName : animations[animationIndex].name;
                            const bool isSelected = animationIndex == selectedAnimationIndex;

                            if (ImGui::Selectable(animationName.c_str(), isSelected))
                            {
                                selectedAnimationIndex = animationIndex;
                                animatorComponent->setSelectedAnimationIndex(selectedAnimationIndex);
                            }

                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }

                        ImGui::EndCombo();
                    }

                    bool isLooped = animatorComponent->isAnimationLooped();
                    if (ImGui::Checkbox("Loop", &isLooped))
                        animatorComponent->setAnimationLooped(isLooped);

                    bool isPaused = animatorComponent->isAnimationPaused();
                    if (ImGui::Checkbox("Paused", &isPaused))
                        animatorComponent->setAnimationPaused(isPaused);

                    float animationSpeed = animatorComponent->getAnimationSpeed();
                    if (ImGui::DragFloat("Speed", &animationSpeed, 0.01f, 0.01f, 4.0f, "%.2f"))
                        animatorComponent->setAnimationSpeed(animationSpeed);

                    if (animatorComponent->isAnimationPlaying())
                    {
                        const float duration = animatorComponent->getCurrentAnimationDuration();
                        float currentTime = animatorComponent->getCurrentTime();

                        if (duration > 0.0f)
                        {
                            if (ImGui::SliderFloat("Time", &currentTime, 0.0f, duration))
                                animatorComponent->setCurrentTime(currentTime);

                            ImGui::Text("Time: %.2f / %.2f", currentTime, duration);
                        }
                    }

                    if (ImGui::Button("Play Selected"))
                        animatorComponent->playAnimationByIndex(static_cast<size_t>(selectedAnimationIndex), animatorComponent->isAnimationLooped());

                    ImGui::SameLine();

                    if (ImGui::Button("Stop"))
                        animatorComponent->stopAnimation();

                    if (m_currentMode != EditorMode::EDIT)
                        ImGui::TextDisabled("Animation preview updates in Edit mode");
                }
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
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();
    const bool imageHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (imageHovered && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
            std::string extension = std::filesystem::path(droppedPath).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

            if (extension == ".elixmat")
            {
                if (applyMaterialToSelectedEntity(droppedPath))
                    m_notificationManager.showSuccess("Applied material to selected mesh");
                else
                    m_notificationManager.showWarning("Select a mesh entity to apply this material");
            }
            else if (extension == ".fbx" || extension == ".obj")
            {
                if (spawnEntityFromModelAsset(droppedPath))
                    m_notificationManager.showSuccess("Model spawned in scene");
                else
                    m_notificationManager.showError("Failed to spawn model");
            }
        }
        ImGui::EndDragDropTarget();
    }

    constexpr float rightMouseDragThresholdPx = 4.0f;
    constexpr double rightMouseHoldToCaptureSec = 0.12;

    if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !m_isViewportMouseCaptured && !ImGui::IsPopupOpen("ViewportContextMenu"))
    {
        m_isViewportRightMousePendingContext = true;
        m_viewportRightMouseDownTime = ImGui::GetTime();

        const ImVec2 mousePos = ImGui::GetMousePos();
        m_viewportRightMouseDownX = mousePos.x;
        m_viewportRightMouseDownY = mousePos.y;
    }

    bool shouldBeginViewportMouseCapture = false;

    if (m_isViewportRightMousePendingContext && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        const ImVec2 mousePos = ImGui::GetMousePos();
        const float dx = mousePos.x - m_viewportRightMouseDownX;
        const float dy = mousePos.y - m_viewportRightMouseDownY;
        const float dragDistanceSq = dx * dx + dy * dy;
        const double heldSec = ImGui::GetTime() - m_viewportRightMouseDownTime;

        if (dragDistanceSq > (rightMouseDragThresholdPx * rightMouseDragThresholdPx) || heldSec >= rightMouseHoldToCaptureSec)
        {
            shouldBeginViewportMouseCapture = true;
            m_isViewportRightMousePendingContext = false;
        }
    }

    if (m_isViewportRightMousePendingContext && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool releasedInsideViewportImage = mouse.x >= imageMin.x && mouse.x < imageMax.x &&
                                                 mouse.y >= imageMin.y && mouse.y < imageMax.y;

        if (releasedInsideViewportImage && !m_isViewportMouseCaptured)
            ImGui::OpenPopup("ViewportContextMenu");

        m_isViewportRightMousePendingContext = false;
    }

    if (ImGui::BeginPopup("ViewportContextMenu"))
    {
        ImGui::TextUnformatted("Create");
        ImGui::Separator();

        if (ImGui::MenuItem("Cube"))
        {
            addPrimitiveEntity("Cube");
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem("Sphere"))
        {
            addPrimitiveEntity("Sphere");
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

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

    auto window = core::VulkanContext::getContext()->getSwapchain()->getWindow();
    GLFWwindow *windowHandler = window->getRawHandler();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && m_viewportSizeX > 0 && m_viewportSizeY > 0)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float imageWidth = imageMax.x - imageMin.x;
        const float imageHeight = imageMax.y - imageMin.y;

        if (imageWidth > 0.0f && imageHeight > 0.0f &&
            mouse.x >= imageMin.x && mouse.x < imageMax.x &&
            mouse.y >= imageMin.y && mouse.y < imageMax.y)
        {
            const float u = std::clamp((mouse.x - imageMin.x) / imageWidth, 0.0f, 0.999999f);
            const float v = std::clamp((mouse.y - imageMin.y) / imageHeight, 0.0f, 0.999999f);

            m_pendingPickX = static_cast<uint32_t>(u * static_cast<float>(m_viewportSizeX));
            m_pendingPickY = static_cast<uint32_t>(v * static_cast<float>(m_viewportSizeY));
            m_hasPendingObjectPick = true;
        }
    }

    if (m_selectedEntity && ImGui::IsKeyPressed(ImGuiKey_F, false))
        focusSelectedEntity();

    const bool shouldCaptureViewportMouse = !ImGui::IsPopupOpen("ViewportContextMenu") &&
                                            (m_isViewportMouseCaptured
                                                 ? ImGui::IsMouseDown(ImGuiMouseButton_Right)
                                                 : shouldBeginViewportMouseCapture);

    if (shouldCaptureViewportMouse && !m_isViewportMouseCaptured)
    {
        m_isViewportRightMousePendingContext = false;
        glfwGetCursorPos(windowHandler, &m_capturedMouseRestoreX, &m_capturedMouseRestoreY);
        glfwSetInputMode(windowHandler, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

#if defined(GLFW_RAW_MOUSE_MOTION)
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(windowHandler, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
#endif

        m_isViewportMouseCaptured = true;
    }
    else if (!shouldCaptureViewportMouse && m_isViewportMouseCaptured)
    {
#if defined(GLFW_RAW_MOUSE_MOTION)
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(windowHandler, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
#endif

        glfwSetInputMode(windowHandler, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetCursorPos(windowHandler, m_capturedMouseRestoreX, m_capturedMouseRestoreY);
        m_isViewportMouseCaptured = false;
    }

    if (m_isViewportMouseCaptured)
    {
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

void Editor::processPendingObjectSelection()
{
    if (!m_hasPendingObjectPick || !m_objectIdColorImage || !m_scene || !m_entityIdBuffer)
        return;

    auto image = m_objectIdColorImage->getImage();

    auto commandBuffer = core::CommandBuffer::createShared(core::VulkanContext::getContext()->getGraphicsCommandPool());
    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    const VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    engine::utilities::ImageUtilities::insertImageMemoryBarrier(
        *image,
        *commandBuffer,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        subresourceRange);

    engine::utilities::ImageUtilities::copyImageToBuffer(
        *image,
        *m_entityIdBuffer,
        *commandBuffer,
        {static_cast<int32_t>(m_pendingPickX), static_cast<int32_t>(m_pendingPickY), 0});

    engine::utilities::ImageUtilities::insertImageMemoryBarrier(
        *image,
        *commandBuffer,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        subresourceRange);

    commandBuffer->end();
    commandBuffer->submit(core::VulkanContext::getContext()->getGraphicsQueue());
    vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());

    uint32_t *data = nullptr;
    m_entityIdBuffer->map(reinterpret_cast<void *&>(data));

    const uint32_t selectedObjectId = data ? data[0] : 0u;
    m_entityIdBuffer->unmap();

    if (selectedObjectId == engine::render::OBJECT_ID_NONE)
    {
        setSelectedEntity(nullptr);
        m_selectedMeshSlot.reset();
        m_hasPendingObjectPick = false;
        return;
    }

    const uint32_t encodedEntityId = engine::render::decodeEntityEncoded(selectedObjectId);
    const uint32_t encodedMeshSlot = engine::render::decodeMeshEncoded(selectedObjectId);

    if (encodedEntityId == 0u)
    {
        setSelectedEntity(nullptr);
        m_selectedMeshSlot.reset();
        m_hasPendingObjectPick = false;
        return;
    }

    engine::Entity *pickedEntity = m_scene->getEntityById(encodedEntityId - 1u);
    setSelectedEntity(pickedEntity);

    if (!pickedEntity || encodedMeshSlot == 0u)
    {
        m_selectedMeshSlot.reset();
        m_hasPendingObjectPick = false;
        return;
    }

    const uint32_t decodedMeshSlot = encodedMeshSlot - 1u;

    size_t meshSlotCount = 0;

    if (auto staticMeshComponent = pickedEntity->getComponent<engine::StaticMeshComponent>())
        meshSlotCount = staticMeshComponent->getMaterialSlotCount();
    else if (auto skeletalMeshComponent = pickedEntity->getComponent<engine::SkeletalMeshComponent>())
        meshSlotCount = skeletalMeshComponent->getMaterialSlotCount();

    if (decodedMeshSlot < meshSlotCount)
        m_selectedMeshSlot = decodedMeshSlot;
    else
        m_selectedMeshSlot.reset();

    m_hasPendingObjectPick = false;
}

void Editor::drawTerminal()
{
    if (!m_showTerminal)
        return;

    ImGui::Begin("Terminal with logs", &m_showTerminal);

    auto *logger = core::Logger::getDefaultLogger();
    if (!logger)
    {
        ImGui::TextDisabled("Logger is not initialized");
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear Logs"))
    {
        logger->clearHistory();
        m_terminalLastLogCount = 0;
        m_forceTerminalScrollToBottom = true;
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Auto-scroll", &m_terminalAutoScroll) && m_terminalAutoScroll)
        m_forceTerminalScrollToBottom = true;

    ImGui::SameLine();
    ImGui::Checkbox("Clear input on run", &m_terminalClearInputOnSubmit);

    auto drawLayerToggle = [this](const char *label, int bit)
    {
        bool enabled = (m_terminalSelectedLayerMask & (1 << bit)) != 0;
        if (ImGui::Checkbox(label, &enabled))
        {
            if (enabled)
                m_terminalSelectedLayerMask |= (1 << bit);
            else
                m_terminalSelectedLayerMask &= ~(1 << bit);
        }
    };

    drawLayerToggle("Core", 0);
    ImGui::SameLine();
    drawLayerToggle("Engine", 1);
    ImGui::SameLine();
    drawLayerToggle("Editor", 2);
    ImGui::SameLine();
    drawLayerToggle("Developer", 3);
    ImGui::SameLine();
    drawLayerToggle("User", 4);

    const float commandLineHeight = ImGui::GetFrameHeightWithSpacing() * 2.2f;
    if (ImGui::BeginChild("TerminalLogRegion", ImVec2(0, -commandLineHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
    {
        const auto logs = logger->getHistorySnapshot();
        const size_t currentLogCount = logs.size();
        const bool hasNewLogs = currentLogCount > m_terminalLastLogCount;
        const bool historyShrank = currentLogCount < m_terminalLastLogCount;

        const float scrollYBeforeRender = ImGui::GetScrollY();
        const float scrollMaxBeforeRender = ImGui::GetScrollMaxY();
        const bool wasAtBottom = (scrollMaxBeforeRender <= 0.0f) || (scrollYBeforeRender >= scrollMaxBeforeRender - 1.0f);

        for (const auto &logMessage : logs)
        {
            int layerBit = 3;
            switch (logMessage.layer)
            {
            case core::Logger::LogLayer::Core:
                layerBit = 0;
                break;
            case core::Logger::LogLayer::Engine:
                layerBit = 1;
                break;
            case core::Logger::LogLayer::Editor:
                layerBit = 2;
                break;
            case core::Logger::LogLayer::Developer:
                layerBit = 3;
                break;
            case core::Logger::LogLayer::User:
                layerBit = 4;
                break;
            }

            if ((m_terminalSelectedLayerMask & (1 << layerBit)) == 0)
                continue;

            ImVec4 color = ImVec4(0.85f, 0.88f, 0.93f, 1.0f);
            switch (logMessage.level)
            {
            case core::Logger::LogLevel::DEBUG:
                color = ImVec4(0.45f, 0.70f, 1.0f, 1.0f);
                break;
            case core::Logger::LogLevel::INFO:
                color = ImVec4(0.74f, 0.90f, 0.78f, 1.0f);
                break;
            case core::Logger::LogLevel::WARNING:
                color = ImVec4(1.0f, 0.86f, 0.52f, 1.0f);
                break;
            case core::Logger::LogLevel::LOG_LEVEL_ERROR:
                color = ImVec4(1.0f, 0.46f, 0.46f, 1.0f);
                break;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(logMessage.formattedMessage.c_str());
            ImGui::PopStyleColor();
        }

        const bool shouldAutoScroll = m_terminalAutoScroll &&
                                      (m_forceTerminalScrollToBottom || historyShrank || (hasNewLogs && wasAtBottom));

        if (shouldAutoScroll)
            ImGui::SetScrollHereY(1.0f);

        m_terminalLastLogCount = currentLogCount;
        m_forceTerminalScrollToBottom = false;
    }
    ImGui::EndChild();

    ImGui::PushItemWidth(-70.0f);
    const bool submitWithEnter = ImGui::InputText("##TerminalCommand", m_terminalCommandBuffer, sizeof(m_terminalCommandBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();

    ImGui::SameLine();
    const bool submitWithButton = ImGui::Button("Run");

    if (submitWithEnter || submitWithButton)
    {
        std::string command = m_terminalCommandBuffer;

        if (!command.empty())
        {
            command.erase(command.begin(), std::find_if(command.begin(), command.end(), [](unsigned char character)
                                                        { return !std::isspace(character); }));
            command.erase(std::find_if(command.rbegin(), command.rend(), [](unsigned char character)
                                       { return !std::isspace(character); })
                              .base(),
                          command.end());
        }

        if (!command.empty())
        {
            m_forceTerminalScrollToBottom = true;

            if (command == "reload_shaders")
            {
                m_pendingShaderReloadRequest = true;
                VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", "Queued shader reload request");
                m_notificationManager.showInfo("Shader reload queued");
            }
            else if (command == "compile_shaders")
            {
                std::vector<std::string> compileErrors;
                const size_t compiledShaders = engine::shaders::ShaderCompiler::compileDirectoryToSpv("./resources/shaders", &compileErrors);

                for (const auto &error : compileErrors)
                    VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::LOG_LEVEL_ERROR, "Terminal", error);

                if (compiledShaders > 0)
                {
                    m_pendingShaderReloadRequest = true;
                    VX_LOG_STREAM(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal",
                                  "Compiled " << compiledShaders << " shader source files. Reload queued.");
                    m_notificationManager.showSuccess("Shaders compiled");
                }
                else if (compileErrors.empty())
                {
                    VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", "No shader source files were compiled.");
                    m_notificationManager.showInfo("No shader changes to compile");
                }
                else
                    m_notificationManager.showError("Shader compilation failed. Check terminal output.");
            }
            else
            {
                VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", "$ " + command);

                const auto [executionResult, output] = FileHelper::executeCommand(command);

                if (!output.empty())
                {
                    std::stringstream outputStream(output);
                    std::string line;
                    while (std::getline(outputStream, line))
                    {
                        if (!line.empty())
                            VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", line);
                    }
                }

                if (executionResult == 0)
                {
                    VX_LOG_STREAM(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal",
                                  "Command finished successfully. Exit code: " << executionResult);
                    m_notificationManager.showSuccess("Command executed successfully");
                }
                else
                {
                    VX_LOG_STREAM(core::Logger::LogLayer::Developer, core::Logger::LogLevel::LOG_LEVEL_ERROR, "Terminal",
                                  "Command failed. Exit code: " << executionResult);
                    m_notificationManager.showError("Command failed. Check terminal output.");
                }
            }
        }

        if (m_terminalClearInputOnSubmit)
            std::memset(m_terminalCommandBuffer, 0, sizeof(m_terminalCommandBuffer));
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
                addPrimitiveEntity("Cube");
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Sphere"))
            {
                addPrimitiveEntity("Sphere");
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}
ELIX_NESTED_NAMESPACE_END
