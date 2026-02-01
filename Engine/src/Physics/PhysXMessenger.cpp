#include "Engine/Physics/PhysXMessenger.hpp"
#include <functional>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

PhysXMessenger::PhysXMessenger(core::Logger *logger) : m_logger(logger)
{
}

void PhysXMessenger::infoCallback(const char *error, const char *message)
{
    if (m_logger)
    {
        const std::string message = "PhysX " + std::string(error) + ": " + message;
        m_logger->info(message);
    }
}

void PhysXMessenger::warningCallback(const char *error, const char *message)
{
    if (m_logger)
    {
        const std::string message = "PhysX " + std::string(error) + ": " + message;
        m_logger->warning(message);
    }
}

void PhysXMessenger::errorCallback(const char *error, const char *message)
{
    if (m_logger)
    {
        const std::string message = "PhysX " + std::string(error) + ": " + message;
        m_logger->error(message);
    }
}

void PhysXMessenger::reportError(physx::PxErrorCode::Enum code, const char *message, const char *file, const int line)
{
    const char *error = nullptr;

    std::function<void(const char *, const char *)> loggingCallback;

    switch (code)
    {
    case physx::PxErrorCode::eNO_ERROR:
        error = "No Error";
        loggingCallback = std::bind(&PhysXMessenger::infoCallback, this, std::placeholders::_1, std::placeholders::_2);
        break;
    case physx::PxErrorCode::eDEBUG_INFO:
        error = "Debug Info";
        loggingCallback = std::bind(&PhysXMessenger::infoCallback, this, std::placeholders::_1, std::placeholders::_2);
        break;
    case physx::PxErrorCode::eDEBUG_WARNING:
        error = "Debug Warning";
        loggingCallback = std::bind(&PhysXMessenger::warningCallback, this, std::placeholders::_1, std::placeholders::_2);
        break;
    case physx::PxErrorCode::eINVALID_PARAMETER:
        error = "Invalid Parameter";
        loggingCallback = std::bind(&PhysXMessenger::errorCallback, this, std::placeholders::_1, std::placeholders::_2);
        break;
    case physx::PxErrorCode::eINVALID_OPERATION:
        error = "Invalid Operation";
        loggingCallback = std::bind(&PhysXMessenger::errorCallback, this, std::placeholders::_1, std::placeholders::_2);
        break;
    case physx::PxErrorCode::eOUT_OF_MEMORY:
        error = "Out of Memory";
        loggingCallback = std::bind(&PhysXMessenger::errorCallback, this, std::placeholders::_1, std::placeholders::_2);
        break;
    case physx::PxErrorCode::eINTERNAL_ERROR:
        error = "Internal Error";
        loggingCallback = std::bind(&PhysXMessenger::errorCallback, this, std::placeholders::_1, std::placeholders::_2);
        break;
    case physx::PxErrorCode::eABORT:
        error = "Abort";
        loggingCallback = std::bind(&PhysXMessenger::errorCallback, this, std::placeholders::_1, std::placeholders::_2);
        break;
    case physx::PxErrorCode::ePERF_WARNING:
        error = "Performance Warning";
        loggingCallback = std::bind(&PhysXMessenger::warningCallback, this, std::placeholders::_1, std::placeholders::_2);
        break;
    case physx::PxErrorCode::eMASK_ALL:
        error = "Unknown Error";
        loggingCallback = std::bind(&PhysXMessenger::errorCallback, this, std::placeholders::_1, std::placeholders::_2);
        break;
    }

    loggingCallback(error, message);
}

ELIX_NESTED_NAMESPACE_END
