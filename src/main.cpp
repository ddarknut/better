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
#include <binn.h>

#include "better.h"
#include "better_func.h"
#include "better_App.h"
#include "better_irc.h"
#include "better_bets.h"
#include "better_imgui_utils.h"

#if BETTER_DEBUG
extern i32 spoof_message_file_index;
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
    f64 now = (f64)qpc_ticks.QuadPart / (f64)qpc_frequency.QuadPart;

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

    i32 consecutive_frames_without_messages = 0;
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (true)
    {
        if (consecutive_frames_without_messages > MIN_FRAMES_BEFORE_WAIT &&
            !imgui_any_mouse_buttons_held(io) &&
            bets_status(&app) == BETS_STATUS_CLOSED)
            WaitMessage();
        else ++consecutive_frames_without_messages;

        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            consecutive_frames_without_messages = 0;
            if (msg.message == WM_QUIT) break;
        }
        if (msg.message == WM_QUIT) break;

        f64 last_frame_time = now;
        QueryPerformanceCounter(&qpc_ticks);
        now = (f64)qpc_ticks.QuadPart / (f64)qpc_frequency.QuadPart;
        f64 dt = now - last_frame_time;

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
        if (app.privmsg_ready &&
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
                    add_log(&app, LOGLEVEL_DEVERROR, "Feedback queue contained a username that was not found on the leaderboard.");
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
                    ImGui::Text("Better %s\n\nMade by ddarknut.\nContact: mail@ddark.net", BETTER_VERSION_STR);
                    if (imgui_clickable_text("ddark.net/better"))
                        open_url("https://ddark.net/better");

                    ImGui::Text("\nThird party software:");

                    ImGui::Text("Binn %s", binn_version());
                    ImGui::SameLine();
                    if (imgui_clickable_text("github.com/liteserver/binn"))
                        open_url("https://github.com/liteserver/binn");

                    ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
                    ImGui::SameLine();
                    if (imgui_clickable_text("github.com/ocornut/imgui"))
                        open_url("https://github.com/ocornut/imgui");

                    ImGui::Text("Fira font family");
                    ImGui::SameLine();
                    if (imgui_clickable_text("github.com/bBoxType/FiraSans"))
                        open_url("https://github.com/bBoxType/FiraSans");

                    ImGui::Text("ImPlot %s", IMPLOT_VERSION);
                    ImGui::SameLine();
                    if (imgui_clickable_text("github.com/epezent/implot"))
                        open_url("https://github.com/epezent/implot");

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
                    if (app.unread_error != -1)
                    {
                        ImGui::SameLine();
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, LOG_TEXT_COLORS[LOGLEVEL_USERERROR]);
                        if (imgui_clickable_text("Log contains new errors."))
                        {
                            app.settings.show_window_log = true;
                            app.should_focus_log_window = true;
                        }
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);

                ImGui::EndMenuBar();
            }

            main_dockspace_id = ImGui::GetID("MyDockSpace");

            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (ImGui::DockBuilderGetNode(main_dockspace_id) == NULL)
            {
                ImGui::DockBuilderRemoveNode(main_dockspace_id); // Clear out existing layout
                ImGui::DockBuilderAddNode(main_dockspace_id, ImGuiDockNodeFlags_DockSpace); // Add empty node
                ImGui::DockBuilderSetNodeSize(main_dockspace_id, ImVec2(avail.x, avail.y));

                ImGuiID dock_id_current = main_dockspace_id;

                ImGuiID dock_id_log = ImGui::DockBuilderSplitNode(dock_id_current, ImGuiDir_Down, 1.f-0.618f, NULL, &dock_id_current);

                ImGui::DockBuilderSetNodeSize(dock_id_current, ImVec2(avail.x, avail.y*0.618f));
                ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dock_id_current, ImGuiDir_Left, 1.f-0.618f, NULL, &dock_id_current);

                ImGui::DockBuilderSetNodeSize(dock_id_current, ImVec2(avail.x*0.618f, avail.y));
                ImGuiID dock_id_bets = ImGui::DockBuilderSplitNode(dock_id_current, ImGuiDir_Up, 0.5f, NULL, &dock_id_current);

                ImGui::DockBuilderDockWindow("Log", dock_id_log);
                ImGui::DockBuilderDockWindow("Leaderboard", dock_id_left);
                ImGui::DockBuilderDockWindow("Chat", dock_id_left);
                ImGui::DockBuilderDockWindow("Bets", dock_id_bets);
                ImGui::DockBuilderDockWindow("Stats", dock_id_current);

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
                {
                    static char t[9] = "_.-*^*-.";
                    char temp = t[0];
                    memmove(&t[0], &t[1], 7);
                    t[7] = temp;
                    ImGui::Text("%s%s%s%s%s%s%s%s", t, t, t, t, t, t, t, t);
                }
                ImGui::Checkbox("Show demo window", &show_demo_window);
                if (ImGui::InputInt("Spoof file index", &spoof_message_file_index))
                {
                    if (spoof_message_file_index >= 0)
                        start_reading_spoof_messages(&app);
                    else
                        stop_reading_spoof_messages(&app);
                }
                if (ImGui::InputFloat("Spoof message interval", &spoof_interval))
                {
                    if (spoof_message_file_index >= 0)
                        start_reading_spoof_messages(&app);
                }
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
                if (ImGui::InputScalar("##handout_amount", ImGuiDataType_U64, &app.settings.handout_amount, &POINTS_STEP_SMALL, &POINTS_STEP_BIG))
                {
                    if (app.settings.handout_amount > POINTS_MAX)
                        app.settings.handout_amount = POINTS_MAX;
                }
                ImGui::SameLine();
                if (imgui_confirmable_button("Hand out", ImVec2(avail_width * 0.25f, 0), !app.settings.confirm_handout))
                {
                    for (auto it = app.points.begin();
                         it != app.points.end();
                         ++it)
                    {
                        if (it->second > POINTS_MAX - app.settings.handout_amount)
                            it->second = POINTS_MAX;
                        else
                            it->second += app.settings.handout_amount;
                    }

                    std::sort(app.leaderboard.begin(),
                              app.leaderboard.end(),
                              [&](std::string a, std::string b) {
                                  return app.points[a] > app.points[b];
                              });

                    add_log(&app, LOGLEVEL_INFO, "Handed out %llu %s to all viewers.", app.settings.handout_amount, app.settings.points_name);
                }

                ImGui::SameLine();
                if(imgui_confirmable_button("Reset all", ImVec2(avail_width * 0.25f, 0), !app.settings.confirm_leaderboard_reset))
                {
                    reset_bets(&app);
                    add_log(&app, LOGLEVEL_INFO, "Resetting everyone's %s to the starting amount.", app.settings.points_name);
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
                if(imgui_confirmable_button("Refund all bets", ImVec2(6.5f*ImGui::GetFontSize(), 0), !app.settings.confirm_refund))
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

                        if(imgui_confirmable_button("Payout", ImVec2(3.5f*ImGui::GetFontSize(), 0), !app.settings.confirm_payout))
                        {
                            do_payout(&app, i, option_totals[i], grand_total_bets);
                        }

                        if (bets_were_open) imgui_pop_disabled();

                        ImGui::SameLine();

                        char info_str[50 + POINTS_NAME_MAX];
                        sprintf(info_str, "%llu bets, %.0f %s (%.1f%%)", it->bets.size(), option_totals[i], app.settings.points_name, (grand_total_bets == 0.0)? 0.0 : 100.0*option_totals[i]/grand_total_bets);
                        auto info_str_width = ImGui::CalcTextSize(info_str).x;

                        ImGui::PushFont(font_mono);

                        ImGui::Text("%i", i+1);
                        ImGui::SameLine();

                        char option_hint[32];
                        sprintf(option_hint, "Option %i", i+1);
                        auto avail = ImGui::GetContentRegionAvail().x;
                        auto name_width = ImGui::CalcTextSize(*it->option_name? it->option_name : option_hint).x;
                        ImGui::SetNextItemWidth(BETTER_MAX(name_width + style.FramePadding.x*2, avail - info_str_width));
                        if (ImGui::InputTextWithHint("", option_hint, it->option_name, sizeof(it->option_name)))
                        {
                            trim_whitespace(it->option_name);

                            // Make sure the name isn't taken!
                            for (auto other_opt = app.bet_registry.begin();
                                 other_opt != app.bet_registry.end();
                                 ++other_opt)
                            {
                                if (other_opt == it) continue;
                                if (_stricmp(it->option_name, other_opt->option_name) == 0)
                                {
                                    *(it->option_name) = '\0';
                                }
                            }
                        }
                        ImGui::SameLine();

                        ImGui::PopFont();

                        ImGui::Text(info_str);

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
                labels[i] = (char*) malloc(OPTION_NAME_MAX + 1);

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
                if (ImGui::RadioButton("Pie", !alt_chart))
                    alt_chart = false;
                ImGui::SameLine();
                if (ImGui::RadioButton("Bars", alt_chart))
                    alt_chart = true;
                ImGui::PopStyleVar();

                if (!alt_chart)
                {
                    static ImVec2 plot_size(1,1);
                    ImPlot::SetNextPlotLimits(0, plot_size.x, 0, plot_size.y, ImGuiCond_Always);
                    if (ImPlot::BeginPlot("##pie", NULL, NULL, ImVec2(-1, -1),
                                          ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMousePos,
                                          ImPlotAxisFlags_NoDecorations,
                                          ImPlotAxisFlags_NoDecorations))
                    {
                        plot_size = ImPlot::GetPlotSize();
                        ImPlot::PlotPieChart(labels, option_totals.data(), (i32)app.bet_registry.size(), plot_size.x*0.5, plot_size.y*0.5, BETTER_MIN(plot_size.x, plot_size.y)*0.5-5.0, true, "%.0f (%.1f%%)", 90, true);
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
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,0,0,1));
                                ImPlot::PlotText(bar_text, i, 0, false, ImVec2(0,-10));
                                ImGui::PopStyleColor();
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
            if (app.should_focus_log_window)
            {
                app.should_focus_log_window = false;
                ImGui::SetNextWindowFocus();
                ImGui::SetNextWindowCollapsed(false);
            }

            if (ImGui::Begin("Log", &app.settings.show_window_log))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
                ImGui::Text("Filter"); ImGui::SameLine();
                #if BETTER_DEBUG
                ImGui::Checkbox("Debug",    &app.log_filter[LOGLEVEL_DEBUG]); ImGui::SameLine();
                #endif
                ImGui::Checkbox("Info",     &app.log_filter[LOGLEVEL_INFO]); ImGui::SameLine();
                ImGui::Checkbox("Warnings", &app.log_filter[LOGLEVEL_WARN]); ImGui::SameLine();
                if (ImGui::Checkbox("Errors",   &app.log_filter[LOGLEVEL_USERERROR]))
                    app.log_filter[LOGLEVEL_DEVERROR] = app.log_filter[LOGLEVEL_USERERROR];
                if (!app.log_filter[LOGLEVEL_USERERROR] && app.unread_error != -1)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(LOG_TEXT_COLORS[LOGLEVEL_USERERROR], "*");
                }
                ImGui::PopStyleVar();

                ImGui::Separator();

                static bool goto_error = false;
                static bool scroll_to_bottom = false;
                static bool is_at_bottom = true;
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
                        if (actual_i == app.unread_error)
                        {
                            if (ImGui::IsItemVisible())
                                app.unread_error = -1;
                            else if (goto_error)
                            {
                                ImGui::SetScrollHereY(0.5f);
                                scroll_to_bottom = false;
                                goto_error = false;
                            }
                        }
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopStyleVar();

                    is_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
                    if (scroll_to_bottom || is_at_bottom)
                        ImGui::SetScrollHereY(1.0f); // scroll to bottom of last text item
                    scroll_to_bottom = false;
                }
                ImGui::EndChild();

                if (!is_at_bottom)
                {
                    if (ImGui::Button("Scroll to bottom"))
                        scroll_to_bottom = true;
                }
                if (app.unread_error != -1)
                {
                    if (!is_at_bottom)
                        ImGui::SameLine();
                    if (ImGui::Button("Go to last error"))
                    {
                        app.log_filter[LOGLEVEL_USERERROR] = true;
                        goto_error = true;
                    }
                }
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
                ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
                bool header_twitch = ImGui::CollapsingHeader("Twitch");
                imgui_tooltip("Connection, login info...");
                if (header_twitch)
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

                    if (ImGui::BeginTable("settings_twitch", 2))
                    {
                        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthAutoResize);
                        ImGui::TableNextColumn();

                        ImGui::Text("Channel");
                        ImGui::TableNextColumn();

                        f32 widget_width = ImGui::GetContentRegionAvailWidth() - 2.0f * style.FramePadding.x;

                        ImGui::SetNextItemWidth(widget_width);
                        if (irc_connected) imgui_push_disabled();
                        if (ImGui::InputText("##channel", app.settings.channel, CHANNEL_NAME_MAX))
                            make_lower(app.settings.channel);
                        if (irc_connected) imgui_pop_disabled();
                        ImGui::TableNextColumn();

                        ImGui::Text("Auto-connect");
                        imgui_extra("If enabled, Better auto-connects to the channel on startup.");
                        ImGui::TableNextColumn();
                        ImGui::Checkbox("##autoconnect", &app.settings.auto_connect);
                        ImGui::TableNextColumn();

                        ImGui::Text("Username");
                        imgui_extra("The username of the account the bot will log in as.\n");
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(widget_width);
                        if (irc_connected) imgui_push_disabled();
                        if (ImGui::InputText("##username", app.settings.username, CHANNEL_NAME_MAX))
                            make_lower(app.settings.username);
                        if (irc_connected) imgui_pop_disabled();
                        ImGui::TableNextColumn();

                        ImGui::Text("OAuth token");
                        imgui_extra("Go to twitchapps.com/tmi to get a token for your account. Must start with \"oauth:\". The clipboard will be emptied after pasting.");
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(widget_width);
                        if (irc_connected) imgui_push_disabled();
                        if (!app.settings.oauth_token_is_present)
                        {
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("(empty)");
                            ImGui::SameLine();

                            bool clip_open = OpenClipboard(app.main_wnd);
                            bool disable_paste = !IsClipboardFormatAvailable(CF_TEXT) ||
                                                 !clip_open;

                            if (disable_paste) imgui_push_disabled();

                            if (ImGui::Button("Paste##paste_oauth_token"))
                            {
                                HANDLE clip_handle = GetClipboardData(CF_TEXT);
                                if (clip_handle == NULL)
                                {
                                    add_log(&app, LOGLEVEL_DEVERROR, "GetClipboardData failed: %d", GetLastError());
                                }
                                else
                                {
                                    char* clip_data = (char*) GlobalLock(clip_handle);
                                    if (clip_data == NULL)
                                    {
                                        add_log(&app, LOGLEVEL_DEVERROR, "GlobalLock failed: %d", GetLastError());
                                    }
                                    else
                                    {
                                        strncpy(app.settings.token, clip_data, TOKEN_MAX);
                                        GlobalUnlock(clip_handle);
                                        if(strncmp(app.settings.token, "oauth:", 6) != 0)
                                        {
                                            add_log(&app, LOGLEVEL_USERERROR, "Pasted token has an incorrect format. Make sure it starts with \"oauth:\".");
                                            SecureZeroMemory(app.settings.token, sizeof(app.settings.token));
                                            app.settings.oauth_token_is_present = false;
                                        }
                                        else
                                        {
                                            if (!EmptyClipboard()) add_log(&app, LOGLEVEL_DEVERROR, "EmptyClipboard failed: %d", GetLastError());

                                            // Trim trailing whitespace in token
                                            char* c = app.settings.token;
                                            while (*c && *c != ' ' && *c != '\r' && *c != '\n') ++c;
                                            *c = '\0';

                                            if (!CryptProtectMemory(app.settings.token, sizeof(app.settings.token), CRYPTPROTECTMEMORY_SAME_PROCESS))
                                            {
                                                add_log(&app, LOGLEVEL_DEVERROR, "CryptProtectMemory failed: %i", GetLastError());
                                                SecureZeroMemory(app.settings.token, sizeof(app.settings.token));
                                                app.settings.oauth_token_is_present = false;
                                            }
                                            else app.settings.oauth_token_is_present = true;
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
                            if (imgui_confirmable_button("Clear##clear_oauth_token", ImVec2(4.0f*ImGui::GetFontSize(), 0)))
                            {
                                SecureZeroMemory(app.settings.token, sizeof(app.settings.token));
                                app.settings.oauth_token_is_present = false;
                            }
                        }
                        if (irc_connected) imgui_pop_disabled();
                        ImGui::TableNextColumn();

                        ImGui::Text("User is moderator");
                        imgui_extra("This allows Better to send messages to the chat more frequently to keep up with large viewer groups. Only enable this if the user is a moderator on (or the owner of) the channel, or you might be temporarily blocked by Twitch.");
                        ImGui::TableNextColumn();
                        ImGui::Checkbox("##mod_mode", &app.settings.is_mod);
                        ImGui::EndTable();
                    }
                }

                bool header_betting = ImGui::CollapsingHeader("Betting");
                imgui_tooltip("Betting behavior, chat commands...");
                if (header_betting)
                {
                    if (ImGui::BeginTable("settings_betting", 2))
                    {
                        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthAutoResize);
                        ImGui::TableNextColumn();

                        ImGui::Text("Command prefix");
                        ImGui::TableNextColumn();

                        f32 widget_width = ImGui::GetContentRegionAvailWidth() - 2.0f * style.FramePadding.x;

                        ImGui::SetNextItemWidth(widget_width);
                        ImGui::InputText("##command_prefix", app.settings.command_prefix, 2);
                        ImGui::TableNextColumn();

                        ImGui::Text("Currency name");
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(widget_width);
                        if (ImGui::InputText("##points_name", app.settings.points_name, POINTS_NAME_MAX, ImGuiInputTextFlags_CharsNoBlank))
                            make_lower(app.settings.points_name);

                        ImGui::Text("Command: %s%s", app.settings.command_prefix, app.settings.points_name);

                        ImGui::TableNextColumn();

                        bool bets_open = bets_status(&app) != BETS_STATUS_CLOSED;

                        ImGui::Text("Starting %s", app.settings.points_name);
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(widget_width);
                        ImGui::InputScalar("##starting_points", ImGuiDataType_U64, &app.settings.starting_points, &POINTS_STEP_SMALL, &POINTS_STEP_BIG/*, NULL, ImGuiInputTextFlags_EnterReturnsTrue*/);
                        ImGui::TableNextColumn();

                        ImGui::Text("Allow multibets");
                        imgui_extra("If enabled, viewers can place bets on multiple options at the same time. If disabled, placing a bet on one option will remove the viewer's bets on all other options.");
                        ImGui::TableNextColumn();
                        if (bets_open) imgui_push_disabled();
                        ImGui::Checkbox("##allow_multibets", &app.settings.allow_multibets);
                        if (bets_open) imgui_pop_disabled();
                        ImGui::TableNextColumn();

                        ImGui::Text("Bet update mode");
                        imgui_extra("This option controls what happens if a viewer places a bet on an option where they already have a wager.\n\nSet mode: The existing wager is replaced by the input amount.\nAdd mode: The input amount is added to the existing wager.");
                        ImGui::TableNextColumn();
                        if (bets_open) imgui_push_disabled();
                        if (ImGui::RadioButton("Set##update_mode_set", !app.settings.add_mode))
                            app.settings.add_mode = false;
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Add##update_mode_add", app.settings.add_mode))
                            app.settings.add_mode = true;
                        if (bets_open) imgui_pop_disabled();
                        ImGui::TableNextColumn();

                        ImGui::Text("Timer leniency");
                        imgui_extra("Bets will be open for this amount of seconds after the timer apparently runs out.");
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(widget_width);
                        if (bets_open) imgui_push_disabled();
                        ImGui::InputScalar("##timer_leniency", ImGuiDataType_U32, &app.settings.coyote_time, &TIMER_STEP_SMALL, &TIMER_STEP_BIG);
                        if (bets_open) imgui_pop_disabled();

                        ImGui::EndTable();
                    }
                }

                bool header_announcements = ImGui::CollapsingHeader("Announcements");
                imgui_tooltip("Choose what announcements Better will make in the chat.");
                if (header_announcements)
                {
                    if (ImGui::BeginTable("settings_announcements", 2))
                    {
                        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthAutoResize);
                        ImGui::TableNextColumn();

                        ImGui::Text("Bets open");
                        ImGui::TableNextColumn();
                        ImGui::Checkbox("##announce_bets_open", &app.settings.announce_bets_open);
                        ImGui::TableNextColumn();

                        ImGui::Text("Bets close");
                        ImGui::TableNextColumn();
                        ImGui::Checkbox("##announce_bets_close", &app.settings.announce_bets_close);
                        ImGui::TableNextColumn();

                        ImGui::Text("Payout");
                        ImGui::TableNextColumn();
                        ImGui::Checkbox("##announce_payout", &app.settings.announce_payout);
                        ImGui::EndTable();
                    }
                }

                bool header_confirmation = ImGui::CollapsingHeader("Confirmation");
                imgui_tooltip("Enable click-twice confirmation for functions that you don't want to click accidentally.");
                if (header_confirmation)
                {
                    if (ImGui::BeginTable("settings_confirmation", 2))
                    {
                        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthAutoResize);
                        ImGui::TableNextColumn();

                        ImGui::Text("Handouts");
                        ImGui::TableNextColumn();
                        ImGui::Checkbox("##confirm_handout", &app.settings.confirm_handout);
                        ImGui::TableNextColumn();

                        ImGui::Text("Resetting leaderboard");
                        ImGui::TableNextColumn();
                        ImGui::Checkbox("##confirm_leaderboard_reset", &app.settings.confirm_leaderboard_reset);
                        ImGui::TableNextColumn();

                        ImGui::Text("Refunding bets");
                        ImGui::TableNextColumn();
                        ImGui::Checkbox("##confirm_refund", &app.settings.confirm_refund);
                        ImGui::TableNextColumn();

                        ImGui::Text("Payouts");
                        ImGui::TableNextColumn();
                        ImGui::Checkbox("##confirm_payout", &app.settings.confirm_payout);
                        ImGui::TableNextColumn();

                        ImGui::EndTable();
                    }
                }
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
        case WM_TIMER:
        {
            switch ((UINT_PTR)wParam)
            {
                case TID_SPOOF_MESSAGES: {
                    #if BETTER_DEBUG
                    read_spoof_messages(&app);
                    #endif
                    return 0;
                }

                case TID_ALLOW_AUTO_RECONNECT: {
                    app.allow_auto_reconnect = true;
                    if (!KillTimer(app.main_wnd, TID_ALLOW_AUTO_RECONNECT))
                        add_log(&app, LOGLEVEL_DEVERROR, "KillTimer failed: %d", GetLastError());
                    return 0;
                }

                case TID_PRIVMSG_READY: {
                    app.privmsg_ready = true;
                    if (!KillTimer(app.main_wnd, TID_PRIVMSG_READY))
                        add_log(&app, LOGLEVEL_DEVERROR, "KillTimer failed: %d", GetLastError());
                    return 0;
                }
            }
        } break;
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
                        add_log(&app, LOGLEVEL_DEVERROR, "Failed to connect socket: %i\n", err);
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
