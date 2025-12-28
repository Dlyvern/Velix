#ifndef ELIX_CONNECTION_HPP
#define ELIX_CONNECTION_HPP

#include "Core/Macros.hpp"

#include <functional>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Connection
{
public:
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection();

    explicit Connection(const std::function<void(Connection*)>& onDisconnect);

    Connection(Connection&& other) noexcept;

    ~Connection();

    void disconnect();

    void setOnDisconnectCallback(const std::function<void(Connection*)>& onDisconnectCallback);
private:
    std::function<void(Connection*)> m_onDisconnectCallback{nullptr};
};

ELIX_NESTED_NAMESPACE_END


#endif //ELIX_CONNECTION_HPP