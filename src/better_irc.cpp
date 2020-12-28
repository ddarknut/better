#pragma warning (disable: 4996) // This function or variable may be unsafe (strcpy, sprintf, ...)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cassert>
#include <cstdio>
// #include <cstdlib>
#include <vector>
#include <string>
#include <cerrno>
#include <algorithm>

#include "better.h"
#include "better_irc.h"
#include "better_ChatEntry.h"
#include "better_bets.h"
#include "better_func.h"

static DWORD _irc_main(App* app)
{
    addrinfo* result;
    addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    i32 res = getaddrinfo("irc.chat.twitch.tv", "6667", &hints, &result);

    if (res != 0)
    {
        if (!PostMessage(app->main_wnd, BETTER_WM_DNS_FAILED, (WPARAM)app, res))
        {
            return GetLastError();
        }

        return 0;
    }

    if (!PostMessage(app->main_wnd, BETTER_WM_DNS_COMPLETE, (WPARAM)app, (LPARAM)result))
    {
        return GetLastError();
    }

    return 0;
}

bool irc_init(App* app)
{
    add_log(app, LOGLEVEL_DEBUG, "Initializing Winsock.");

    i32 res = WSAStartup(MAKEWORD(2,2), &app->wsa_data);
    if (res != 0)
    {
        add_log(app, LOGLEVEL_DEVERROR, "WSAStartup failed: %i", res);
        abort();
    }

    return true;
}

void irc_cleanup(App* app)
{
    add_log(app, LOGLEVEL_DEBUG, "Cleaning up Winsock.");

    if (app->sock != INVALID_SOCKET)
    {
        closesocket(app->sock);
        app->sock = INVALID_SOCKET;
    }
    WSACleanup();
}

bool irc_connect(App* app)
{
    if (app->settings.channel[0] == '\0')
    {
        add_log(app, LOGLEVEL_USERERROR, "Cannot start connection: Channel name is empty.");
        return false;
    }

    if (app->settings.username[0] == '\0')
    {
        add_log(app, LOGLEVEL_USERERROR, "Cannot start connection: Username is empty.");
        return false;
    }

    if (!app->settings.oauth_token_is_present)
    {
        add_log(app, LOGLEVEL_USERERROR, "Cannot start connection: OAuth token is empty.");
        return false;
    }

    if (dns_thread_running(app))
    {
        add_log(app, LOGLEVEL_USERERROR, "Cannot start connection: Still waiting on another DNS request.");
        return false;
    }

    add_log(app, LOGLEVEL_INFO, "Sending DNS request...");

    app->dns_req_thread = CreateThread(NULL,
                                       0,
                                       (LPTHREAD_START_ROUTINE)_irc_main,
                                       app,
                                       0,
                                       &app->dns_req_thread_id);
    if (!app->dns_req_thread)
    {
        add_log(app, LOGLEVEL_DEVERROR, "CreateThread failed: %d", GetLastError());
        abort();
    }

    return true;
}

void irc_disconnect(App* app)
{
    add_log(app, LOGLEVEL_INFO, "Disconnecting from Twitch.");

    if (app->sock != INVALID_SOCKET)
        closesocket(app->sock);
    app->sock = INVALID_SOCKET;
    app->joined_channel = false;
}

void irc_schedule_reconnect(App* app)
{
    if (!SetTimer(app->main_wnd, TID_ALLOW_AUTO_RECONNECT, (UINT)MIN_RECONNECT_INTERVAL, NULL))
        add_log(app, LOGLEVEL_DEVERROR, "SetTimer failed: %d", GetLastError());
}

void irc_timed_reconnect(App* app)
{
    if (app->allow_auto_reconnect)
    {
        add_log(app, LOGLEVEL_INFO, "Attempting to reconnect...");

        if (app->sock != INVALID_SOCKET)
            closesocket(app->sock);
        app->sock = INVALID_SOCKET;
        app->joined_channel = false;

        irc_connect(app);
    }
    else irc_disconnect(app);
}

bool dns_thread_running(App* app)
{
    if (!app->dns_req_thread)
        return false;

    DWORD exit_code;
    if (!GetExitCodeThread(app->dns_req_thread, &exit_code))
    {
        add_log(app, LOGLEVEL_DEVERROR, "GetExitCodeThread failed: %d", GetLastError());
        abort();
    }

    if (exit_code == STILL_ACTIVE)
        return true;

    if (exit_code != 0)
    {
        add_log(app, LOGLEVEL_DEVERROR, "DNS thread failed with exit code: %d", exit_code);
        abort();
    }

    return false;
}

