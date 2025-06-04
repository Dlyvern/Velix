#include "UIMesh.hpp"

#include <imgui.h>
#include <ElixirCore/AssetsManager.hpp>
#include <ElixirCore/Utilities.hpp>
#include "ElixirCore/GameObject.hpp"

void UIMesh::draw(common::Mesh *mesh, int meshIndex, GameObject* gameObject)
{
    if (!mesh)
        return;

     auto* material = mesh->getMaterial();

    if (!material)
        return;

    const std::string header = "Mesh " + std::to_string(meshIndex);

    if (ImGui::CollapsingHeader(header.c_str()))
    {
        glm::vec3 color = material->getBaseColor();

        if (ImGui::ColorEdit3(("Base Color##" + std::to_string(meshIndex)).c_str(), &color.x))
            material->setBaseColor(color);

        ImGui::SeparatorText("Textures");

        using TexType = elix::Texture::TextureType;

        for (auto textureType : {TexType::Diffuse, TexType::Normal, TexType::Metallic, TexType::Roughness, TexType::AO})
        {
            elix::Texture* tex = material->getTexture(textureType);
            ImGui::Text("%s: %s", utilities::fromTypeToString(textureType).c_str(), tex ? tex->getName().c_str() : "(none)");
        }

        const auto& allMaterials = AssetsManager::instance().getAllMaterials(); // returns vector<Material*>
        std::vector<std::string> materialNames;
        int currentIndex = -1;

        for (size_t i = 0; i < allMaterials.size(); ++i)
        {
            const auto& name = allMaterials[i]->getName();
            materialNames.push_back(name);

            Material* activeMaterial = gameObject->overrideMaterials.contains(meshIndex)
            ? gameObject->overrideMaterials[meshIndex]
            : mesh->getMaterial();

            if (name == activeMaterial->getName())
                currentIndex = static_cast<int>(i);
        }

        if (ImGui::BeginCombo(("Material##" + std::to_string(meshIndex)).c_str(),
                              currentIndex >= 0 ? materialNames[currentIndex].c_str() : "(none)"))
        {
            for (size_t i = 0; i < materialNames.size(); ++i)
            {
                bool isSelected = (currentIndex == static_cast<int>(i));
                if (ImGui::Selectable(materialNames[i].c_str(), isSelected))
                {
                    auto* newMaterial = allMaterials[i];
                    gameObject->overrideMaterials[meshIndex] = newMaterial;
                    // mesh->setMaterial(newMaterial);
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
}
