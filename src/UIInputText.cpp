#include "UIInputText.hpp"

#include <imgui.h>

bool UIInputText::draw(std::string &line)
{
    static char nameBuffer[128];
    strncpy(nameBuffer, line.c_str(), sizeof(nameBuffer));
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

    if (ImGui::InputText("GameObject name", nameBuffer, IM_ARRAYSIZE(nameBuffer)))
    {
        line = nameBuffer;
        return true;
    }

    return false;

}
