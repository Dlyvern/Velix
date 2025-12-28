#include "Engine/Signals/Connection.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Connection::Connection() = default;

Connection::Connection(const std::function<void(Connection*)>& onDisconnect) : m_onDisconnectCallback(onDisconnect)
{

}

Connection::Connection(Connection&& other) noexcept
{
    m_onDisconnectCallback = std::move(other.m_onDisconnectCallback);
    other.m_onDisconnectCallback = nullptr;
}

Connection::~Connection()
{
    disconnect();
}

void Connection::disconnect()
{
    if(m_onDisconnectCallback)
        m_onDisconnectCallback(this);
}

void Connection::setOnDisconnectCallback(const std::function<void(Connection*)>& onDisconnectCallback)
{
    m_onDisconnectCallback = onDisconnectCallback;
}

ELIX_NESTED_NAMESPACE_END