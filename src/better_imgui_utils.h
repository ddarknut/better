#ifndef BETTER_IMGUI_UTILS_H
#define BETTER_IMGUI_UTILS_H

#include <imgui.h>
#include <implot.h>

#include "better_types.h"

bool imgui_any_mouse_buttons_held(ImGuiIO& io);
bool imgui_confirmable_button(char* button_text, ImVec2& button_size, bool skip_confirm=false);
void imgui_tooltip(const char* content);
void imgui_extra(const char* content);
void imgui_push_disabled();
void imgui_pop_disabled();
ImPlotPoint bar_chart_getter(void* app, i32 i);

#endif // BETTER_IMGUI_UTILS_H
