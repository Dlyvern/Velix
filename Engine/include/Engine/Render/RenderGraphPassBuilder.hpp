#ifndef ELIX_RENDER_GRAPH_PASS_BUILDER_HPP
#define ELIX_RENDER_GRAPH_PASS_BUILDER_HPP

#include "Core/Macros.hpp"

#include "Engine/Render/Proxies/IRenderGraphProxy.hpp"
#include "Engine/Render/Proxies/ImageRenderGraphProxy.hpp"
#include "Engine/Render/Proxies/RenderPassRenderGraphProxy.hpp"

#include <unordered_map>
#include <stdexcept>
ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class ResourceAccess : uint8_t
{
    None  = 0,
    Read  = 1 << 0,
    Write = 1 << 1
};

inline ResourceAccess operator|(ResourceAccess a, ResourceAccess b)
{
    return static_cast<ResourceAccess>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b)
    );
}

class RenderGraphPassBuilder
{
public:
    using SharedPtr = std::shared_ptr<RenderGraphPassBuilder>;
    using UniquePtr = std::unique_ptr<RenderGraphPassBuilder>;
    using Ptr = RenderGraphPassBuilder*;

    template<typename T, typename... Args>
    std::shared_ptr<T> createProxy(const std::string& name, Args&&... args)
    {
        static_assert(!std::is_abstract_v<T>, "RenderGraph::createProxy() Cannot add abstract component!");
        static_assert(std::is_base_of_v<IProxy, T>, "RenderGraphPassBuilder::createProxy() T must derive from IRenderGraphProxy class");

        if(auto it = m_proxies.find(name); it != m_proxies.end())
            return std::dynamic_pointer_cast<T>(it->second);

        auto proxy = std::make_shared<T>(name, std::forward<Args>(args)...);

        m_proxies[name] = proxy;

        return proxy;
    }

    template<typename ProxyType, typename... Args>
    void buildProxy(ProxyType* proxy, Args&&... args)
    {
        if constexpr (std::is_same_v<ProxyType, ImageRenderGraphProxy>)
            RenderGraphPassBuilder::createImageProxy(std::forward<Args>(args)..., proxy);
        else if constexpr (std::is_same_v<ProxyType, RenderPassRenderGraphProxy>)
            RenderGraphPassBuilder::createRenderPassProxy(std::forward<Args>(args)..., proxy);
        else
            throw std::runtime_error("Unrecognized proxy");
    }

    template<typename ProxyType>
    std::shared_ptr<ProxyType> getProxy(const std::string& name)
    {
        auto it = m_proxies.find(name);
        if(it == m_proxies.end())
            return nullptr;
        return std::dynamic_pointer_cast<ProxyType>(it->second);
    }

    std::unordered_map<std::string, std::shared_ptr<IProxy>>& getProxies()
    {
        return m_proxies;
    }
private:
    static void createImageProxy(VkDevice device, ImageRenderGraphProxy* imageProxy);
    static void createRenderPassProxy(VkDevice device, RenderPassRenderGraphProxy* renderPassProxy);

    std::unordered_map<std::string, std::shared_ptr<IProxy>> m_proxies;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_RENDER_GRAPH_PASS_BUILDER_HPP