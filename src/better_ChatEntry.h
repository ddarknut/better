#ifndef BETTER_CHATENTRY_H
#define BETTER_CHATENTRY_H

struct ChatEntry
{
    char* name;
    char* msg;

    void free_all()
    {
        if (name) free(name);
        if (msg)  free(msg);
    }
};

#endif // BETTER_CHATENTRY_H
