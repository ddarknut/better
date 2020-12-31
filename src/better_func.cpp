#pragma warning (disable: 4996) // This function or variable may be unsafe (strcpy, sprintf, ...)

#include <cctype>
#include <algorithm>
#include <winsock2.h>
#include <binn.h>

#include "better.h"
#include "better_func.h"
#include "better_const.h"
#include "better_Settings.h"

void make_lower(char* s)
{
    for(; *s; ++s) *s = tolower(*s);
}

bool str_contains(const char* const s, const char c)
{
    for (const char* p = s; *p != '\0'; ++p)
        if (*p == c) return true;
    return false;
}

void trim_whitespace(char* s)
{
    char* begin = s;

    // Leading whitespace
    while(str_contains(" \t\r\n", *begin)) ++begin;

    if (*begin == '\0')
    {
        // String is all whitespace
        *s = '\0';
        return;
    }

    // Trailing whitespace
    char* last = begin;
    while(last[1]) ++last; // Find end of string
    while(last > begin && str_contains(" \t\r\n", *last)) --last;

    memmove(s, begin, last - begin + 1);

    last -= begin - s;

    // Write new null terminator character
    last[1] = '\0';
}

void _add_log(App* app, const u8 log_level, const char* src_file, const i32 src_line, const char* const fmt ...)
{
    va_list args;
    va_start(args, fmt);

    // Write the content into a buffer
    char* buffer = (char*) malloc(256);
    i32 write_num = vsnprintf(buffer, 256, fmt, args);
    if (write_num > 256)
    {
        free(buffer);
        buffer = (char*) malloc(write_num);
        i32 write_num_again = vsnprintf(buffer, write_num, fmt, args);
        BETTER_ASSERT(write_num_again == write_num);
    }

    // Write the log to file
    SYSTEMTIME timestamp;
    GetLocalTime(&timestamp);
    char* path = (char*) malloc(strlen(app->base_dir) + 30);
    sprintf(path, "%slog_%.4hu-%.2hu-%.2hu.txt", app->base_dir, timestamp.wYear, timestamp.wMonth, timestamp.wDay);
    FILE* file = fopen(path, "a");
    fprintf(file, "%.2hu:%.2hu:%.2hu.%.3hu [%s] %s (%s:%i)\n", timestamp.wHour, timestamp.wMinute, timestamp.wSecond, timestamp.wMilliseconds, LOG_LEVEL_ID_STR[log_level], buffer, src_file, src_line);
    free(path);
    fclose(file);

    if (BETTER_DEBUG || log_level > LOGLEVEL_DEBUG)
    {
        // Add to the log buffer
        i32 pos = (app->log_first_index+app->log_count)%LOG_BUFFER_MAX;
        if (app->log_count == LOG_BUFFER_MAX)
        {
            free(app->log_buffer[pos].content);
            app->log_first_index = (app->log_first_index+1)%LOG_BUFFER_MAX;
            if (pos == app->unread_error)
                app->unread_error = -1;
        }
        else
            ++app->log_count;
        app->log_buffer[pos].level = log_level;
        app->log_buffer[pos].content = buffer;

        if (log_level >= LOGLEVEL_USERERROR)
            app->unread_error = pos;
    }
    else
    {
        free(buffer);
    }

    va_end(args);
}

