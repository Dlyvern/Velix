#ifndef ELIX_IRENDER_GRAPH_PROXY_HPP
#define ELIX_IRENDER_GRAPH_PROXY_HPP

#include "Core/Macros.hpp"

#include <memory>
#include <string>
#include <functional>

#include "Engine/Render/Proxies/RenderGraphProxyData.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IProxy
{
public:
    virtual ~IProxy() = default;
    virtual const std::string& getName() const = 0;
};

template<typename DataType, template<typename...> class StorageType, typename... StorageArgs>
class IRenderGraphProxy : public IProxy
{
public:
    using SharedPtr = std::shared_ptr<IRenderGraphProxy>;
    using UniquePtr = std::unique_ptr<IRenderGraphProxy>;
    
    IRenderGraphProxy(const std::string& name) : m_name(name) {}

    bool isDependedOnSwapChain{false};

    const std::string& getName() const override
    {
        return m_name;
    }

    void addOnSwapChainRecretedFunction(const std::function<void()>& function)
    {
        m_onSwapChainRecreatedFunctions.push_back(function);
    }

    void onSwapChainRecreated()
    {
        for(const auto& function : m_onSwapChainRecreatedFunctions)
            if(function)
                function();
    }

    virtual ~IRenderGraphProxy() override = default;

    StorageType<StorageArgs...> storage;
private:
    std::string m_name;
    std::vector<std::function<void()>> m_onSwapChainRecreatedFunctions;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_IRENDER_GRAPH_PROXY_HPPs