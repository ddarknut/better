#ifndef BETTER_FUNC_H
#define BETTER_FUNC_H

void make_lower(char* s);
void add_log(App* app, u8 log_level, const char* const fmt ...);

void load_settings_from_disk(App* app);
void save_settings_to_disk(App* app);
void load_leaderboard_from_disk(App* app);
void save_leaderboard_to_disk(App* app);
u32 get_privmsg_interval(App* app);

#if BETTER_DEBUG
void start_reading_spoof_messages(App* app);
void stop_reading_spoof_messages(App* app);
void read_spoof_messages(App* app);
#endif

#endif // BETTER_FUNC_H
