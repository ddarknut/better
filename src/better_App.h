#ifndef BETTER_APP_H
#define BETTER_APP_H

#include <winsock2.h>
#include <deque>
#include <map>
#include <vector>
#include <string>
#include <set>

#include "better_Settings.h"
#include "better_IrcMessage.h"
#include "better_ChatEntry.h"
#include "better_BetTable.h"
#include "better_LogEntry.h"

struct App
{
    char* base_dir;

    SOCKET sock = INVALID_SOCKET;
    WSADATA wsa_data;
    HANDLE dns_req_thread = NULL;
    DWORD dns_req_thread_id;

    bool joined_channel = false;

    std::deque<char*> write_queue;
    std::deque<char*> privmsg_queue;
    std::deque<IrcMessage> read_queue;

    std::map<std::string, u64> points;
    std::vector<std::string> leaderboard;
    std::vector<BetTable> bet_registry;
    // TODO: because std::set sorts keys alphabetically, names with the
    // smallest alphabetical order will be prioritized in the case that
    // we can't keep up with the queue. Instead, switch to using
    // std::deque and manually check if the name is already added.
    std::set<std::string> point_feedback_queue;

    std::string fish_name, shark_name;
    u64 fish_points = 0, shark_points = 0;

    HWND main_wnd;

    LogEntry log_buffer[LOG_BUFFER_MAX];
    i32 log_first_index = 0;
    i32 log_count = 0;

    ChatEntry chat_buffer[CHAT_BUFFER_MAX];
    i32 chat_first_index = 0;
    i32 chat_count = 0;

    bool chat_auto_scroll = true;
    bool chat_connected = true;

    f64 now;
    f32 timer_left = -INFINITY;
    f32 last_privmsg_time = 0.0f;
    f32 last_connect_attempt = 0.0f;

    bool should_focus_log_window = false;
    i32 unread_error = -1;
    bool log_filter[LOGLEVEL_ENUM_SIZE] =
    {
        false,
        true,
        true,
        true,
    };

    Settings settings;
};

#endif // BETTER_APP_H
