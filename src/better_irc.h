#ifndef BETTER_IRC_H
#define BETTER_IRC_H

#include <winsock2.h>

#include "better.h"
#include "better_App.h"

static DWORD _irc_main(App* app);

bool irc_init(App* app);
void irc_cleanup(App* app);

bool irc_connect(App* app);
void irc_disconnect(App* app);
void irc_timed_reconnect(App* app);

bool dns_thread_running(App* app);

void irc_on_dns_complete(App* app, addrinfo* result);
void irc_on_dns_failed(App* app, DWORD getaddrinfo_error);
void irc_on_connect(App* app);
void irc_on_write(App* app);
void irc_on_read_or_close(App* app);

void irc_handle_message(App* app, IrcMessage* msg);
void irc_queue_write(App* app, char* msg, bool is_privmsg);

better_internal char* parse_extract(char** ptr, char stop_at);

#endif // BETTER_IRC_H