void irc_on_dns_complete(App* app, addrinfo* result)
{
    add_log(app, LOGLEVEL_INFO, "DNS request complete.");

    // Attempt to connect to the first address returned by
    // the call to getaddrinfo
    addrinfo* ptr=result;

    // Create a SOCKET for connecting to server
    app->sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

    if (app->sock == INVALID_SOCKET)
    {
        add_log(app, LOGLEVEL_DEVERROR, "Failed to create socket: %ld", WSAGetLastError());
        abort();
    }

    app->allow_auto_reconnect = false;
    if (!SetTimer(app->main_wnd, TID_ALLOW_AUTO_RECONNECT, MIN_RECONNECT_INTERVAL, NULL))
        add_log(app, LOGLEVEL_DEVERROR, "SetTimer failed: %d", GetLastError());

    i32 res = WSAAsyncSelect(app->sock,
                             app->main_wnd,
                             BETTER_WM_SOCK_MSG,
                             FD_CONNECT);
    assert(res != SOCKET_ERROR);

    res = connect(app->sock, ptr->ai_addr, (int)ptr->ai_addrlen);
    if (res == SOCKET_ERROR)
    {
        i32 err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK)
        {
            add_log(app, LOGLEVEL_DEVERROR, "connect failed: %i", err);
            abort();
        }
    }
    else assert(0 && "Made a blocking call to connect");
}

void irc_on_dns_failed(App* app, DWORD getaddrinfo_error)
{
    add_log(app, LOGLEVEL_USERERROR, "DNS request failed. Are you connected to the internet? (getaddrinfo returned %d)", getaddrinfo_error);
}

void irc_on_connect(App* app)
{
    if (app->sock == INVALID_SOCKET)
    {
        add_log(app, LOGLEVEL_DEVERROR, "Unable to connect to Twitch.");
        return;
    }

    add_log(app, LOGLEVEL_INFO, "Connected to Twitch.");

    // TODO: Should really try the next address returned by getaddrinfo (ptr->ai_next) if the connect call failed. But for this simple example we just free the resources returned by getaddrinfo and print an error message

    // freeaddrinfo(result);

    if (!SetTimer(app->main_wnd, TID_PRIVMSG_READY, get_privmsg_interval(app), NULL))
        add_log(app, LOGLEVEL_DEVERROR, "SetTimer failed: %d", GetLastError());

    char* sendbuf = (char*) malloc(SEND_BUFLEN+1);

    if (app->settings.username[0] == '\0')
    {
        add_log(app, LOGLEVEL_USERERROR, "Can't log in: Username is empty.");
        free(sendbuf);
        irc_disconnect(app);
    }
    else if (!app->settings.oauth_token_is_present)
    {
        add_log(app, LOGLEVEL_USERERROR, "Can't log in: OAuth token is empty.");
        free(sendbuf);
        irc_disconnect(app);
    }
    else
    {
        if (!CryptUnprotectMemory(app->settings.token, sizeof(app->settings.token), CRYPTPROTECTMEMORY_SAME_PROCESS))
        {
            add_log(app, LOGLEVEL_DEVERROR, "CryptUnprotectMemory failed: %i", GetLastError());
            SecureZeroMemory(app->settings.token, sizeof(app->settings.token));
            app->settings.oauth_token_is_present = false;
            return;
        }

        sprintf(sendbuf, "PASS %s\r\nNICK %s\r\n", app->settings.token, app->settings.username);

        if (!CryptProtectMemory(app->settings.token, sizeof(app->settings.token), CRYPTPROTECTMEMORY_SAME_PROCESS))
        {
            add_log(app, LOGLEVEL_DEVERROR, "CryptProtectMemory failed: %i", GetLastError());
            SecureZeroMemory(app->settings.token, sizeof(app->settings.token));
            app->settings.oauth_token_is_present = false;
            return;
        }

        add_log(app, LOGLEVEL_INFO, "Logging in as \"%s\"...", app->settings.username);

        irc_queue_write(app, sendbuf, false);
    }
}

void irc_send_buffer(App* app, char* buf)
{
    // printf("<%s", buf);
    i32 res = send(app->sock, buf, (i32)strlen(buf), 0);
    if (res == SOCKET_ERROR)
    {
        i32 err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK)
        {
            add_log(app, LOGLEVEL_DEVERROR, "Lost connection to Twitch. (send returned %i)", WSAGetLastError());
            irc_timed_reconnect(app);
        }
    }
}

