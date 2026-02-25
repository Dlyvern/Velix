#include "Engine/Skeleton.hpp"

#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Skeleton::Skeleton()
{
}

unsigned int Skeleton::addBone(const BoneInfo &bone)
{
    // If bone already exist
    if (auto it = m_boneMap.find(bone.name); it != m_boneMap.end())
        return it->second;

    const unsigned int boneID = m_bonesInfo.size();
    m_boneMap[bone.name] = boneID;

    auto boneInfo = bone;
    boneInfo.id = static_cast<int>(boneID);
    m_bonesInfo.push_back(boneInfo);

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
        VX_ENGINE_INFO_STREAM("Bone: " << bone.name << " (ID: " << bone.id << "), Parent: "
                  << (bone.parentId == -1 ? "None" : m_bonesInfo[bone.parentId].name)
                  << ", Children: ");
        for (int child : bone.children)
        {
            VX_ENGINE_INFO_STREAM(m_bonesInfo[child].name << " ");
        }
        VX_ENGINE_INFO_STREAM(std::endl);
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
    if (boneID < 0 || static_cast<size_t>(boneID) >= m_bonesInfo.size())
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
    m_bindPoseTransform.assign(m_bonesInfo.size(), glm::mat4(1.0f));
    m_finalBoneMatrices.assign(m_bonesInfo.size(), glm::mat4(1.0f));

    if (m_bonesInfo.empty())
        return;

    glm::mat4 identity(1.0f);

    auto processBone = [this](int boneID, const glm::mat4 &parentTransform, auto &&self) -> void
    {
        BoneInfo &bone = m_bonesInfo[boneID];
        glm::mat4 globalTransform = parentTransform * bone.localBindTransform;

        bone.globalBindTransform = globalTransform;
        bone.finalTransformation = globalTransform;

        for (int childID : bone.children)
            self(childID, globalTransform, self);
    };

    for (const auto &bone : m_bonesInfo)
        if (bone.parentId == -1)
            processBone(bone.id, identity, processBone);
}

const std::vector<glm::mat4> &Skeleton::getBindPoses()
{
    if (m_bindPoseTransform.size() < m_bonesInfo.size())
        m_bindPoseTransform.resize(m_bonesInfo.size(), glm::mat4(1.0f));

    for (const auto &bone : m_bonesInfo)
        m_bindPoseTransform[bone.id] = bone.globalBindTransform * bone.offsetMatrix;

    return m_bindPoseTransform;
}

const std::vector<glm::mat4> &Skeleton::getFinalMatrices()
{
    if (m_finalBoneMatrices.size() < m_bonesInfo.size())
        m_finalBoneMatrices.resize(m_bonesInfo.size(), glm::mat4(1.0f));

    for (const auto &bone : m_bonesInfo)
        m_finalBoneMatrices[bone.id] = bone.finalTransformation * bone.offsetMatrix;

    return m_finalBoneMatrices;
}

ELIX_NESTED_NAMESPACE_END
