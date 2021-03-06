#ifndef BETTER_CONST_H
#define BETTER_CONST_H

#include "imgui.h"

#define BETTER_VERSION_STR "0.3.0"

enum : u8
{
    LOGLEVEL_DEBUG,
    LOGLEVEL_INFO,
    LOGLEVEL_WARN,
    LOGLEVEL_USERERROR,
    LOGLEVEL_DEVERROR,
    LOGLEVEL_ENUM_SIZE
};

enum : u8
{
    BETS_STATUS_OPEN,
    BETS_STATUS_COYOTE,
    BETS_STATUS_CLOSED
};

enum : UINT_PTR
{
    TID_SPOOF_MESSAGES,
    TID_ALLOW_AUTO_RECONNECT,
    TID_PRIVMSG_READY,
};

const i32 MIN_FRAMES_BEFORE_WAIT = 3;

const i32 WINDOW_MIN_X = 400;
const i32 WINDOW_MIN_Y = 350;

const size_t RECV_BUFLEN = 2100;
const size_t SEND_BUFLEN = 500;

const i32 LEADERBOARD_PAGE_SIZE = 500;

const i32 CHAT_BUFFER_MAX   = 1000;
const i32 LOG_BUFFER_MAX    = 1000;

const i32 CHANNEL_NAME_MAX  = 100;
const i32 USERNAME_MAX  = 100;
const i32 TOKEN_MAX = 128;

const u64 POINTS_MAX = UINT64_MAX;
const u64 POINTS_STEP_SMALL = 100;
const u64 POINTS_STEP_BIG = 1000;

const i32 TIMER_MAX = 1000000;
const i32 TIMER_STEP_SMALL = 1;
const i32 TIMER_STEP_BIG = 5;

const u32 DEFAULT_COYOTE_TIME = 5;

const i32 CHAT_COMMAND_MAX = 50;
const i32 CHAT_PARAM_MAX = 60;

const i32 POINTS_NAME_MAX = 30;
const i32 OPTION_NAME_MAX = 50;

// NOTE: These timevalues are in milliseconds (used with SetTimer)
const u32 PRIVMSG_MIN_INTERVAL = 1600; // Rate limit is 20 messages per 30 seconds -> 1.5 seconds interval.
const u32 PRIVMSG_MIN_INTERVAL_AS_MOD = 800; // Rate limit is 100 per 30 seconds -> 0.3 seconds interval.
const u32 MIN_RECONNECT_INTERVAL = 15000;

const ImVec4 TEXT_COLOR_WARN = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);

const ImVec4 LOG_TEXT_COLORS[LOGLEVEL_ENUM_SIZE] =
{
    ImVec4(0.8f, 0.8f, 0.8f, 1),
    ImVec4(1   , 1   , 1   , 1),
    TEXT_COLOR_WARN,
    ImVec4(1   , 0.2f, 0.2f, 1),
    ImVec4(1   , 0.2f, 0.2f, 1),
};

const char *const LOG_LEVEL_ID_STR[LOGLEVEL_ENUM_SIZE] =
{
    "dbug",
    "info",
    "warn",
    "uerr",
    "derr",
};

// To make sure we are always backward compatible when loading settings, the
// values of these keys must not be changed, and they must not collide.
enum BINN_KEY : int
{
    BINN_KEY_handout_amount = 0,
    BINN_KEY_timer_setting,
    BINN_KEY_show_window_chat,
    BINN_KEY_show_window_settings,
    BINN_KEY_show_window_log,
    BINN_KEY_show_window_points,
    BINN_KEY_show_window_bets,
    BINN_KEY_show_window_debug,
    BINN_KEY_show_window_statistics,
    BINN_KEY_auto_connect,
    BINN_KEY_channel,
    BINN_KEY_username,
    BINN_KEY_is_mod,
    BINN_KEY_command_prefix,
    BINN_KEY_points_name,
    BINN_KEY_starting_points,
    BINN_KEY_allow_multibets,
    BINN_KEY_add_mode,
    BINN_KEY_coyote_time,
    BINN_KEY_announce_bets_open,
    BINN_KEY_announce_bets_close,
    BINN_KEY_announce_payout,
    BINN_KEY_confirm_handout,
    BINN_KEY_confirm_leaderboard_reset,
    BINN_KEY_confirm_refund,
    BINN_KEY_confirm_payout,
    BINN_KEY_token,
};

static_assert(POINTS_NAME_MAX < CHAT_COMMAND_MAX); // We use the points name for the feedback command.
static_assert(OPTION_NAME_MAX < CHAT_PARAM_MAX); // Now that users can refer to options by name, the parameter buffer needs to be big enough to contain any name.
static_assert(TOKEN_MAX % CRYPTPROTECTMEMORY_BLOCK_SIZE == 0); // Requirement by the crypto api.

#endif // BETTER_CONST_H