void open_url(const char* url)
{
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

template<typename T>
static bool binn_try_get_value(u8* data_ptr, T* dest, const BINN_KEY key, i32 type, i32* ptr_size = NULL)
{
    T val;
    if (!binn_map_get(data_ptr, key, type, (void*)&val, ptr_size))
        return false;
    *dest = val;
    return true;
}

static bool binn_try_get_string(u8* data_ptr, char* dest, const BINN_KEY key, usize dest_size)
{
    char* val;
    if (!binn_map_get(data_ptr, key, BINN_STRING, (void*)&val, NULL))
        return false;
    strncpy(dest, val, dest_size);
    return true;
}

void load_settings_from_disk(App* app)
{
    char* path = (char*) malloc(strlen(app->base_dir) + 15);
    sprintf(path, "%ssettings", app->base_dir);
    FILE* file = fopen(path, "rb");
    free(path);

    if (!file)
        add_log(app, LOGLEVEL_DEBUG, "No settings file found.");
    else
    {
        add_log(app, LOGLEVEL_DEBUG, "Reading settings from disk.");

        u32 version;
        fread(&version, sizeof(u32), 1, file);

        // Started using the backwards compatible format on version 7 -- we should be able to read any known version newer than that.
        if (version < 7 || version > SETTINGS_VERSION)
        {
            add_log(app, LOGLEVEL_WARN, "Settings file version was too old or unknown (file: %i, current: %i). Resetting.", version, SETTINGS_VERSION);
        }
        else
        {
            fseek(file, 0, SEEK_END);
            usize data_size = ftell(file) - sizeof(u32);
            fseek(file, sizeof(u32), SEEK_SET);

            u8* data_ptr = (u8*) malloc(data_size);

            fread(data_ptr, data_size, 1, file);
            fclose(file);

            i32 obj_type = BINN_MAP, count = 0, size = (i32)data_size;
            if (!binn_is_valid_ex(data_ptr, &obj_type, &count, &size))
                add_log(app, LOGLEVEL_USERERROR, "Settings file is corrupted. Resetting.");
            else
            {
                binn_try_get_value(data_ptr, &app->settings.handout_amount,            BINN_KEY_handout_amount,            BINN_UINT64);
                binn_try_get_value(data_ptr, &app->settings.timer_setting,             BINN_KEY_timer_setting,             BINN_INT32);
                binn_try_get_value(data_ptr, &app->settings.show_window_chat,          BINN_KEY_show_window_chat,          BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.show_window_settings,      BINN_KEY_show_window_settings,      BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.show_window_log,           BINN_KEY_show_window_log,           BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.show_window_points,        BINN_KEY_show_window_points,        BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.show_window_bets,          BINN_KEY_show_window_bets,          BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.show_window_debug,         BINN_KEY_show_window_debug,         BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.show_window_statistics,    BINN_KEY_show_window_statistics,    BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.auto_connect,              BINN_KEY_auto_connect,              BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.is_mod,                    BINN_KEY_is_mod,                    BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.starting_points,           BINN_KEY_starting_points,           BINN_UINT64);
                binn_try_get_value(data_ptr, &app->settings.allow_multibets,           BINN_KEY_allow_multibets,           BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.add_mode,                  BINN_KEY_add_mode,                  BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.coyote_time,               BINN_KEY_coyote_time,               BINN_UINT32);
                binn_try_get_value(data_ptr, &app->settings.announce_bets_open,        BINN_KEY_announce_bets_open,        BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.announce_bets_close,       BINN_KEY_announce_bets_close,       BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.announce_payout,           BINN_KEY_announce_payout,           BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.confirm_handout,           BINN_KEY_confirm_handout,           BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.confirm_leaderboard_reset, BINN_KEY_confirm_leaderboard_reset, BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.confirm_refund,            BINN_KEY_confirm_refund,            BINN_BOOL);
                binn_try_get_value(data_ptr, &app->settings.confirm_payout,            BINN_KEY_confirm_payout,            BINN_BOOL);

                binn_try_get_string(data_ptr, app->settings.channel,                   BINN_KEY_channel,                   CHANNEL_NAME_MAX);
                binn_try_get_string(data_ptr, app->settings.username,                  BINN_KEY_username,                  USERNAME_MAX);
                binn_try_get_string(data_ptr, app->settings.command_prefix,            BINN_KEY_command_prefix,            2);
                binn_try_get_string(data_ptr, app->settings.points_name,               BINN_KEY_points_name,               POINTS_NAME_MAX);

                if (binn_map_null(data_ptr, BINN_KEY_token))
                    app->settings.oauth_token_is_present = false;
                else
                {
                    DATA_BLOB data_in, data_out;

                    if (binn_try_get_value(data_ptr, &data_in.pbData, BINN_KEY_token, BINN_BLOB, (i32*)&data_in.cbData))
                    {
                        if (!CryptUnprotectData(&data_in, NULL, NULL, NULL, NULL, 0, &data_out))
                        {
                            add_log(app, LOGLEVEL_DEVERROR, "Failed to decrypt saved OAuth token. Did Windows user credentials change? (CryptUnprotectData failed: %d)", GetLastError());
                        }
                        else
                        {
                            assert(data_out.cbData == sizeof(app->settings.token));
                            memcpy(app->settings.token, data_out.pbData, sizeof(app->settings.token));

                            if (!CryptProtectMemory(app->settings.token, sizeof(app->settings.token), CRYPTPROTECTMEMORY_SAME_PROCESS))
                            {
                                SecureZeroMemory(app->settings.token, sizeof(app->settings.token));
                                add_log(app, LOGLEVEL_DEVERROR, "CryptProtectMemory failed: %d", GetLastError());
                            }
                            else
                            {
                                app->settings.oauth_token_is_present = true;
                            }

                            SecureZeroMemory(data_out.pbData, data_out.cbData);
                            LocalFree(data_out.pbData);
                        }
                    }
                }
            }

            free(data_ptr);
        }
    }
}

void save_settings_to_disk(App* app)
{
    add_log(app, LOGLEVEL_DEBUG, "Saving settings to disk.");

    char* path = (char*) malloc(strlen(app->base_dir) + 15);
    sprintf(path, "%ssettings", app->base_dir);
    FILE* file = fopen(path, "wb");
    free(path);

    if (!file)
        add_log(app, LOGLEVEL_DEVERROR, "Couldn't open settings file for writing.");
    else
    {
        binn* map = binn_map();

        binn_map_set_uint64 (map, BINN_KEY_handout_amount,            app->settings.handout_amount);
        binn_map_set_int32  (map, BINN_KEY_timer_setting,             app->settings.timer_setting);
        binn_map_set_bool   (map, BINN_KEY_show_window_chat,          app->settings.show_window_chat);
        binn_map_set_bool   (map, BINN_KEY_show_window_settings,      app->settings.show_window_settings);
        binn_map_set_bool   (map, BINN_KEY_show_window_log,           app->settings.show_window_log);
        binn_map_set_bool   (map, BINN_KEY_show_window_points,        app->settings.show_window_points);
        binn_map_set_bool   (map, BINN_KEY_show_window_bets,          app->settings.show_window_bets);
        binn_map_set_bool   (map, BINN_KEY_show_window_debug,         app->settings.show_window_debug);
        binn_map_set_bool   (map, BINN_KEY_show_window_statistics,    app->settings.show_window_statistics);
        binn_map_set_bool   (map, BINN_KEY_auto_connect,              app->settings.auto_connect);
        binn_map_set_str    (map, BINN_KEY_channel,                   app->settings.channel);
        binn_map_set_str    (map, BINN_KEY_username,                  app->settings.username);
        binn_map_set_bool   (map, BINN_KEY_is_mod,                    app->settings.is_mod);
        binn_map_set_str    (map, BINN_KEY_command_prefix,            app->settings.command_prefix);
        binn_map_set_str    (map, BINN_KEY_points_name,               app->settings.points_name);
        binn_map_set_uint64 (map, BINN_KEY_starting_points,           app->settings.starting_points);
        binn_map_set_bool   (map, BINN_KEY_allow_multibets,           app->settings.allow_multibets);
        binn_map_set_bool   (map, BINN_KEY_add_mode,                  app->settings.add_mode);
        binn_map_set_uint32 (map, BINN_KEY_coyote_time,               app->settings.coyote_time);
        binn_map_set_bool   (map, BINN_KEY_announce_bets_open,        app->settings.announce_bets_open);
        binn_map_set_bool   (map, BINN_KEY_announce_bets_close,       app->settings.announce_bets_close);
        binn_map_set_bool   (map, BINN_KEY_announce_payout,           app->settings.announce_payout);
        binn_map_set_bool   (map, BINN_KEY_confirm_handout,           app->settings.confirm_handout);
        binn_map_set_bool   (map, BINN_KEY_confirm_leaderboard_reset, app->settings.confirm_leaderboard_reset);
        binn_map_set_bool   (map, BINN_KEY_confirm_refund,            app->settings.confirm_refund);
        binn_map_set_bool   (map, BINN_KEY_confirm_payout,            app->settings.confirm_payout);

        if (app->settings.oauth_token_is_present)
        {
            DATA_BLOB data_in, data_out;
            data_in.cbData = sizeof(app->settings.token);
            data_in.pbData = (BYTE*) app->settings.token;

            if (!CryptUnprotectMemory(app->settings.token,
                                      sizeof(app->settings.token),
                                      CRYPTPROTECTMEMORY_SAME_PROCESS))
            {
                add_log(app, LOGLEVEL_DEVERROR, "CryptUnprotectMemory failed: %i", GetLastError());
                SecureZeroMemory(app->settings.token, sizeof(app->settings.token));
                app->settings.oauth_token_is_present = false;
            }
            if (!CryptProtectData(&data_in, L"Twitch OAuth token for Better", NULL, NULL, NULL, 0, &data_out))
                add_log(app, LOGLEVEL_DEVERROR, "Failed to encrypt token: %i", GetLastError());
            else
            {
                if (!CryptProtectMemory(app->settings.token,
                                        sizeof(app->settings.token),
                                        CRYPTPROTECTMEMORY_SAME_PROCESS))
                {
                    add_log(app, LOGLEVEL_DEVERROR, "CryptProtectMemory failed: %i", GetLastError());
                    SecureZeroMemory(app->settings.token, sizeof(app->settings.token));
                    app->settings.oauth_token_is_present = false;
                }

                binn_map_set_blob(map, BINN_KEY_token, data_out.pbData, data_out.cbData);
            }
        }
        else
        {
            binn_map_set_null(map, BINN_KEY_token);
        }

        fwrite(&SETTINGS_VERSION, sizeof(u32), 1, file);
        fwrite(binn_ptr(map), binn_size(map), 1, file);
        fclose(file);

        binn_free(map);
    }
}

void load_leaderboard_from_disk(App* app)
{
    char* path = (char*) malloc(strlen(app->base_dir) + 20);
    sprintf(path, "%sleaderboard.txt", app->base_dir);
    FILE* file = fopen(path, "r");
    free(path);

    if (!file)
        add_log(app, LOGLEVEL_DEBUG, "No leaderboard file found.");
    else
    {
        add_log(app, LOGLEVEL_DEBUG, "Reading leaderboard from disk.");

        while (true)
        {
            char name[USERNAME_MAX];
            u64 points;

            i32 res = fscanf(file, "%s %llu\n", name, &points);
            if (res != 2) break;

            std::string s_name = std::string(name);
            app->points[s_name] = points;
            app->leaderboard.push_back(s_name);
        }

        fclose(file);

        std::sort(app->leaderboard.begin(),
                  app->leaderboard.end(),
                  [&](std::string a, std::string b) {
                      return app->points[a] > app->points[b];
                  });

    }

}

void save_leaderboard_to_disk(App* app)
{
    add_log(app, LOGLEVEL_DEBUG, "Saving leaderboard to disk.");

    char* path = (char*) malloc(strlen(app->base_dir) + 20);
    sprintf(path, "%sleaderboard.txt", app->base_dir);
    FILE* file = fopen(path, "w");
    assert(file);

    for (auto it = app->points.begin();
         it != app->points.end();
         ++it)
    {
        fprintf(file, "%s %llu\n", it->first.c_str(), it->second);
    }

    free(path);
    fclose(file);
}

u32 get_privmsg_interval(App* app)
{
    if (app->settings.is_mod)
        return PRIVMSG_MIN_INTERVAL_AS_MOD;
    return PRIVMSG_MIN_INTERVAL;
}

#if BETTER_DEBUG

i32 spoof_message_file_index = -1;
f32 spoof_interval = 0.5f;
i32 spoof_chunk_size = 10;
static f32 last_read_time = 0;
static i32 last_read_pos[2] = {};

void start_reading_spoof_messages(App* app)
{
    if (!SetTimer(app->main_wnd, TID_SPOOF_MESSAGES, (UINT)(spoof_interval*1000.0f), NULL))
        add_log(app, LOGLEVEL_DEVERROR, "SetTimer failed: %d", GetLastError());
}

void stop_reading_spoof_messages(App* app)
{
    if (!KillTimer(app->main_wnd, TID_SPOOF_MESSAGES))
        add_log(app, LOGLEVEL_DEVERROR, "KillTimer failed: %d", GetLastError());
}

void read_spoof_messages(App* app)
{
    i32 file_i = spoof_message_file_index;
    char* path = (char*) malloc(strlen(app->base_dir) + 30);
    sprintf(path, "%sdebug_chatlog%i.txt", app->base_dir, file_i);
    FILE* file = fopen(path, "r");

    if (file)
    {
        fseek(file, last_read_pos[file_i], SEEK_SET);

        for (int i = 0; i < spoof_chunk_size; ++i)
        {
            char name[USERNAME_MAX+1];
            char content[2100];
            char timestamp[15];

            i32 res = fscanf(file, "%s %s ", timestamp, name);
            if (res != 2)
            {
                last_read_pos[file_i] = 0;
                break;
            }
            if (!fgets(content, 2100, file))
            {
                last_read_pos[file_i] = 0;
                break;
            }

            last_read_pos[file_i] = ftell(file);

            name[strlen(name)-1] = '\0';
            content[strlen(content)-1] = '\0';

            IrcMessage msg;
            msg.command     = (char*) malloc(8);
            msg.name        = (char*) malloc(strlen(name) + 1);
            char* m_content = (char*) malloc(strlen(content) + 1);

            strcpy(msg.command, "PRIVMSG");
            strcpy(msg.name, name);
            strcpy(m_content, content);

            msg.params.push_back(m_content);

            app->read_queue.push_back(msg);
        }

        fclose(file);
    }

    free(path);
}

#endif
