#ifndef BETTER_SETTINGS_H
#define BETTER_SETTINGS_H

#include "better_types.h"
#include "better_const.h"

const u32 SETTINGS_VERSION = 2;

struct Settings
{
    u64 handout_amount = 100;
    i32 timer_setting = 60;

    bool show_window_chat = false;
    bool show_window_settings = true;
    bool show_window_log = false;
    bool show_window_points = true;
    bool show_window_bets = true;
    bool show_window_debug = false;
    bool show_window_statistics = false;

    bool auto_connect = false;
    char channel[CHANNEL_NAME_MAX] = "";
    char username[USERNAME_MAX] = "";
    char token[TOKEN_MAX] = "";
    bool is_mod = false;

    char command_prefix[2] = "!";
    char points_name[POINTS_NAME_MAX] = "points";

    u64 starting_points = 100;
    bool allow_multibets = true;
    u32 coyote_time = DEFAULT_COYOTE_TIME;

    bool announce_bets_open = true;
    bool announce_bets_close = true;
    bool announce_payout = true;
};

#endif // BETTER_SETTINGS_H
