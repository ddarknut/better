// TODO: Allow secure socket connection (SSL)
// TODO: Save window position and size to disk
// TODO: 'bet n%'
// TODO: undo stack
// TODO: style menu
// TODO: Allow use of option name when putting in bets
// TODO: Cumulative betting mode

#pragma warning (disable: 4996) // This function or variable may be unsafe (strcpy, sprintf, ...)

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <winsock2.h>
#include <d3d11.h>
#include <tchar.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <implot.h>

#include "better.h"
#include "better_func.h"
#include "better_App.h"
#include "better_irc.h"
#include "better_bets.h"

#if BETTER_DEBUG
extern f32 spoof_interval;
extern i32 spoof_chunk_size;
#endif

// Data
static ID3D11Device*            g_pd3dDevice = NULL;
static ID3D11DeviceContext*     g_pd3dDeviceContext = NULL;
static IDXGISwapChain*          g_pSwapChain = NULL;
static ID3D11RenderTargetView*  g_mainRenderTargetView = NULL;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

better_internal bool imgui_confirmable_button(char* button_text, ImVec2& button_size)
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
            // printf("%lu\n", id);
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

better_internal void imgui_tooltip(const char* content)
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

better_internal void imgui_extra(const char* content)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    imgui_tooltip(content);
}

better_internal void imgui_push_disabled()
{
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
}

better_internal void imgui_pop_disabled()
{
    ImGui::PopStyleVar();
    ImGui::PopItemFlag();
}

better_internal ImPlotPoint bar_chart_getter(void* app, i32 i)
{
    return ImPlotPoint(i, (f64)((App*)app)->bet_registry[i].get_point_sum());
}

App app;

