#ifndef ELIX_ANIMATION_TREE_HPP
#define ELIX_ANIMATION_TREE_HPP

#include "Core/Macros.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/vec2.hpp>

#include <algorithm>
#include <cstdint>
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
        IntLess = 7,
        FloatEqual = 8,
        StateFinished = 9
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

struct AnimationBlendSpace1DSample
{
    std::string animationAssetPath;
    int clipIndex{0};
    float position{0.0f};
};

struct AnimationGraphTransition
{
    int fromNodeId{0};  // -1 = Any child in the owning machine
    int toNodeId{0};
    float blendDuration{0.3f};
    bool hasExitTime{false};
    float exitTime{1.0f};  // normalized [0,1]
    std::vector<AnimationTransitionCondition> conditions;
};

struct AnimationTreeNode
{
    enum class Type : uint8_t
    {
        StateMachine = 0,
        ClipState = 1,
        BlendSpace1D = 2
    };

    int id{-1};
    Type type{Type::ClipState};
    int parentMachineNodeId{-1};
    std::string name;
    glm::vec2 editorPosition{0.0f};

    // State machine data
    int entryNodeId{-1};
    std::vector<int> childNodeIds;
    std::vector<AnimationGraphTransition> transitions;

    // Leaf playback data
    std::string animationAssetPath;
    int clipIndex{0};
    bool loop{true};
    float speed{1.0f};

    // Blend space data
    std::string blendParameterName;
    std::vector<AnimationBlendSpace1DSample> blendSamples;

    [[nodiscard]] bool isStateMachine() const { return type == Type::StateMachine; }
    [[nodiscard]] bool isLeaf() const { return type != Type::StateMachine; }
};

class AnimationTree
{
public:
    static constexpr uint32_t CURRENT_GRAPH_VERSION = 1u;
    static constexpr int ANY_NODE_ID = -1;

    std::string name;
    std::string assetPath;

    // Legacy flat-tree payload kept for migration compatibility with old assets.
    int entryStateIndex{-1};
    std::vector<AnimationTreeState> states;
    std::vector<AnimationTransition> transitions;
    std::vector<AnimationTreeParameter> parameters;
    std::vector<glm::vec2> stateNodePositions;  // editor layout only

    // Hierarchical graph payload.
    uint32_t graphVersion{0u};
    int nextNodeId{1};
    int rootMachineNodeId{-1};
    std::vector<AnimationTreeNode> graphNodes;

    [[nodiscard]] bool hasGraph() const
    {
        return graphVersion >= CURRENT_GRAPH_VERSION &&
               rootMachineNodeId >= 0 &&
               findNode(rootMachineNodeId) != nullptr;
    }

    [[nodiscard]] AnimationTreeNode *findNode(int id)
    {
        const auto it = std::find_if(graphNodes.begin(), graphNodes.end(),
                                     [id](const AnimationTreeNode &node)
                                     { return node.id == id; });
        return it == graphNodes.end() ? nullptr : &(*it);
    }

    [[nodiscard]] const AnimationTreeNode *findNode(int id) const
    {
        const auto it = std::find_if(graphNodes.begin(), graphNodes.end(),
                                     [id](const AnimationTreeNode &node)
                                     { return node.id == id; });
        return it == graphNodes.end() ? nullptr : &(*it);
    }

    [[nodiscard]] int addGraphNode(AnimationTreeNode::Type type,
                                   const std::string &nodeName,
                                   int parentMachineNodeId = -1,
                                   const glm::vec2 &editorPosition = glm::vec2(0.0f))
    {
        AnimationTreeNode node{};
        node.id = nextNodeId++;
        node.type = type;
        node.parentMachineNodeId = parentMachineNodeId;
        node.name = nodeName;
        node.editorPosition = editorPosition;
        graphNodes.push_back(node);

        if (parentMachineNodeId >= 0)
        {
            if (AnimationTreeNode *parent = findNode(parentMachineNodeId);
                parent && parent->type == AnimationTreeNode::Type::StateMachine)
            {
                parent->childNodeIds.push_back(node.id);
                if (parent->entryNodeId < 0)
                    parent->entryNodeId = node.id;
            }
        }

        return node.id;
    }