void irc_on_write(App* app)
{
    if (app->sock == INVALID_SOCKET)
        return;

    while (app->write_queue.size() > 0)
    {
        char* buf = app->write_queue.front();
        irc_send_buffer(app, buf);
        app->write_queue.pop_front();
        SecureZeroMemory(buf, strlen(buf));
        free(buf);
    }

    while (app->privmsg_queue.size() > 0)
    {
        if (!app->privmsg_ready)
        {
            // TODO: Instead of ignoring the FD_WRITE event and immediately ask for a new one here, we can just wait with calling WSAAsyncSelect until we are *actually* ready to send a message. That way, we aren't constantly receiving FD_WRITE events and not acting on them even though privmsg_queue isn't empty.
            i32 res = WSAAsyncSelect(app->sock,
                                     app->main_wnd,
                                     BETTER_WM_SOCK_MSG,
                                     FD_WRITE | FD_READ | FD_CLOSE);
            assert(res != SOCKET_ERROR);
            break;
        }

        app->privmsg_ready = false;
        if (!SetTimer(app->main_wnd, TID_PRIVMSG_READY, get_privmsg_interval(app), NULL))
            add_log(app, LOGLEVEL_DEVERROR, "SetTimer failed: %d", GetLastError());

        char* buf = app->privmsg_queue.front();
        irc_send_buffer(app, buf);
        app->privmsg_queue.pop_front();
        SecureZeroMemory(buf, strlen(buf));
        free(buf);
    }
}

better_internal bool parse_extract(char** ptr, const char* const stop_at, char** res)
{
    char* begin = *ptr;
    while (!str_contains(stop_at, **ptr))
    {
        ++*ptr;
        if (**ptr == '\0')
        {
            // Unexpected null terminator. The message is not complete.
            return false;
        }
    }
    i32 len = (i32)(*ptr - begin);
    if (len > 0)
    {
        *res = (char*)malloc(len+1);
        strncpy(*res, begin, len);
        (*res)[len] = '\0';
    }
    else *res = NULL;
    return true;
}

better_internal char* parse_messages(App* app, char* const buf, i32 bytes)
{
    ////////////////////
    // parse messages //
    ////////////////////

    char* ptr = buf;

    while (ptr < buf + bytes)
    {
        IrcMessage msg;
        char* msg_begin = ptr;
        if (*ptr == ':')
        {
            // Message contains prefix
            ++ptr;
            if (!parse_extract(&ptr, " !@\r", &msg.name))
                return msg_begin;

            if (*ptr == '\r') goto message_end;

            while (*ptr != ' ') ++ptr;
            while (*ptr == ' ') ++ptr;
        }

        if (*ptr >= '0' && *ptr <= '9')
        {
            // The message is a numeric reply
            msg.command = (char*)malloc(4);
            strncpy(msg.command, ptr, 3);
            msg.command[3] = '\0';
            ptr += 3;
        }
        else if (!parse_extract(&ptr, " \r", &msg.command))
        {
            return msg_begin;
        }

        if (*ptr == '\r') goto message_end;

        while (*ptr == ' ')
        {
            while (*ptr == ' ') ++ptr;
            if (*ptr == '\r') // no params
                break;
            char* param;
            if (*ptr == ':')
            {
                // Trailing
                ++ptr;
                if (!parse_extract(&ptr, "\r", &param))
                    return msg_begin;
            }
            else
            {
                // Middle param
                if (!parse_extract(&ptr, " \r", &param))
                    return msg_begin;
            }
            if (param) msg.params.push_back(param);
        }

        message_end:
        if (msg.command) app->read_queue.push_back(msg);

        while ((ptr < buf + bytes) && (*ptr == '\r' || *ptr == '\n')) ++ptr;
    }

    return NULL;
}

