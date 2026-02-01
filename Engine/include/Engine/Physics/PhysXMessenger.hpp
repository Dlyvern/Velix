#ifndef ELIX_PHYSX_MESSENGER
#define ELIX_PHYSX_MESSENGER

#include <foundation/PxErrorCallback.h>

#include "Core/Macros.hpp"
#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class PhysXMessenger : public physx::PxErrorCallback
{
public:
    explicit PhysXMessenger(core::Logger *logger);

    void reportError(physx::PxErrorCode::Enum code, const char *message, const char *file, const int line) override;

private:
    core::Logger *m_logger{nullptr};

    void infoCallback(const char *error, const char *message);
    void warningCallback(const char *error, const char *message);
    void errorCallback(const char *error, const char *message);
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PHYSX_MESSENGER