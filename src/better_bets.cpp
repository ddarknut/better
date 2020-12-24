#pragma warning (disable: 4996) // This function or variable may be unsafe (strcpy, sprintf, ...)

#include <algorithm>

#include "better_bets.h"
#include "better_func.h"
#include "better_irc.h"

u8 bets_status(App* app)
{
    if (app->timer_left > 0.0f)
        return BETS_STATUS_OPEN;
    if (app->timer_left > - (f32)app->settings.coyote_time)
        return BETS_STATUS_COYOTE;
    return BETS_STATUS_CLOSED;
}

// betting_on_option is 0-indexed
u64 available_points(App* app, std::string* user, i32 betting_on_option)
{
    assert(betting_on_option >= 0 && betting_on_option < app->bet_registry.size());

    u64 res = app->points[*user];

    if (app->settings.add_mode)
    {
        // Subtract wager on this option.
        auto bet_map = app->bet_registry[betting_on_option].bets;
        auto bet = bet_map.find(*user);
        if (bet != bet_map.end())
        {
            if (bet->second > res)
                res = 0;
            else
                res -= bet->second;
        }
    }

    if (app->settings.allow_multibets)
    {
        // Subtract wagers on other options.
        i32 i = 0;
        for (auto it_table = app->bet_registry.begin();
             it_table != app->bet_registry.end();
             ++it_table)
        {
            if (i == betting_on_option)
            {
                ++i;
                continue;
            }

            auto it_bet = it_table->bets.find(*user);
            if (it_bet != it_table->bets.end())
            {
                if (it_bet->second > res)
                    res = 0;
                else
                    res -= it_bet->second;
            }

            ++i;
        }
    }

    return res;
}

void register_max_bet(App* app, std::string* user, i32 option)
{
    if (option < 0 || option >= app->bet_registry.size()) return;

    u64 amount = available_points(app, user, option);
    if (amount > 0) register_bet(app, user, amount, option);
}

void register_bet(App* app, std::string* user, i64 amount, i32 option)
{
    if (option < 0 || option >= app->bet_registry.size()) return;

    if (amount > 0 && available_points(app, user, option) < (u64)amount) return;

    // If multibets are not allowed, remove wagers on other options
    if (!app->settings.allow_multibets)
    {
        i32 idx = 0;
        for (auto it = app->bet_registry.begin();
             it != app->bet_registry.end();
             ++it, ++idx)
            if (idx != option) it->bets.erase(*user);
    }

    if (app->settings.add_mode)
    {
        u64 current_amount = app->bet_registry[option].bets[*user];
        i64 new_amount = current_amount + amount;
        if (new_amount <= 0)
            app->bet_registry[option].bets.erase(*user);
        else
            app->bet_registry[option].bets[*user] = new_amount;
    }
    else
    {
        if (amount <= 0)
            app->bet_registry[option].bets.erase(*user);
        else
            app->bet_registry[option].bets[*user] = amount;
    }
}

void open_bets(App* app)
{
    add_log(app, LOGLEVEL_INFO, "Opening bets for the next %i+%u seconds.", app->settings.timer_setting, app->settings.coyote_time);

    app->timer_left = (f32) app->settings.timer_setting;

    app->fish_name = "";
    app->shark_name = "";
    app->fish_points = 0;
    app->shark_points = 0;

    if (app->settings.announce_bets_open)
    {
        #define MSG_LEN 150 + CHANNEL_NAME_MAX + 2*POINTS_NAME_MAX
        static_assert(MSG_LEN < 512);
        char* buf = (char*) malloc(MSG_LEN);
        sprintf(buf, "PRIVMSG #%s :Bets are now OPEN! Type %s%s to see how many %s you have, and %sbet (amount) (option) to place a bet. E.g. %sbet 100 1\r\n", app->settings.channel, app->settings.command_prefix, app->settings.points_name, app->settings.points_name, app->settings.command_prefix, app->settings.command_prefix);
        irc_queue_write(app, buf, true);
    }
}

void close_bets(App* app)
{
    add_log(app, LOGLEVEL_INFO, "Closing bets.");

    app->timer_left = -INFINITY;

    if (app->settings.announce_bets_close)
    {
        char* buf = (char*) malloc(CHANNEL_NAME_MAX+50);
        sprintf(buf, "PRIVMSG #%s :Bets are now CLOSED!\r\n", app->settings.channel);
        irc_queue_write(app, buf, true);
    }
}

void reset_bets(App* app)
{
    add_log(app, LOGLEVEL_INFO, "Resetting bets.");
    for (auto it = app->bet_registry.begin();
         it != app->bet_registry.end();
         ++it)
        it->bets.clear();
}

// NOTE: winning_option is 0-indexed
void do_payout(App* app, i32 winning_option, f64 table_total, f64 grand_total)
{
    add_log(app, LOGLEVEL_INFO, "Collecting bets. Payout for %i betters on option %i (%s).", app->bet_registry[winning_option].bets.size(), winning_option+1, app->bet_registry[winning_option].option_name);

    i32 better_count = 0;
    BETTER_ASSERT(winning_option >= 0 && winning_option < app->bet_registry.size() && "winning option is outside the range of existing options");
    for (int i = 0; i < app->bet_registry.size(); ++i)
    {
        BetTable* table = &app->bet_registry[i];
        for (auto it = table->bets.begin();
             it != table->bets.end();
             ++it)
        {
            ++better_count;

            if (it->second > app->points[it->first])
                // Surely we will never hit this branch...
                app->points[it->first] = 0;
            else
            {
                app->points[it->first] -= it->second;
            }

            if (i == winning_option)
            {
                u64 win = (u64)((grand_total*it->second)/table_total);
                app->points[it->first] += win;

                if (win - it->second > app->shark_points)
                {
                    app->shark_name = it->first;
                    app->shark_points = win - it->second;
                }
            }
            else if (it->second > app->fish_points)
            {
                app->fish_name = it->first;
                app->fish_points = it->second;
            }
        }
    }

    if (app->settings.announce_payout && better_count > 0)
    {
        char* buf = (char*) malloc(100 + CHANNEL_NAME_MAX + OPTION_NAME_MAX);
        sprintf(buf, "PRIVMSG #%s :Option %i (%s) wins! Bets have been collected, and rewards paid out.\r\n", app->settings.channel, winning_option+1, app->bet_registry[winning_option].option_name);
        irc_queue_write(app, buf, true);
    }

    std::sort(app->leaderboard.begin(),
              app->leaderboard.end(),
              [&](std::string a, std::string b) {
                  return app->points[a] > app->points[b];
              });

    save_leaderboard_to_disk(app);

    reset_bets(app);
}
