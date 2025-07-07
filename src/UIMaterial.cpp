#include "UIMaterial.hpp"

#include <imgui.h>

void UIMaterial::draw(Material *material)
{
    if (!material)
        return;

    glm::vec3 color = material->getBaseColor();

    if (ImGui::ColorEdit3("Base Color##", &color.x))
        material->setBaseColor(color);

    ImGui::SeparatorText("Textures");

    using TexType = elix::Texture::TextureType;

    for (const auto& textureType : {TexType::Diffuse, TexType::Normal, TexType::Metallic, TexType::Roughness, TexType::AO})
    {
        elix::Texture* tex = material->getTexture(textureType);
        // ImGui::Text("%s: %s", utilities::fromTypeToString(textureType).c_str(), tex ? tex->getName().c_str() : "(none)");
    }

}
