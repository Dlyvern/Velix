#ifndef ELIX_RENDER_GRAPH_PROXY_DATA_HPP
#define ELIX_RENDER_GRAPH_PROXY_DATA_HPP

#include "Core/Macros.hpp"

#include <vector>
#include <memory>
#include <map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IRenderGraphProxyData
{
public:
    virtual ~IRenderGraphProxyData() = default;
};

template<typename Key, typename Value>
class RenderGraphProxyContainerMapPtrData : public IRenderGraphProxyData
{
public:
    std::map<Key, std::shared_ptr<Value>> data;
};

template<typename T>
class RenderGraphProxyContainerData : public IRenderGraphProxyData
{
public:
    std::vector<T> data;
};

template<typename T>
class RenderGraphProxyContainerPtrData : public IRenderGraphProxyData
{
public:
    std::vector<std::shared_ptr<T>> data;
};

template<typename T>
class RenderGraphProxySingleData : public IRenderGraphProxyData
{
public:
    T data;
};

template<typename T>
class RenderGraphProxySinglePtrData : public IRenderGraphProxyData
{
public:
    std::shared_ptr<T> data;
};


ELIX_NESTED_NAMESPACE_END

#endif //ELIX_RENDER_GRAPH_PROXY_DATA_HPP