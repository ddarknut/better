#ifndef BETTER_CONST_H
#define BETTER_CONST_H

#include "imgui.h"

#define BETTER_VERSION_STR "0.2.2 WIP"

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
const u32 PRIVMSG_MIN_INTERVAL = (u32)(30000.0 / 20.0 + 0.1);
const u32 PRIVMSG_MIN_INTERVAL_AS_MOD = (u32)(30000.0 / 100.0 + 0.1);
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

static_assert(POINTS_NAME_MAX < CHAT_COMMAND_MAX); // We use the points name for the feedback command.
static_assert(OPTION_NAME_MAX < CHAT_PARAM_MAX); // Now that users can refer to options by name, the parameter buffer needs to be big enough to contain any name.
static_assert(TOKEN_MAX % CRYPTPROTECTMEMORY_BLOCK_SIZE == 0); // Requirement by the crypto api.

#endif // BETTER_CONST_H
