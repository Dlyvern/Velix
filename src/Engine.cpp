#include "Engine.hpp"

#include <glad/glad.h>

#include "CameraManager.hpp"
#include "ElixirCore/Physics.hpp"
#include "ElixirCore/WindowsManager.hpp"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "Editor.hpp"
#include "Renderer.hpp"
#include "ElixirCore/SceneManager.hpp"
#include "ElixirCore/ShaderManager.hpp"
#include "ElixirCore/Application.hpp"
#include "ElixirCore/Logger.hpp"
#define IMGUI_ENABLE_DOCKING

bool Engine::run()
{
    try
    {
        init();
    }
    catch (const std::exception &e)
    {
        ELIX_LOG_ERROR("ENGINE_RUN_ERROR: COULD NOT INITIALIZE ENGINE: %s", std::string(e.what()));
        return false;
    }

    float lastFrame = 0.0f;
    float deltaTime = 0.0f;

    while (window::WindowsManager::instance().getCurrentWindow()->isWindowOpened())
    {
    	const float currentFrame = window::MainWindow::getTime();
    	deltaTime = currentFrame - lastFrame;
    	lastFrame = currentFrame;

    	window::MainWindow::pollEvents();

    	physics::PhysicsController::instance().simulate(deltaTime);

    	if (Editor::instance().getState() != Editor::State::Play)
    		if (Editor::instance().m_editorCamera)
    			Editor::instance().m_editorCamera->update(deltaTime);

    	CameraManager::getInstance().getActiveCamera()->update(deltaTime);
    	SceneManager::instance().updateCurrentScene(deltaTime);

    	Renderer::instance().beginFrame();
    	Renderer::instance().endFrame();

    	Editor::instance().update();

    	window::WindowsManager::instance().getCurrentWindow()->swapBuffers();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    elix::Application::instance().shutdown();

    return true;
}

void Engine::init()
{
	elix::Application::instance().init();

	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(window::WindowsManager::instance().getCurrentWindow()->getOpenGLWindow(), &bufferWidth, &bufferHeight);

	window::MainWindow::setViewport(0, 0, bufferWidth, bufferHeight);

    Renderer::instance().initFrameBuffer(bufferWidth, bufferHeight);

    initImgui();

	const auto camera = new Camera();
	Editor::instance().m_editorCamera = camera;
    CameraManager::getInstance().setActiveCamera(camera->getCamera());
    physics::PhysicsController::instance().init();

    ShaderManager::instance().preLoadShaders();
}

void Engine::initImgui()
{
	if (!window::WindowsManager::instance().getCurrentWindow())
		return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplGlfw_InitForOpenGL(window::WindowsManager::instance().getCurrentWindow()->getOpenGLWindow(), true);
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
}