    void createEmptyGraph()
    {
        graphNodes.clear();
        graphVersion = CURRENT_GRAPH_VERSION;
        nextNodeId = 1;
        rootMachineNodeId = addGraphNode(AnimationTreeNode::Type::StateMachine, "Root");
    }

    void migrateLegacyToGraph()
    {
        createEmptyGraph();

        AnimationTreeNode *root = findNode(rootMachineNodeId);
        if (!root)
            return;

        std::vector<int> legacyToNodeId(states.size(), -1);
        for (size_t index = 0; index < states.size(); ++index)
        {
            const AnimationTreeState &legacyState = states[index];
            const glm::vec2 editorPosition =
                index < stateNodePositions.size()
                    ? stateNodePositions[index]
                    : glm::vec2(280.0f + static_cast<float>(index) * 220.0f, 80.0f);
            const int nodeId = addGraphNode(AnimationTreeNode::Type::ClipState,
                                            legacyState.name,
                                            rootMachineNodeId,
                                            editorPosition);
            legacyToNodeId[index] = nodeId;

            if (AnimationTreeNode *node = findNode(nodeId))
            {
                node->animationAssetPath = legacyState.animationAssetPath;
                node->clipIndex = legacyState.clipIndex;
                node->loop = legacyState.loop;
                node->speed = legacyState.speed;
            }
        }

        if (entryStateIndex >= 0 &&
            entryStateIndex < static_cast<int>(legacyToNodeId.size()) &&
            legacyToNodeId[static_cast<size_t>(entryStateIndex)] >= 0)
        {
            root->entryNodeId = legacyToNodeId[static_cast<size_t>(entryStateIndex)];
        }
        else if (!root->childNodeIds.empty())
        {
            root->entryNodeId = root->childNodeIds.front();
        }

        root->transitions.clear();
        root->transitions.reserve(transitions.size());
        for (const AnimationTransition &legacyTransition : transitions)
        {
            if (legacyTransition.toStateIndex < 0 ||
                legacyTransition.toStateIndex >= static_cast<int>(legacyToNodeId.size()))
                continue;

            AnimationGraphTransition graphTransition{};
            graphTransition.fromNodeId =
                (legacyTransition.fromStateIndex == -1)
                    ? ANY_NODE_ID
                    : ((legacyTransition.fromStateIndex >= 0 &&
                        legacyTransition.fromStateIndex < static_cast<int>(legacyToNodeId.size()))
                           ? legacyToNodeId[static_cast<size_t>(legacyTransition.fromStateIndex)]
                           : -999999);
            graphTransition.toNodeId = legacyToNodeId[static_cast<size_t>(legacyTransition.toStateIndex)];
            if (graphTransition.toNodeId < 0)
                continue;
            if (graphTransition.fromNodeId == -999999)
                continue;

            graphTransition.blendDuration = legacyTransition.blendDuration;
            graphTransition.hasExitTime = legacyTransition.hasExitTime;
            graphTransition.exitTime = legacyTransition.exitTime;
            graphTransition.conditions = legacyTransition.conditions;
            root->transitions.push_back(std::move(graphTransition));
        }
    }

    void ensureGraph()
    {
        if (hasGraph())
            return;

        if (!states.empty() || !transitions.empty() || entryStateIndex >= 0 || !stateNodePositions.empty())
            migrateLegacyToGraph();
        else
            createEmptyGraph();
    }

    [[nodiscard]] std::vector<int> buildNodePath(int nodeId) const
    {
        std::vector<int> reversedPath;
        const AnimationTreeNode *node = findNode(nodeId);
        while (node)
        {
            reversedPath.push_back(node->id);
            if (node->parentMachineNodeId < 0)
                break;
            node = findNode(node->parentMachineNodeId);
        }

        std::reverse(reversedPath.begin(), reversedPath.end());
        return reversedPath;
    }

    [[nodiscard]] std::string formatNodePath(int nodeId, bool machinesOnly = false) const
    {
        const std::vector<int> path = buildNodePath(nodeId);
        std::string formatted;
        for (int pathNodeId : path)
        {
            const AnimationTreeNode *node = findNode(pathNodeId);
            if (!node)
                continue;
            if (machinesOnly && !node->isStateMachine())
                continue;

            if (!formatted.empty())
                formatted += " / ";
            formatted += node->name.empty() ? "(unnamed)" : node->name;
        }
        return formatted;
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ANIMATION_TREE_HPP
