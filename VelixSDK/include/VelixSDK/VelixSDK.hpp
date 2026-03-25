#ifndef ELIX_VELIX_SDK_HPP
#define ELIX_VELIX_SDK_HPP

#include "VelixSDK/Register.hpp"
#include "VelixSDK/World.hpp"
#include "VelixSDK/VXActor.hpp"
#include "VelixSDK/VXObject.hpp"

#include "Engine/DebugDraw.hpp"

namespace VX
{
    /// Shorthand alias so SDK scripts can write VX::Debug::line(...) etc.
    using Debug = elix::engine::DebugDraw;
}

#endif // ELIX_VELIX_SDK_HPP