void irc_on_read_or_close(App* app)
{
    better_persist char partial_msg[RECV_BUFLEN+1] = "";
    while(true)
    {
        char buf[2*RECV_BUFLEN+1];
        i32 bytes = recv(app->sock, buf+RECV_BUFLEN, RECV_BUFLEN, 0);
        if (bytes > 0)
        {
            if (bytes == RECV_BUFLEN)
                add_log(app, LOGLEVEL_DEBUG, "Received a max-length byte chunk.");

            i32 partial_size = (i32) strlen(partial_msg);
            char* messages_begin = buf + RECV_BUFLEN - partial_size;

            (buf + RECV_BUFLEN)[bytes] = '\0';
            // printf(">%s", buf+RECV_BUFLEN);

            if (partial_size > 0)
                memcpy(messages_begin, partial_msg, partial_size);

            char* rest = parse_messages(app, messages_begin, partial_size + bytes);
            if (rest) strcpy(partial_msg, rest);
            else *partial_msg = '\0';
        }
        else if (bytes == 0)
        {
            add_log(app, LOGLEVEL_WARN, "Connection closed by the server.");
            irc_timed_reconnect(app);
            break;
        }
        else
        {
            i32 err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK)
            {
                // So apparently twitch doesn't respect RFC1459's 512-byte limit -- they only guarantee that a message is no more than 512 *UNICODE CODEPOINTS*. Meaning messages can be as long as 2048 bytes. See: https://discuss.dev.twitch.tv/t/message-character-limit/7793/5
                // I've increased the buffer size, but I should probably do some testing and make sure the netcode can handle long strings of multibyte characters. I should probably also put in a safety check in front of the strcpy.

                add_log(app, LOGLEVEL_DEVERROR, "Lost connection to Twitch. (recv failed %i)", err);
                irc_timed_reconnect(app);
            }
            break;
        }
    }
}

