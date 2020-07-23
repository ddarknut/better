#ifndef BETTER_BETTABLE_H
#define BETTER_BETTABLE_H

#include "better.h"

struct BetTable
{
    char option_name[OPTION_NAME_MAX] = "";
    std::map<std::string, u64> bets;

    u64 get_point_sum()
    {
        u64 res = 0;
        for (auto it = bets.begin(); it != bets.end(); ++it)
            res += it->second;
        return res;
    }
};

#endif // BETTER_BETTABLE_H
