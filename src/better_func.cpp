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
        }
        else
            ++app->log_count;
        app->log_buffer[pos].level = log_level;
        app->log_buffer[pos].content = buffer;
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

    if (file)
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
            fseek(file, 0, SEEK_SET);

            data_in.pbData = (BYTE*) malloc(data_in.cbData);

            fread(data_in.pbData, data_in.cbData, 1, file);

            fclose(file);

            auto res = CryptUnprotectData(&data_in, NULL, NULL, NULL, NULL, 0, &data_out);
            assert(res);

            assert(data_out.cbData == sizeof(app->settings));

            memcpy(&app->settings, data_out.pbData, data_out.cbData);

            if (app->settings.token[0] != '\0')
            {
                CryptProtectMemory(app->settings.token, sizeof(app->settings.token), CRYPTPROTECTMEMORY_SAME_PROCESS);
                SecureZeroMemory(data_out.pbData, data_out.cbData);
            }

            LocalFree(data_out.pbData);
            free(data_in.pbData);
        }
    }
    else add_log(app, LOGLEVEL_DEBUG, "No settings file found.");

    free(path);
}

void save_settings_to_disk(App* app)
{
    add_log(app, LOGLEVEL_DEBUG, "Saving settings to disk.");

    DATA_BLOB data_in, data_out;
    data_in.cbData = sizeof(app->settings);
    data_in.pbData = (BYTE*) &app->settings;

    bool has_token = app->settings.token[0] != '\0';
    if (has_token) CryptUnprotectMemory(app->settings.token,
                                        sizeof(app->settings.token),
                                        CRYPTPROTECTMEMORY_SAME_PROCESS);
    auto res = CryptProtectData(&data_in, L"User settings for Better", NULL, NULL, NULL, 0, &data_out);
    if (has_token) CryptProtectMemory(app->settings.token,
                                      sizeof(app->settings.token),
                                      CRYPTPROTECTMEMORY_SAME_PROCESS);
    assert(res);

    char* path = (char*) malloc(strlen(app->base_dir) + 15);
    sprintf(path, "%ssettings", app->base_dir);
    FILE* file = fopen(path, "wb");
    assert(file);

    fwrite(&SETTINGS_VERSION, sizeof(u32), 1, file);
    fwrite(data_out.pbData, data_out.cbData, 1, file);

    free(path);
    fclose(file);
}

void load_leaderboard_from_disk(App* app)
{
    add_log(app, LOGLEVEL_DEBUG, "Reading leaderboard from disk.");

    char* path = (char*) malloc(strlen(app->base_dir) + 20);
    sprintf(path, "%sleaderboard.txt", app->base_dir);
    FILE* file = fopen(path, "r");

    if (file)
    {
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
    else add_log(app, LOGLEVEL_DEBUG, "No leaderboard file found.");

    free(path);
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

f32 get_privmsg_interval(App* app)
{
    if (app->settings.is_mod)
        return PRIVMSG_MIN_INTERVAL_AS_MOD;
    return PRIVMSG_MIN_INTERVAL;
}

#if BETTER_DEBUG

f32 spoof_interval = 0.5f;
i32 spoof_chunk_size = 10;
static f32 last_read_time = 0;
static i32 last_read_pos[2] = {};

void maybe_read_spoof_messages(App* app, i32 file_i)
{
    if (app->now - last_read_time < spoof_interval) return;

    last_read_time = (f32)app->now;

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
