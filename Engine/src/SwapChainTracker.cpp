#include "Engine/SwapChainTracker.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void SwapChainTracker::addDependenciesResource(const std::function<void(const core::SwapChain::SharedPtr swapChain)>& recreationCallback)
{
    m_dependencies.push_back(recreationCallback);
}

void SwapChainTracker::notifyDependencies(const core::SwapChain::SharedPtr swapChain) const
{
    for(const auto& resource : m_dependencies)
        if(resource)
            resource(swapChain);
}


ELIX_NESTED_NAMESPACE_END