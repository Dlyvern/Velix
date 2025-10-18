#ifndef ELIX_SWAP_CHAIN_TRACKER_HPP
#define ELIX_SWAP_CHAIN_TRACKER_HPP

#include "Core/Macros.hpp"
#include "Core/SwapChain.hpp"

#include <vector>
#include <functional>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class SwapChainTracker
{
public:
    void addDependenciesResource(const std::function<void(const core::SwapChain::SharedPtr swapChain)>& recreationCallback);
    void notifyDependencies(const core::SwapChain::SharedPtr swapChain) const;

private:
    std::vector<std::function<void(const core::SwapChain::SharedPtr swapChain)>> m_dependencies;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SWAP_CHAIN_TRACKER_HPP