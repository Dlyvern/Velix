#include "Engine/Skeleton.hpp"

#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Skeleton::Skeleton()
{
    m_finalBoneMatrices.reserve(100);

    for (int i = 0; i < 100; i++)
        m_finalBoneMatrices.emplace_back(1.0f);
}

unsigned int Skeleton::addBone(const BoneInfo &bone)
{
    // If bone already exist
    if (auto it = m_boneMap.find(bone.name); it != m_boneMap.end())
        return it->second;

    const unsigned int boneID = m_bonesInfo.size();
    m_boneMap[bone.name] = boneID;
    m_bonesInfo.push_back(bone);

    return boneID;
}

int Skeleton::getBoneId(const std::string &boneName)
{
    auto it = m_boneMap.find(boneName);
    return it != m_boneMap.end() ? it->second : -1;
}

void Skeleton::printBonesHierarchy()
{
    for (const auto &bone : m_bonesInfo)
    {
        std::cout << "Bone: " << bone.name << " (ID: " << bone.id << "), Parent: "
                  << (bone.parentId == -1 ? "None" : m_bonesInfo[bone.parentId].name)
                  << ", Children: ";
        for (int child : bone.children)
        {
            std::cout << m_bonesInfo[child].name << " ";
        }
        std::cout << std::endl;
    }
}

size_t Skeleton::getBonesCount() const
{
    return m_bonesInfo.size();
}

bool Skeleton::hasBone(const std::string &boneName) const
{
    auto it = m_boneMap.find(boneName);

    return it != m_boneMap.end();
}

Skeleton::BoneInfo *Skeleton::getBone(const std::string &boneName)
{
    const int id = getBoneId(boneName);

    if (id == -1)
        return nullptr;

    return &m_bonesInfo[id];
}

Skeleton::BoneInfo *Skeleton::getBone(int boneID)
{
    if (m_bonesInfo.size() < boneID)
        return nullptr;

    return &m_bonesInfo[boneID];
}

Skeleton::BoneInfo *Skeleton::getParent()
{
    for (auto &bone : m_bonesInfo)
        if (bone.parentId == -1)
            return &bone;

    return nullptr;
}

void Skeleton::calculateBindPoseTransforms()
{
    m_bindPoseTransform.resize(m_bonesInfo.size(), glm::mat4(1.0f));

    glm::mat4 identity(1.0f);

    auto processBone = [this](int boneID, const glm::mat4 &parentTransform, auto &&self) -> void
    {
        BoneInfo &bone = m_bonesInfo[boneID];
        glm::mat4 globalTransform = parentTransform * bone.localBindTransform;

        // m_bindPoseTransform[boneID] = globalTransform * bone.offsetMatrix;
        m_bindPoseTransform[boneID] = globalTransform;

        // bone.finalTransformation = globalTransform * bone.offsetMatrix;
        // bone.finalTransformation = globalTransform;

        for (int childID : bone.children)
            self(childID, globalTransform, self);
    };

    for (const auto &bone : m_bonesInfo)
        if (bone.parentId == -1)
            processBone(bone.id, identity, processBone);
}

const std::vector<glm::mat4> &Skeleton::getBindPoses()
{
    // return m_bindPoseTransform;

    for (auto &m : m_bindPoseTransform)
        m = glm::mat4(1.0f);

    return m_bindPoseTransform;
}

const std::vector<glm::mat4> &Skeleton::getFinalMatrices()
{
    // for (const auto &bone : m_bonesInfo)
    //     m_finalBoneMatrices[bone.id] = bone.finalTransformation;

    for (const auto &bone : m_bonesInfo)
        m_finalBoneMatrices[bone.id] = bone.finalTransformation * bone.offsetMatrix;

    // for (auto &m : m_finalBoneMatrices)
    //     m = glm::mat4(1.0f);

    return m_finalBoneMatrices;
}

ELIX_NESTED_NAMESPACE_END