bool is_alphabetic(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

void irc_handle_message(App* app, IrcMessage* msg)
{
    i32 numeric = atoi(msg->command);
    if (numeric)
    {
        switch (numeric)
        {
            case 1: // Welcome message
            {
                make_lower(app->settings.channel);
                make_lower(app->settings.username);

                if (msg->params.size() < 1)
                {
                    add_log(app, LOGLEVEL_DEVERROR, "Expected at least one parameter with welcome message.");
                    irc_disconnect(app);
                }
                else if (_stricmp(app->settings.username, msg->params[0]) != 0)
                {
                    add_log(app, LOGLEVEL_USERERROR, "Login failed: Username did not match the OAuth token's owner.", app->settings.channel);
                    irc_disconnect(app);
                }
                else
                {
                    add_log(app, LOGLEVEL_INFO, "Login successful. Joining channel \"%s\"...", app->settings.channel);

                    i32 max_len = 8 + CHANNEL_NAME_MAX;
                    char* join_msg = (char*)malloc(max_len+1);
                    sprintf(join_msg, "JOIN #%s\r\n", app->settings.channel);
                    join_msg[max_len] = '\0';

                    irc_queue_write(app, join_msg, false);
                }

                msg->free_all();
            } break;

            default:
            {
                msg->free_all();
            } break;
        }
    }
    else
    {
        if (strcmp(msg->command, "PRIVMSG") == 0)
        {
            char* name = msg->name;
            std::string s_name = std::string(name);
            char* msg_content = msg->params.back();

            // Add to chat log
            {
                i32 i = (app->chat_first_index + app->chat_count) % CHAT_BUFFER_MAX;
                ChatEntry* chat_entry = app->chat_buffer + i;
                if (app->chat_count == CHAT_BUFFER_MAX)
                {
                    chat_entry->free_all();
                    app->chat_first_index = (app->chat_first_index + 1) % CHAT_BUFFER_MAX;
                }
                else
                    ++app->chat_count;
                chat_entry->name = name;
                chat_entry->msg = msg_content;
            }

            // Free everything from the IRC message except name and trailing
            // param. The ChatEntry now owns those strings.
            {
                msg->params.pop_back();
                msg->free_params();
                free(msg->command);
                msg = NULL;
            }

            // Add user to leaderboard if they aren't there
            {
                auto it_points = app->points.find(s_name);
                if (it_points == app->points.end())
                {
                    app->points[s_name] = app->settings.starting_points;
                    app->leaderboard.push_back(s_name);
                    std::sort(app->leaderboard.begin(),
                              app->leaderboard.end(),
                              [&](std::string a, std::string b) {
                                  return app->points[a] > app->points[b];
                              });
                    it_points = app->points.find(s_name);
                }
            }

            //printf("'%s' ", msg_content);

            if (msg_content[0] == app->settings.command_prefix[0])
            {
                char command[CHAT_COMMAND_MAX+1];
                char* read = msg_content + 1;

                // Extract command
                {
                    char* write;
                    for (write = command;
                         write < command + CHAT_COMMAND_MAX && *read && *read != ' ';
                         ++read, ++write)
                        *write = *read;
                    *write = '\0';

                    for (; *read == ' '; ++read);
                }

                if (strlen(command) > 0)
                {
                    if (_stricmp(command, app->settings.points_name) == 0)
                    {
                        //////////////////////
                        // FEEDBACK COMMAND //
                        //////////////////////

                        app->point_feedback_queue.insert(s_name);
                    }
                    else if (bets_status(app) != BETS_STATUS_CLOSED &&
                             (_stricmp(command, "bet")  == 0 ||
                              _stricmp(command, "bets") == 0))
                    {
                        /////////////////
                        // BET COMMAND //
                        /////////////////

                        // Extract amount parameter
                        char amount_param[CHAT_PARAM_MAX+1];
                        {
                            char* write;
                            for (write = amount_param;
                                 write < amount_param + CHAT_PARAM_MAX && *read && *read != ' ';
                                 ++read, ++write)
                                *write = *read;
                            *write = '\0';

                            for (; *read == ' '; ++read);
                        }

                        if (strlen(amount_param) > 0)
                        {
                            // Extract trailing option parameter
                            char option_param[CHAT_PARAM_MAX+1];
                            {
                                char* write;
                                for (write = option_param;
                                     write < option_param + CHAT_PARAM_MAX && *read;
                                     ++read, ++write)
                                    *write = *read;
                                *write = '\0';
                            }

                            if (strlen(option_param) > 0)
                            {
                                char* end;

                                // Interpret option either as number (index) or a string (name)
                                i32 option = strtol(option_param, &end, 10);
                                if (errno == ERANGE || end == option_param)
                                {
                                    errno = 0;

                                    option = -1;
                                    i32 idx = 0;
                                    for (auto& table : app->bet_registry)
                                    {
                                        make_lower(option_param);
                                        if (_stricmp(option_param, table.option_name) == 0)
                                        {
                                            option = idx;
                                            break;
                                        }
                                        ++idx;
                                    }
                                }
                                else option -= 1; // When users refer to options by number, they are 1-indexed. Make it 0-indexed.

                                if (option >= 0)
                                {
                                    if (_stricmp(amount_param, "all") == 0)
                                        register_max_bet(app, &s_name, option);
                                    else
                                    {
                                        i64 amount = strtoll(amount_param, &end, 10);
                                        if (errno != ERANGE && end != amount_param)
                                        {
                                            errno = 0;
                                            register_bet(app, &s_name, amount, option);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        else if (strcmp(msg->command, "JOIN") == 0)
        {
            add_log(app, LOGLEVEL_INFO, "Join successful.");
            app->joined_channel = true;
            msg->free_all();
        }
        else if (strcmp(msg->command, "PING") == 0)
        {
            // TODO: Right now we always call free on messages when they're removed from the write queue. Therefore we always have to allocate them with malloc, even if they could be static like in this case. Probably should fix this.
            char* rpl_s = "PONG :tmi.twitch.tv\r\n";
            char* rpl = (char*) malloc(strlen(rpl_s)+1);
            strcpy(rpl, rpl_s);
            irc_queue_write(app, rpl, false);
            msg->free_all();
        }
        else if (strcmp(msg->command, "NOTICE") == 0)
        {
            if (msg->params.size() >= 2)
                add_log(app, LOGLEVEL_WARN, "Server notice: %s", msg->params[1]);
            msg->free_all();
        }
        else
        {
            add_log(app, LOGLEVEL_DEBUG, "Unhandled message (%s) (%s)", msg->name, msg->command);
            for (int i = 0; i < msg->params.size(); ++i)
                printf("%s\n", msg->params[i]);
            msg->free_all();
        }
    }
}

void irc_queue_write(App* app, char* msg, bool is_privmsg)
{
    if (app->sock == INVALID_SOCKET)
    {
        SecureZeroMemory(msg, strlen(msg));
        free(msg);
        return;
    }

    if (is_privmsg)
        app->privmsg_queue.push_back(msg);
    else
        app->write_queue.push_back(msg);

    i32 res = WSAAsyncSelect(app->sock,
                             app->main_wnd,
                             BETTER_WM_SOCK_MSG,
                             FD_WRITE | FD_READ | FD_CLOSE);
    assert(res != SOCKET_ERROR);
}
