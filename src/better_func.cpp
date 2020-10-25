#pragma warning (disable: 4996) // This function or variable may be unsafe (strcpy, sprintf, ...)

#include <cctype>
#include <algorithm>

#include "better.h"
#include "better_func.h"
#include "better_const.h"

void make_lower(char* s)
{
    for(; *s; ++s) *s = tolower(*s);
}

void add_log(App* app, u8 log_level, const char* const fmt ...)
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
    fprintf(file, "%.2hu:%.2hu:%.2hu.%.3hu [%s] %s\n", timestamp.wHour, timestamp.wMinute, timestamp.wSecond, timestamp.wMilliseconds, LOG_LEVEL_ID_STR[log_level], buffer);
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

        if (log_level == LOGLEVEL_ERROR)
            app->unread_error = pos;
    }
    else
    {
        free(buffer);
    }

    va_end(args);
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
        if (version != SETTINGS_VERSION)
        {
            add_log(app, LOGLEVEL_WARN, "Settings file version (%i) did not match the current one (%i). Resetting.", version, SETTINGS_VERSION);
        }
        else
        {
            DATA_BLOB data_in, data_out;

            fseek(file, 0, SEEK_END);
            data_in.cbData = ftell(file) - sizeof(u32);
            fseek(file, sizeof(u32), SEEK_SET);

            data_in.pbData = (BYTE*) malloc(data_in.cbData);

            fread(data_in.pbData, data_in.cbData, 1, file);

            fclose(file);

            auto res = CryptUnprotectData(&data_in, NULL, NULL, NULL, NULL, 0, &data_out);
            if (!res)
            {
                add_log(app, LOGLEVEL_ERROR, "Failed to decrypt saved settings (%d). Did Windows user credentials change?", GetLastError());
            }
            else
            {
                assert(data_out.cbData == sizeof(app->settings));

                memcpy(&app->settings, data_out.pbData, data_out.cbData);

                if (app->settings.oauth_token_is_present)
                {
                    if (!CryptProtectMemory(app->settings.token, sizeof(app->settings.token), CRYPTPROTECTMEMORY_SAME_PROCESS))
                    {
                        add_log(app, LOGLEVEL_ERROR, "CryptProtectMemory failed: %d", GetLastError());
                    }
                    SecureZeroMemory(data_out.pbData, data_out.cbData);
                    app->settings.oauth_token_is_present = false;
                }
            }

            LocalFree(data_out.pbData);
            free(data_in.pbData);
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
        add_log(app, LOGLEVEL_ERROR, "Couldn't open settings file for writing.");
    else
    {
        DATA_BLOB data_in, data_out;
        data_in.cbData = sizeof(app->settings);
        data_in.pbData = (BYTE*) &app->settings;

        fwrite(&SETTINGS_VERSION, sizeof(u32), 1, file);

        if (app->settings.oauth_token_is_present)
        {
            if (!CryptUnprotectMemory(app->settings.token,
                                      sizeof(app->settings.token),
                                      CRYPTPROTECTMEMORY_SAME_PROCESS))
            {
                add_log(app, LOGLEVEL_ERROR, "CryptUnprotectMemory failed: %i", GetLastError());
                SecureZeroMemory(app->settings.token, sizeof(app->settings.token));
                app->settings.oauth_token_is_present = false;
            }
        }
        if (!CryptProtectData(&data_in, L"Twitch OAuth token for Better", NULL, NULL, NULL, 0, &data_out))
            add_log(app, LOGLEVEL_ERROR, "Failed to encrypt token: %i", GetLastError());
        else
        {
            if (app->settings.oauth_token_is_present)
            {
                if (!CryptProtectMemory(app->settings.token,
                                        sizeof(app->settings.token),
                                        CRYPTPROTECTMEMORY_SAME_PROCESS))
                {
                    add_log(app, LOGLEVEL_ERROR, "CryptProtectMemory failed: %i", GetLastError());
                    SecureZeroMemory(app->settings.token, sizeof(app->settings.token));
                    app->settings.oauth_token_is_present = false;
                }
            }

            fwrite(data_out.pbData, data_out.cbData, 1, file);
        }

        fclose(file);
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
        add_log(app, LOGLEVEL_ERROR, "SetTimer failed: %d", GetLastError());
}

void stop_reading_spoof_messages(App* app)
{
    if (!KillTimer(app->main_wnd, TID_SPOOF_MESSAGES))
        add_log(app, LOGLEVEL_ERROR, "KillTimer failed: %d", GetLastError());
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
