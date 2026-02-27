#ifndef ELIX_APPLICATION_LOOP_HPP
#define ELIX_APPLICATION_LOOP_HPP

#include "Core/Macros.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/Window.hpp"

#include "Engine/Runtime/ApplicationConfig.hpp"
#include "Engine/Runtime/IRuntime.hpp"

#include <functional>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ApplicationLoop
{
public:
    using RuntimeFactory = std::function<std::unique_ptr<IRuntime>(const ApplicationConfig &)>;

    int run(const ApplicationConfig &applicationConfig, const RuntimeFactory &runtimeFactory);

private:
    bool preInit(const ApplicationConfig &applicationConfig);
    void loop();
    void shutdown();

    std::unique_ptr<IRuntime> m_runtime{nullptr};

private:
    std::shared_ptr<core::VulkanContext> m_vulkanContext{nullptr};
    platform::Window::SharedPtr m_window{nullptr};
    std::string m_graphicsPipelineCachePath;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_APPLICATION_LOOP_HPP
