#ifndef BETTER_BETS_H
#define BETTER_BETS_H

#include <map>
#include <string>

#include "better_App.h"

u8 bets_status(App* app);
u64 available_points(App* app, std::string* user, i32 betting_on_option);
void register_max_bet(App* app, std::string* user, i32 option);
void register_bet(App* app, std::string* user, i64 amount, i32 option);
void open_bets(App* app);
void close_bets(App* app);
void reset_bets(App* app);
void do_payout(App* app, i32 winning_option, f64 table_total, f64 grand_total);

#endif // BETTER_BETS_H
