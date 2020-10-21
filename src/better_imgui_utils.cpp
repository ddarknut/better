#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include "better_imgui_utils.h"
#include "better_App.h"

bool imgui_confirmable_button(char* button_text, ImVec2& button_size, bool skip_confirm)
{
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGui::PushID("imgui_confirmable_button");
    auto id = ImGui::GetID(button_text);
    bool* button_clicked_once = storage->GetBoolRef(id, false);
    bool res = false;
    if (!*button_clicked_once)
    {
        if (ImGui::Button(button_text, button_size))
        {
            if (skip_confirm)
                res = true;
            else
                *button_clicked_once = true;
        }
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f,0.1f,0.1f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1,0,0,1));
        if (ImGui::Button("Confirm", button_size))
        {
            *button_clicked_once = false;
            res = true;
        }
        if (!ImGui::IsItemHovered())
            *button_clicked_once = false;
        ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
    return res;
}

void imgui_tooltip(const char* content)
{
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(content);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void imgui_extra(const char* content)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    imgui_tooltip(content);
}

void imgui_push_disabled()
{
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
}

void imgui_pop_disabled()
{
    ImGui::PopStyleVar();
    ImGui::PopItemFlag();
}

ImPlotPoint bar_chart_getter(void* app, i32 i)
{
    return ImPlotPoint(i, (f64)((App*)app)->bet_registry[i].get_point_sum());
}