#if BETTER_DEBUG
i32 main(i32, char**)
#else
INT WinMain(HINSTANCE, HINSTANCE, PSTR, INT)
#endif
{
    HINSTANCE hInstance = GetModuleHandle(NULL);

    i32 base_dir_bufsize = 64;
    while (true)
    {
        app.base_dir = (char*) malloc(base_dir_bufsize);
        char* ptr = app.base_dir + GetModuleFileNameA(hInstance, app.base_dir, base_dir_bufsize);
        // TODO: allow non-ansi characters in base_dir (by using the wide version of GetModuleFileName here, and then WideCharToMultiByte)
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            base_dir_bufsize *= 2;
            free(app.base_dir);
            continue;
        }
        while (*ptr != '\\') --ptr;
        ptr[1] = '\0';
        break;
    }

    add_log(&app, LOGLEVEL_DEBUG, "Program starting up.");

    load_settings_from_disk(&app);
    load_leaderboard_from_disk(&app);

    LARGE_INTEGER qpc_frequency;
    QueryPerformanceFrequency(&qpc_frequency);
    LARGE_INTEGER qpc_ticks;
    QueryPerformanceCounter(&qpc_ticks);
    app.now = (f64)qpc_ticks.QuadPart / (f64)qpc_frequency.QuadPart;

    if (!irc_init(&app)) return 1;

    ImGui_ImplWin32_EnableDpiAwareness();

    // Load resources
    HRSRC rsrc_font_default = FindResource(hInstance, L"font_default", RT_RCDATA);
    assert(rsrc_font_default);
    HGLOBAL h_font_default = LoadResource(hInstance, rsrc_font_default);
    assert(h_font_default);
    void* data_font_default = LockResource(h_font_default);
    assert(data_font_default);
    i32 size_font_default = SizeofResource(hInstance, rsrc_font_default);
    assert(size_font_default);

    HRSRC rsrc_font_mono = FindResource(hInstance, L"font_mono", RT_RCDATA);
    assert(rsrc_font_mono);
    HGLOBAL h_font_mono = LoadResource(hInstance, rsrc_font_mono);
    assert(h_font_mono);
    void* data_font_mono = LockResource(h_font_mono);
    assert(data_font_mono);
    i32 size_font_mono = SizeofResource(hInstance, rsrc_font_mono);
    assert(size_font_mono);

    HICON icon_handle = LoadIconA(hInstance, "icon");

    add_log(&app, LOGLEVEL_DEBUG, "Creating window.");

    // Create window
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, hInstance, icon_handle, NULL, NULL, NULL, _T("Better"), NULL };
    ::RegisterClassEx(&wc);
    app.main_wnd = ::CreateWindow(wc.lpszClassName, _T("Better"), WS_OVERLAPPEDWINDOW, 100, 100, 1000, 800, NULL, NULL, wc.hInstance, NULL);

    // Initialize Direct3D
    if (!CreateDeviceD3D(app.main_wnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(app.main_wnd, SW_SHOWDEFAULT);
    ::UpdateWindow(app.main_wnd);

    add_log(&app, LOGLEVEL_DEBUG, "Creating imgui context.");

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImPlot::CreateContext();

    char* imgui_ini_path = (char*) malloc(base_dir_bufsize + 10);
    sprintf(imgui_ini_path, "%s%s", app.base_dir, "imgui.ini");

    io.IniFilename = imgui_ini_path;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    ImFont* font_default  = io.Fonts->AddFontFromMemoryTTF(data_font_default,
                                                           size_font_default,
                                                           18,
                                                           &font_config);
    ImFont* font_big      = io.Fonts->AddFontFromMemoryTTF(data_font_default,
                                                           size_font_default,
                                                           36,
                                                           &font_config);
    ImFont* font_mono     = io.Fonts->AddFontFromMemoryTTF(data_font_mono,
                                                           size_font_mono,
                                                           18,
                                                           &font_config);
    ImFont* font_mono_big = io.Fonts->AddFontFromMemoryTTF(data_font_mono,
                                                           size_font_mono,
                                                           36,
                                                           &font_config);
    io.Fonts->AddFontDefault();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 3;

    // Setup Platform/Renderer bindings
    ImGui_ImplWin32_Init(app.main_wnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool show_demo_window = false;

    ZeroMemory(&app.chat_buffer, sizeof(app.chat_buffer));

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    srand((u32)time(NULL));

    if (app.settings.auto_connect)
        irc_connect(&app);

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        if (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            continue;
        }

        #if BETTER_DEBUG
        static i32 read_spoof_messages = -1;
        if (read_spoof_messages >= 0)
            maybe_read_spoof_messages(&app, read_spoof_messages);
        #endif

        f64 last_frame_time = app.now;
        QueryPerformanceCounter(&qpc_ticks);
        app.now = (f64)qpc_ticks.QuadPart / (f64)qpc_frequency.QuadPart;
        f64 dt = app.now - last_frame_time;

        // Handle new messages
        for (auto it = app.read_queue.begin();
             it != app.read_queue.end();
             ++it)
            irc_handle_message(&app, &*it);
        app.read_queue.clear();

        // Update data used in GUI
        u32 total_number_of_bets = 0;
        f64 grand_total_bets = 0;
        std::vector<f64> option_totals;
        for (auto it_table = app.bet_registry.begin();
             it_table != app.bet_registry.end();
             ++it_table)
        {
            total_number_of_bets += (u32)it_table->bets.size();
            option_totals.push_back((f64)it_table->get_point_sum());
            grand_total_bets += (f64)option_totals.back();
        }
        assert(option_totals.size() == app.bet_registry.size());

        char points_name_cap[POINTS_NAME_MAX];
        strcpy(points_name_cap, app.settings.points_name);
        points_name_cap[0] = toupper(points_name_cap[0]);

        RECT rect;
        GetClientRect(app.main_wnd, &rect);
        i32 display_w = rect.right - rect.left,
            display_h = rect.bottom - rect.top;

        // Check point feedback queue
        // NOTE: If someone spams the !points command, there was a problem where privmsg_queue would fill up with a bunch of messages for that person, and the bot would keep dishing them out long after the person stopped spamming the command. We do a couple things here to minimize that problem: we check that privmsg_queue is empty, and that we are ready to send a new privmsg is so that there is less time between a user being removed from point_feedback_queue to them actually getting the message. Also, we don't go through all of point_feedback_queue -- only enough to create one PRIVMSG.
        // TODO: We can probably eliminate this problem entirely if we just move this to irc_on_write and send messages directly instead of via irc_queue_write.
        if (app.now - app.last_privmsg_time > get_privmsg_interval(&app) &&
            !app.point_feedback_queue.empty() &&
            app.privmsg_queue.empty())
        {
            char* buf = (char*) malloc(SEND_BUFLEN + 1);
            sprintf(buf, "PRIVMSG #%s :%s: ", app.settings.channel, points_name_cap);

            size_t used_chars = strlen(buf);

            char single[SEND_BUFLEN+1];

            auto last_used = app.point_feedback_queue.end();
            for (auto it = app.point_feedback_queue.begin();
                 it != app.point_feedback_queue.end();
                 ++it)
            {
                auto it_points = app.points.find(*it);
                if (it_points == app.points.end())
                {
                    add_log(&app, LOGLEVEL_ERROR, "Feedback queue contained a username that was not found on the leaderboard.");
                    last_used = it;
                    continue;
                }

                f64 points_used = 0;
                for (int i = 0; i < app.bet_registry.size(); ++i)
                {
                    auto it_bet = app.bet_registry[i].bets.find(*it);
                    if (it_bet != app.bet_registry[i].bets.end())
                        points_used += it_bet->second;
                }

                if (points_used > 0)
                    snprintf(single, SEND_BUFLEN+1, "%s: %.0f/%llu, ", it->c_str(), points_used, it_points->second);
                else
                    snprintf(single, SEND_BUFLEN+1, "%s: %llu, ", it->c_str(), it_points->second);
                size_t n = strlen(single);
                if (used_chars + n > SEND_BUFLEN) break;

                strcpy(buf + used_chars, single);
                used_chars += n;
                last_used = it;
            }

            if (last_used != app.point_feedback_queue.end())
                app.point_feedback_queue.erase(app.point_feedback_queue.begin(), ++last_used);

            strcpy(buf + used_chars - 2, "\r\n");

            irc_queue_write(&app, buf, true);
        }

        // Update timer
        if (app.timer_left > - (f32)app.settings.coyote_time)
        {
            app.timer_left -= (f32)dt;
            if (app.timer_left <= - (f32)app.settings.coyote_time)
                close_bets(&app);
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiID main_dockspace_id;

        /////////////////
        // MAIN WINDOW //
        /////////////////
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2((f32)display_w, (f32)display_h));
        ImGuiWindowFlags wnd_flags = ImGuiWindowFlags_NoDecoration
                                   | ImGuiWindowFlags_NoMove
                                   | ImGuiWindowFlags_NoScrollWithMouse
                                   | ImGuiWindowFlags_NoDocking
                                   | ImGuiWindowFlags_NoBringToFrontOnFocus
                                   | ImGuiWindowFlags_MenuBar;
        if (ImGui::Begin("Main", NULL, wnd_flags))
        {
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(8,8)); // default WindowPadding

            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("View"))
                {
                    ImGui::MenuItem("Bets",          NULL, &app.settings.show_window_bets);
                    ImGui::MenuItem("Chat",          NULL, &app.settings.show_window_chat);
                    #if BETTER_DEBUG
                    ImGui::MenuItem("Debug",         NULL, &app.settings.show_window_debug);
                    #endif
                    ImGui::MenuItem("Leaderboard",   NULL, &app.settings.show_window_points);
                    ImGui::MenuItem("Log",           NULL, &app.settings.show_window_log);
                    ImGui::MenuItem("Settings",      NULL, &app.settings.show_window_settings);
                    ImGui::MenuItem("Stats",         NULL, &app.settings.show_window_statistics);
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Help"))
                {
                    ImGui::Text("Better %s\n\nMade by ddarknut.\nContact: ddarknut@protonmail.com\n\nLibraries:\nDear ImGui %s - github.com/ocornut/imgui\nImPlot %s - github.com/epezent/implot", BETTER_VERSION_STR, ImGui::GetVersion(), IMPLOT_VERSION);
                    ImGui::EndMenu();
                }

                const char* irc_status_text;
                if (app.joined_channel)
                    irc_status_text = "Connected to Twitch.";
                else if (dns_thread_running(&app))
                    irc_status_text = "Waiting for DNS request...";
                else if (app.sock != INVALID_SOCKET)
                    irc_status_text = "Connecting...";
                else
                    irc_status_text = "Disconnected from Twitch.";

                const char* bets_status_text;
                if (bets_status(&app) != BETS_STATUS_CLOSED)
                    bets_status_text = "Bets are open.";
                else
                    bets_status_text = "Bets are closed.";

                char text[256];
                sprintf(text, "%s %s", irc_status_text, bets_status_text);

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10,0));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 100.0f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.08f,.08f,.08f,1));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
                if (ImGui::BeginChild("Status",
                                      ImVec2(ImGui::GetContentRegionAvail().x-8, ImGui::GetFrameHeight()),
                                      false,
                                      ImGuiWindowFlags_NoScrollbar
                                      | ImGuiWindowFlags_NoScrollWithMouse
                                      | ImGuiWindowFlags_AlwaysUseWindowPadding
                    ))
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text(text);
                }
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);

                ImGui::EndMenuBar();
            }

            main_dockspace_id = ImGui::GetID("MyDockSpace");

            if (ImGui::DockBuilderGetNode(main_dockspace_id) == NULL)
            {
                ImGui::DockBuilderRemoveNode(main_dockspace_id); // Clear out existing layout
                ImGui::DockBuilderAddNode(main_dockspace_id, ImGuiDockNodeFlags_DockSpace); // Add empty node
                ImGui::DockBuilderSetNodeSize(main_dockspace_id, ImVec2((f32)display_w, 1.f));

                ImGuiID dock_id_current = main_dockspace_id;
                ImGuiID dock_id_log = ImGui::DockBuilderSplitNode(dock_id_current, ImGuiDir_Down, 1.f-0.618f, NULL, &dock_id_current);
                ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dock_id_current, ImGuiDir_Left, 1.f-0.618f, NULL, &dock_id_current);
                // ImGuiID dock_id_bets = ImGui::DockBuilderSplitNode(dock_id_current, ImGuiDir_Up, 100.f, NULL, &dock_id_current);

                ImGui::DockBuilderDockWindow("Log", dock_id_log);
                ImGui::DockBuilderDockWindow("Leaderboard", dock_id_left);
                ImGui::DockBuilderDockWindow("Chat", dock_id_left);
                ImGui::DockBuilderDockWindow("Bets", dock_id_current);
                // ImGui::DockBuilderDockWindow("Stats", dock_id_current);

                ImGui::DockBuilderFinish(main_dockspace_id);
            }

            // ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0,0,0,0));
            ImGui::DockSpace(main_dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton);
            // ImGui::PopStyleColor();

            ImGui::PopStyleVar(); // WindowPadding
        }
        ImGui::End();
        ImGui::PopStyleVar(3);

        //////////////////
        // DEBUG WINDOW //
        //////////////////
        #if BETTER_DEBUG
        if (app.settings.show_window_debug)
        {
            if (ImGui::Begin("Debug", &app.settings.show_window_debug))
            {
                ImGui::Text("%.5f (%.1f)", dt, 1./dt);
                ImGui::Checkbox("Show demo window", &show_demo_window);
                ImGui::InputInt("Read spoof messages", &read_spoof_messages);
                ImGui::InputFloat("Spoof message interval", &spoof_interval);
                ImGui::InputInt("Spoof message chunk", &spoof_chunk_size);
                const i32 num_pers = 1234;
                if (ImGui::Button("Fill leaderboard"))
                {
                    for (i32 i = 0; i < num_pers; ++i)
                    {
                        char name[100];
                        sprintf(name, "person%.4i", i);
                        std::string s_name = std::string(name);
                        auto it_points = app.points.find(s_name);
                        if (it_points == app.points.end())
                        {
                            app.points[s_name] = app.settings.starting_points;
                            app.leaderboard.push_back(s_name);
                        }
                    }
                    std::sort(app.leaderboard.begin(),
                              app.leaderboard.end(),
                              [&](std::string a, std::string b) {
                                  return app.points[a] > app.points[b];
                              });
                }
                if (ImGui::Button("Fill feedback queue"))
                {
                    for (i32 i = 0; i < num_pers; ++i)
                    {
                        char name[20];
                        sprintf(name, "person%.4i", i);
                        app.point_feedback_queue.insert(std::string(name));
                    }
                }
                if (ImGui::Button("Fill bets") && app.bet_registry.size() > 0)
                {
                    for (i32 i = 0; i < num_pers; ++i)
                    {
                        char name[100];
                        sprintf(name, "person%.4i", i);
                        std::string s_name = std::string(name);
                        auto it_points = app.points.find(s_name);
                        if (it_points != app.points.end() && it_points->second > 0)
                        {
                            register_bet(&app, &s_name, rand()%(it_points->second), rand()%app.bet_registry.size());
                        }
                    }
                }
                if (ImGui::Button("Fill log"))
                {
                    for (i32 i = 0; i < 100; ++i)
                    {
                        add_log(&app, i % LOGLEVEL_ENUM_SIZE, "This is a log entry! Log level: %i", i % LOGLEVEL_ENUM_SIZE);
                    }
                }
            }
            ImGui::End();
        }

        /////////////////
        // DEMO WINDOW //
        /////////////////
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);
        #endif

        /////////////////
        // CHAT WINDOW //
        /////////////////
        if (app.settings.show_window_chat)
        {
            if (ImGui::Begin("Chat", &app.settings.show_window_chat))
            {
                static bool show_warning = true;
                if (show_warning)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, TEXT_COLOR_WARN);
                    ImGui::TextWrapped("/!\\ This view currently does not hide deleted messages. Be careful about capturing it on stream.");
                    ImGui::PopStyleColor();
                    if (ImGui::Button("OK")) show_warning = false;
                    ImGui::Separator();
                }
                if (app.chat_connected)
                {
                    better_persist bool scroll_to_bottom = false;
                    better_persist bool is_at_bottom = true;
                    if (ImGui::BeginChild("ChatScrollingRegion", ImVec2(0,-ImGui::GetFrameHeightWithSpacing())))
                    {
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,0));

                        for (i32 i = app.chat_first_index;
                             i < app.chat_first_index+app.chat_count;
                             ++i)
                        {
                            i32 actual_i = i % CHAT_BUFFER_MAX;

                            if (actual_i % 2)
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                            ImGui::TextWrapped("%s: %s", app.chat_buffer[actual_i].name, app.chat_buffer[actual_i].msg);
                            if (actual_i % 2)
                                ImGui::PopStyleColor();
                        }

                        ImGui::PopStyleVar();

                        is_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
                        if (scroll_to_bottom || is_at_bottom)
                            ImGui::SetScrollHereY(1.0f); // scroll to bottom of last text item
                        scroll_to_bottom = false;
                    }
                    ImGui::EndChild();

                    if (!is_at_bottom && ImGui::Button("Scroll to bottom"))
                        scroll_to_bottom = true;
                }
            }
            ImGui::End();
        }

        ////////////////////////
        // LEADERBOARD WINDOW //
        ////////////////////////
        if (app.settings.show_window_points)
        {
            if (ImGui::Begin("Leaderboard", &app.settings.show_window_points))
            {
                f32 avail_width = ImGui::GetContentRegionAvailWidth() - 2 * style.ItemSpacing.x;
                ImGui::SetNextItemWidth(avail_width * 0.5f);
                ImGui::InputScalar("##handout_amount", ImGuiDataType_U64, &app.settings.handout_amount, &POINTS_STEP_SMALL, &POINTS_STEP_BIG);
                ImGui::SameLine();
                if (imgui_confirmable_button("Hand out", ImVec2(avail_width * 0.25f, 0)))
                {
                    for (auto it = app.points.begin();
                         it != app.points.end();
                         ++it)
                        it->second += app.settings.handout_amount;

                    std::sort(app.leaderboard.begin(),
                              app.leaderboard.end(),
                              [&](std::string a, std::string b) {
                                  return app.points[a] > app.points[b];
                              });

                    add_log(&app, LOGLEVEL_INFO, "Handed out %llu %s to all viewers.", app.settings.handout_amount, app.settings.points_name);
                }

                ImGui::SameLine();
                if(imgui_confirmable_button("Reset all", ImVec2(avail_width * 0.25f, 0)))
                {
                    reset_bets(&app);
                    for (auto& entry : app.points)
                        entry.second = app.settings.starting_points;
                }

                BETTER_ASSERT(app.points.size() == app.leaderboard.size() && "Points table and leaderboard sizes do not match");

                static i32 page = 0;
                i32 leaderboard_size = (i32) app.leaderboard.size();
                if (ImGui::BeginChild("LeaderboardScrollingRegion", ImVec2(0,-ImGui::GetFrameHeightWithSpacing())))
                {
                    ImGui::Columns(3, "leaderboard_columns");
                    // ImGui::SetColumnWidth(0, 3.0f * ImGui::GetFontSize());
                    ImGui::Text("#");
                    ImGui::NextColumn();
                    ImGui::Text("User");
                    ImGui::NextColumn();
                    ImGui::Text(points_name_cap);
                    ImGui::NextColumn();
                    ImGui::Separator();

                    i32 i = 0;
                    for (auto it = app.leaderboard.begin();
                         it != app.leaderboard.end();
                         ++it)
                    {
                        if (i < page * LEADERBOARD_PAGE_SIZE)
                        {
                            ++i;
                            continue;
                        }

                        if (i >= (page + 1) * LEADERBOARD_PAGE_SIZE)
                            break;

                        if (i % 2)
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));

                        ImGui::Text("%i", i+1);
                        ImGui::NextColumn();
                        ImGui::Text(it->c_str());
                        ImGui::NextColumn();
                        ImGui::Text("%llu", app.points[*it]);
                        ImGui::NextColumn();

                        if (i % 2)
                            ImGui::PopStyleColor();

                        ++i;
                    }
                }
                ImGui::EndChild();

                if (ImGui::Button("<<", ImVec2(ImGui::GetFrameHeight(), 0)))
                    page = 0;
                ImGui::SameLine();
                if (ImGui::Button("<", ImVec2(ImGui::GetFrameHeight(), 0)))
                    if (page > 0) --page;
                ImGui::SameLine();
                ImGui::Text("%i-%i/%i",
                            BETTER_MIN(app.leaderboard.size(), page * LEADERBOARD_PAGE_SIZE + 1),
                            BETTER_MIN(app.leaderboard.size(), (page + 1) * LEADERBOARD_PAGE_SIZE),
                            app.leaderboard.size());
                ImGui::SameLine();
                ImGui::SetCursorPosX(BETTER_MAX(ImGui::GetContentRegionMax().x - 2.0f * ImGui::GetFrameHeight() - style.ItemSpacing.x,
                                     ImGui::GetCursorPos().x));
                if (ImGui::Button(">", ImVec2(ImGui::GetFrameHeight(), 0)))
                    if (page < leaderboard_size / LEADERBOARD_PAGE_SIZE) ++page;
                ImGui::SameLine();
                if (ImGui::Button(">>", ImVec2(ImGui::GetFrameHeight(), 0)))
                    page = leaderboard_size / LEADERBOARD_PAGE_SIZE;
            }
            ImGui::End();
        }

        /////////////////
        // BETS WINDOW //
        /////////////////
        if (app.settings.show_window_bets)
        {
            if (ImGui::Begin("Bets", &app.settings.show_window_bets))
            {
                ImGui::PushFont(font_mono_big);

                char buf[50];

                switch (bets_status(&app))
                {
                    case BETS_STATUS_OPEN:
                    {
                        i32 seconds_left = (i32)app.timer_left;
                        sprintf(buf, "%i:%.2i", seconds_left/60, seconds_left%60);
                    } break;

                    case BETS_STATUS_COYOTE:
                    {
                        sprintf(buf, "Bets are closing...");
                    } break;

                    case BETS_STATUS_CLOSED:
                    {
                        sprintf(buf, "Bets are closed.");
                    } break;
                }

                f32 progress = app.settings.timer_setting == 0? 0.f : app.timer_left/app.settings.timer_setting;
                f32 hue = BETTER_LERP(0.f, 80.f/255.f, progress);

                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(hue, 1.f, .9f).Value);
                ImGui::ProgressBar(progress, ImVec2(-1,0), buf);
                ImGui::PopStyleColor();

                ImGui::PopFont();

                bool bets_were_open = bets_status(&app) != BETS_STATUS_CLOSED;

                if (bets_were_open) imgui_push_disabled();
                if (ImGui::Button("Open bets"))
                {
                    open_bets(&app);
                }
                ImGui::SameLine();
                ImGui::Text("for");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(5 * ImGui::GetFontSize());
                ImGui::InputScalar("seconds", ImGuiDataType_S32, &app.settings.timer_setting, &TIMER_STEP_SMALL, &TIMER_STEP_BIG);
                app.settings.timer_setting = BETTER_CLAMP(app.settings.timer_setting, 1, TIMER_MAX);

                if (bets_were_open) imgui_pop_disabled();
                else imgui_push_disabled();
                if (ImGui::Button("Close bets")) close_bets(&app);
                if (bets_were_open) imgui_push_disabled();
                else imgui_pop_disabled();

                ImGui::SameLine();
                if(imgui_confirmable_button("Refund all bets", ImVec2(6.5f*ImGui::GetFontSize(), 0)))
                    reset_bets(&app);
                if (bets_were_open) imgui_pop_disabled();

                ImGui::Text("Total # bets: %i", total_number_of_bets);
                ImGui::SameLine(0, ImGui::GetFontSize());
                ImGui::Text("Total %s: %.0f", app.settings.points_name, grand_total_bets);

                ImGui::Separator();

                if (ImGui::BeginChild("Option list"))
                {
                    auto removal = app.bet_registry.end();

                    bool bets_exist = grand_total_bets > 0;

                    i32 i = 0;
                    for (auto it = app.bet_registry.begin();
                         it != app.bet_registry.end();
                         ++it)
                    {
                        ImGui::PushID(i);

                        if (bets_were_open) imgui_push_disabled();

                        if (bets_exist) imgui_push_disabled();
                        if (ImGui::Button("-", ImVec2(ImGui::GetFrameHeight(), 0)))
                        {
                            removal = it;
                        }
                        if (bets_exist)
                        {
                            imgui_pop_disabled();
                            imgui_tooltip("Refund bets or make a payout before removing options.");
                        }

                        ImGui::SameLine();

                        if(imgui_confirmable_button("Payout", ImVec2(3.5f*ImGui::GetFontSize(), 0)))
                        {
                            do_payout(&app, i, option_totals[i], grand_total_bets);
                        }

                        if (bets_were_open) imgui_pop_disabled();

                        ImGui::SameLine();

                        ImGui::Text("%i", i+1);
                        ImGui::SameLine();

                        ImGui::SetNextItemWidth(0.4f * ImGui::GetContentRegionAvail().x);
                        ImGui::InputTextWithHint("", "Name...", it->option_name, sizeof(it->option_name));
                        ImGui::SameLine();

                        ImGui::Text("%i bets, %.0f %s (%.1f%%)", it->bets.size(), option_totals[i], app.settings.points_name, (grand_total_bets == 0.0)? 0.0 : 100.0*option_totals[i]/grand_total_bets);

                        ImGui::Separator();

                        ImGui::PopID();
                        ++i;
                    }

                    if (removal != app.bet_registry.end())
                        app.bet_registry.erase(removal);

                    if (bets_were_open) imgui_push_disabled();
                    if (ImGui::Button("+", ImVec2(ImGui::GetFrameHeight(), 0)))
                    {
                        app.bet_registry.push_back(BetTable());
                        option_totals.push_back(0);
                    }
                    if (bets_were_open) imgui_pop_disabled();
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }

        //////////////////
        // STATS WINDOW //
        //////////////////
        if (app.settings.show_window_statistics)
        {
            char** labels = (char**) malloc(sizeof(char*) * app.bet_registry.size());
            f64* positions = (f64*) malloc(sizeof(f64) * app.bet_registry.size());
            for (int i = 0; i < app.bet_registry.size(); ++i)
                labels[i] = (char*) malloc(20);

            ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(display_w * 0.5f - 150.f, display_h * 0.5f - 100.f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Stats", &app.settings.show_window_statistics))
            {
                if (!app.shark_name.empty())
                {
                    ImGui::Text("Top shark: %s (%llu)", app.shark_name.c_str(), app.shark_points);
                }
                if (!app.fish_name.empty())
                {
                    if (!app.shark_name.empty())
                        ImGui::SameLine();
                    ImGui::Text("Biggest fish: %s (%llu)", app.fish_name.c_str(), app.fish_points);
                }

                i32 i = 0;
                for (auto& option : app.bet_registry)
                {
                    if (option.option_name[0] != '\0')
                        sprintf(labels[i], "%s", option.option_name);
                    else
                        sprintf(labels[i], "Option %i", i+1);
                    positions[i] = i;
                    ++i;
                }

                char y_name[POINTS_NAME_MAX + 10];
                sprintf(y_name, "%s bet", points_name_cap);

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
                static bool alt_chart = false;
                if (ImGui::RadioButton("Pie chart", !alt_chart))
                    alt_chart = false;
                ImGui::SameLine();
                if (ImGui::RadioButton("Bar chart", alt_chart))
                    alt_chart = true;
                ImGui::PopStyleVar();

                if (!alt_chart)
                {
                    static ImVec2 plot_size(1,1);
                    ImPlot::SetNextPlotLimits(0, plot_size.x, 0, plot_size.y, ImGuiCond_Always);
                    // f32 size = BETTER_MIN(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
                    // size = BETTER_MAX(size, 100.0f);
                    if (ImPlot::BeginPlot("##pie", NULL, NULL, ImVec2(-1, -1),
                                          ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMousePos,
                                          ImPlotAxisFlags_NoDecorations,
                                          ImPlotAxisFlags_NoDecorations))
                    {
                        plot_size = ImPlot::GetPlotSize();
                        ImPlot::PlotPieChart(labels, option_totals.data(), (i32)app.bet_registry.size(), plot_size.x*0.5, plot_size.y*0.5, BETTER_MIN(plot_size.x, plot_size.y)*0.5-5.0, true, "%.0f");
                        ImPlot::EndPlot();
                    }
                }
                else
                {
                    ImPlot::SetNextPlotTicksX(positions, (i32)app.bet_registry.size(), labels);
                    ImPlot::SetNextPlotLimitsX(-0.5, app.bet_registry.size()-0.5, ImGuiCond_Always);
                    ImPlot::FitNextPlotAxes();
                    if (ImPlot::BeginPlot("##bars", NULL, y_name, ImVec2(-1,-1),
                                          ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMousePos,
                                          ImPlotAxisFlags_None,
                                          ImPlotAxisFlags_LockMin))
                    {
                        ImPlot::PlotBarsG("", bar_chart_getter, &app, (i32)app.bet_registry.size(), 0.9);

                        if (grand_total_bets > 0)
                            for (i32 i = 0; i < app.bet_registry.size(); ++i)
                            {
                                char bar_text[100];
                                sprintf(bar_text, "%.0f (%.1f%%)", option_totals[i], 100.0*option_totals[i]/grand_total_bets);
                                ImPlot::PlotText(bar_text, i, 0, false, ImVec2(0,-10));
                            }

                        ImPlot::EndPlot();
                    }
                }

            }
            ImGui::End();

            for (int i = 0; i < app.bet_registry.size(); ++i)
                free(labels[i]);
            free(labels);
            free(positions);
        }

        ////////////////
        // LOG WINDOW //
        ////////////////
        if (app.settings.show_window_log)
        {
            if (ImGui::Begin("Log", &app.settings.show_window_log))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
                ImGui::Text("Filter"); ImGui::SameLine();
                #if BETTER_DEBUG
                ImGui::Checkbox("Debug",    &app.log_filter[LOGLEVEL_DEBUG]); ImGui::SameLine();
                #endif
                ImGui::Checkbox("Info",     &app.log_filter[LOGLEVEL_INFO]); ImGui::SameLine();
                ImGui::Checkbox("Warnings", &app.log_filter[LOGLEVEL_WARN]); ImGui::SameLine();
                ImGui::Checkbox("Errors",   &app.log_filter[LOGLEVEL_ERROR]);
                ImGui::PopStyleVar();

                ImGui::Separator();

                better_persist bool scroll_to_bottom = false;
                better_persist bool is_at_bottom = true;
                if (ImGui::BeginChild("LogScrollingRegion", ImVec2(0,-ImGui::GetFrameHeightWithSpacing())))
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,0));

                    for (i32 i = app.log_first_index;
                         i < app.log_first_index+app.log_count;
                         ++i)
                    {
                        i32 actual_i = i % LOG_BUFFER_MAX;
                        if (!app.log_filter[app.log_buffer[actual_i].level]) continue;
                        ImGui::PushStyleColor(ImGuiCol_Text, LOG_TEXT_COLORS[app.log_buffer[actual_i].level]);
                        ImGui::TextWrapped(app.log_buffer[actual_i].content);
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopStyleVar();

                    is_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
                    if (scroll_to_bottom || is_at_bottom)
                        ImGui::SetScrollHereY(1.0f); // scroll to bottom of last text item
                    scroll_to_bottom = false;
                }
                ImGui::EndChild();

                if (!is_at_bottom && ImGui::Button("Scroll to bottom"))
                    scroll_to_bottom = true;
            }
            ImGui::End();
        }

        /////////////////////
        // SETTINGS WINDOW //
        /////////////////////
        if (app.settings.show_window_settings)
        {
            ImGui::SetNextWindowSize(ImVec2(600.f, 400.f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(display_w * 0.5f - 300.f, display_h * 0.5f - 200.f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Settings", &app.settings.show_window_settings))
            {
                bool dns_was_running = dns_thread_running(&app);
                if (dns_was_running) imgui_push_disabled();

                bool irc_connected = app.sock != INVALID_SOCKET || dns_was_running;

                if (irc_connected) imgui_push_disabled();
                // TODO: "reconnect"
                if (ImGui::Button("Connect"))
                {
                    if (!irc_connect(&app))
                    {
                        // TODO: Errors should already be handled in irc_connect, but maybe provide feedback
                    }
                }
                if (irc_connected) imgui_pop_disabled();

                ImGui::SameLine();

                if (ImGui::Button("Disconnect"))
                {
                    irc_disconnect(&app);
                }

                if (dns_was_running) imgui_pop_disabled();

                f32 x = ImGui::GetContentRegionAvail().x;
                ImGui::Columns(2, "settings_columns", false);
                ImGui::SetColumnWidth(0, BETTER_MAX(x * 0.35f, 8 * ImGui::GetFontSize()));

                ImGui::Text("Channel");
                ImGui::NextColumn();

                f32 widget_width = ImGui::GetContentRegionAvail().x;

                ImGui::PushID("Channel");
                ImGui::SetNextItemWidth(widget_width);
                if (irc_connected) imgui_push_disabled();
                if (ImGui::InputText("", app.settings.channel, CHANNEL_NAME_MAX))
                {
                    make_lower(app.settings.channel);
                }
                if (irc_connected) imgui_pop_disabled();
                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Text("Auto-connect");
                imgui_extra("If enabled, Better auto-connects to the channel on startup.");
                ImGui::NextColumn();
                ImGui::PushID("Auto-connect");
                ImGui::Checkbox("", &app.settings.auto_connect);
                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Text("Username");
                imgui_extra("May be left blank, in which case Better will log in anonymously, and won't be able to send messages to the channel. OAuth token will be ignored in this case.\n");
                ImGui::NextColumn();
                ImGui::PushID("username");
                ImGui::SetNextItemWidth(widget_width);
                if (irc_connected) imgui_push_disabled();
                ImGui::InputText("", app.settings.username, CHANNEL_NAME_MAX);
                if (irc_connected) imgui_pop_disabled();
                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Text("OAuth token");
                imgui_extra("Go to twitchapps.com/tmi to get a token for your account. Must start with \"oauth:\". The clipboard will be emptied after pasting.");
                ImGui::NextColumn();
                ImGui::PushID("token");
                ImGui::SetNextItemWidth(widget_width);

                if (irc_connected) imgui_push_disabled();

                if (app.settings.token[0] == '\0')
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("(empty)");
                    ImGui::SameLine();

                    bool clip_open = OpenClipboard(app.main_wnd);
                    bool disable_paste = !IsClipboardFormatAvailable(CF_TEXT) ||
                                         !clip_open;

                    if (disable_paste) imgui_push_disabled();

                    if (ImGui::Button("Paste"))
                    {
                        HANDLE clip_handle = GetClipboardData(CF_TEXT);
                        if (clip_handle == NULL)
                        {
                            add_log(&app, LOGLEVEL_ERROR, "GetClipboardData failed: %d", GetLastError());
                            abort();
                        }
                        else
                        {
                            char* clip_data = (char*) GlobalLock(clip_handle);
                            if (clip_data == NULL)
                            {
                                add_log(&app, LOGLEVEL_ERROR, "GlobalLock failed when getting clipboard data: %d", GetLastError());
                                abort();
                            }
                            else
                            {
                                strncpy(app.settings.token, clip_data, TOKEN_MAX);
                                GlobalUnlock(clip_handle);
                                if(strncmp(app.settings.token, "oauth:", 6) != 0)
                                {
                                    add_log(&app, LOGLEVEL_ERROR, "Pasted token has an incorrect format. Make sure it starts with \"oauth:\".");
                                    SecureZeroMemory(app.settings.token, sizeof(app.settings.token));
                                }
                                else
                                {
                                    if (!EmptyClipboard()) add_log(&app, LOGLEVEL_ERROR, "EmptyClipboard failed: %d", GetLastError());
                                    if (!CryptProtectMemory(app.settings.token, sizeof(app.settings.token), CRYPTPROTECTMEMORY_SAME_PROCESS))
                                    {
                                        add_log(&app, LOGLEVEL_ERROR, "CryptProtectMemory failed: %i", GetLastError());
                                    }
                                }
                            }
                        }
                    }

                    if (disable_paste) imgui_pop_disabled();
                    if (clip_open) CloseClipboard();
                }
                else
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("(hidden)");
                    ImGui::SameLine();
                    if (imgui_confirmable_button("Clear", ImVec2(4.0f*ImGui::GetFontSize(), 0)))
                        SecureZeroMemory(app.settings.token, sizeof(app.settings.token));
                }

                if (irc_connected) imgui_pop_disabled();

                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Text("User is moderator");
                imgui_extra("This allows Better to send messages to the chat more frequently to keep up with large viewer groups. Only enable this if the user is a moderator on (or the owner of) the channel, or you might be temporarily blocked by Twitch.");
                ImGui::NextColumn();
                ImGui::PushID("User is moderator");
                ImGui::Checkbox("", &app.settings.is_mod);
                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Separator();

                ImGui::Text("Command prefix");
                ImGui::NextColumn();
                ImGui::PushID("Command prefix");
                ImGui::SetNextItemWidth(widget_width);
                ImGui::InputText("", app.settings.command_prefix, 2);
                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Text("Points name");
                ImGui::NextColumn();
                ImGui::PushID("Points name");
                ImGui::SetNextItemWidth(widget_width);
                if (ImGui::InputText("", app.settings.points_name, POINTS_NAME_MAX, ImGuiInputTextFlags_CharsNoBlank))
                {
                    make_lower(app.settings.points_name);
                }
                ImGui::PopID();

                ImGui::Text("Command: %s%s", app.settings.command_prefix, app.settings.points_name);

                ImGui::NextColumn();

                ImGui::Separator();

                bool bets_open = bets_status(&app) != BETS_STATUS_CLOSED;

                ImGui::Text("Starting points");
                ImGui::NextColumn();
                ImGui::PushID("Starting points");
                ImGui::SetNextItemWidth(widget_width);
                ImGui::InputScalar("", ImGuiDataType_U64, &app.settings.starting_points, &POINTS_STEP_SMALL, &POINTS_STEP_BIG/*, NULL, ImGuiInputTextFlags_EnterReturnsTrue*/);
                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Text("Allow multibets");
                imgui_extra("If enabled, viewers can place bets on multiple options at the same time. If disabled, placing a bet on one option will remove the viewer's bets on all other options.");
                ImGui::NextColumn();
                if (bets_open) imgui_push_disabled();
                ImGui::PushID("Allow multibets");
                ImGui::Checkbox("", &app.settings.allow_multibets);
                if (bets_open) imgui_pop_disabled();
                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Text("Timer leniency");
                imgui_extra("Bets will be open for this amount of seconds after the timer apparently runs out.");
                ImGui::NextColumn();
                ImGui::PushID("Timer leniency");
                ImGui::SetNextItemWidth(widget_width);
                if (bets_open) imgui_push_disabled();
                ImGui::InputScalar("", ImGuiDataType_U32, &app.settings.coyote_time, &TIMER_STEP_SMALL, &TIMER_STEP_BIG);
                if (bets_open) imgui_pop_disabled();
                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Separator();

                ImGui::Text("Announcements");
                ImGui::NextColumn();
                ImGui::NextColumn();
                imgui_extra("Choose what announcements Better will make in the chat.");

                ImGui::Text("Bets open");
                ImGui::NextColumn();
                ImGui::PushID("announce_bets_open");
                ImGui::Checkbox("", &app.settings.announce_bets_open);
                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Text("Bets close");
                ImGui::NextColumn();
                ImGui::PushID("announce_bets_close");
                ImGui::Checkbox("", &app.settings.announce_bets_close);
                ImGui::PopID();
                ImGui::NextColumn();

                ImGui::Text("Payout");
                ImGui::NextColumn();
                ImGui::PushID("announce_payout");
                ImGui::Checkbox("", &app.settings.announce_payout);
                ImGui::PopID();
                ImGui::NextColumn();
            }
            ImGui::End();
        }

        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, (float*)&clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        g_pSwapChain->Present(1, 0); // Present with vsync
        // g_pSwapChain->Present(0, 0); // Present without vsync
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    ImPlot::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(app.main_wnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);

    irc_disconnect(&app);

    irc_cleanup(&app);

    save_leaderboard_to_disk(&app);
    save_settings_to_disk(&app);

    SecureZeroMemory(app.settings.token, sizeof(app.settings.token));

    add_log(&app, LOGLEVEL_DEBUG, "Program shutting down.");

    free(imgui_ini_path);
    free(app.base_dir);

    return 0;
}


// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[6] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };
    auto hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (hr != S_OK)
    {
        fprintf(stderr, "Failed to create hardware D3D device (0x%.8X), falling back to software renderer (WARP).", hr);
        hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
        if (hr != S_OK)
        {
            fprintf(stderr, "Failed to create software D3D device (0x%.8X).", hr);
            return false;
        }
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
        case WM_SIZE:
            if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
            {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        case WM_DPICHANGED:
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)
            {
                //const int dpi = HIWORD(wParam);
                //printf("WM_DPICHANGED to %d (%.0f%%)\n", dpi, (float)dpi / 96.0f * 100.0f);
                const RECT* suggested_rect = (RECT*)lParam;
                ::SetWindowPos(hWnd, NULL, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            break;
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO* mminfo = (MINMAXINFO*) lParam;
            mminfo->ptMinTrackSize.x = WINDOW_MIN_X;
            mminfo->ptMinTrackSize.y = WINDOW_MIN_Y;
            return 0;
        }
        case BETTER_WM_DNS_COMPLETE:
            irc_on_dns_complete((App*)wParam, (addrinfo*)lParam);
            return 0;
        case BETTER_WM_DNS_FAILED:
            irc_on_dns_failed((App*)wParam, (DWORD)lParam);
            return 0;
        case BETTER_WM_SOCK_MSG:
            SOCKET sock = (SOCKET)wParam;
            if (sock != app.sock) return 0;

            i32 err = WSAGETSELECTERROR(lParam);
            switch (WSAGETSELECTEVENT(lParam))
            {
                case FD_CONNECT:
                    if (err != 0)
                        add_log(&app, LOGLEVEL_ERROR, "Failed to connect socket: %i\n", err);
                    else
                        irc_on_connect(&app);
                    break;
                case FD_WRITE:
                    irc_on_write(&app);
                    break;
                case FD_READ:
                case FD_CLOSE:
                    irc_on_read_or_close(&app);
                    break;
            }

            return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
