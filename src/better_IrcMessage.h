#ifndef BETTER_IRCMESSAGE_H
#define BETTER_IRCMESSAGE_H

#include <vector>

struct IrcMessage
{
    char* name = NULL;
    char* command = NULL;
    std::vector<char*> params;

    void free_all()
    {
        if (name)    free(name);
        if (command) free(command);
        free_params();
    }

    void free_params()
    {
        for (auto it = params.begin(); it != params.end(); ++it)
            free(*it);
    }
};

#endif // BETTER_IRCMESSAGE_H
