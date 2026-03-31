#ifndef ELIX_ANIMATION_TREE_HPP
#define ELIX_ANIMATION_TREE_HPP

#include "Core/Macros.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/vec2.hpp>

#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct AnimationTreeParameter
{
    enum class Type : uint8_t
    {
        Float = 0,
        Bool = 1,
        Int = 2,
        Trigger = 3
    };

    std::string name;
    Type type{Type::Float};
    float floatDefault{0.0f};
    bool boolDefault{false};
    int intDefault{0};
};

struct AnimationTreeState
{
    std::string name;
    std::string animationAssetPath;
    int clipIndex{0};
    bool loop{true};
    float speed{1.0f};
};

struct AnimationTransitionCondition
{
    enum class Type : uint8_t
    {
        FloatGreater = 0,
        FloatLess = 1,
        BoolTrue = 2,
        BoolFalse = 3,
        IntEqual = 4,
        Trigger = 5,
        IntGreater = 6,
        IntLess = 7
    };

    Type type{Type::FloatGreater};
    std::string parameterName;
    float floatThreshold{0.0f};
    int intValue{0};
};

struct AnimationTransition
{
    int fromStateIndex{0};  // -1 = Any State
    int toStateIndex{0};
    float blendDuration{0.3f};
    bool hasExitTime{false};
    float exitTime{1.0f};  // normalized [0,1]
    std::vector<AnimationTransitionCondition> conditions;
};

class AnimationTree
{
public:
    std::string name;
    std::string assetPath;
    int entryStateIndex{-1};
    std::vector<AnimationTreeState> states;
    std::vector<AnimationTransition> transitions;
    std::vector<AnimationTreeParameter> parameters;
    std::vector<glm::vec2> stateNodePositions;  // editor layout only
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ANIMATION_TREE_HPP
