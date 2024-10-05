#include "../include/server.h"
#include "../include/protocol.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-strict"

void *handle_client(const void *arg)
{
#pragma clang diagnostic pop
    const struct ClientInfo *client_info     = (const struct ClientInfo *)arg;
    int                      client_socket   = client_info->client_socket;
    int                      client_index    = client_info->client_index;
    const char              *client_username = client_info->username;

    while(1)
    {
        char    buffer[BUFFER_SIZE];
        uint8_t version;
        ssize_t bytes_received;

        // Read the message with protocol
        bytes_received = read_with_protocol(client_socket, &version, buffer, BUFFER_SIZE);

        if(bytes_received <= 0)
        {
            printf("%s left the chat.\n", client_username);
            break;
        }

        // Null-terminate the message (if not already done by read_with_protocol)
        buffer[bytes_received] = '\0';

        // Process the message
        printf("Received from %s: %s\n", client_username, buffer);
        handle_message(buffer, client_socket);
    }

    // Cleanup when the client disconnects
    close(client_socket);
    client_count--;
    printf("Population: %d/%d\n", client_count, MAX_CLIENTS);
    fflush(stdout);
    pthread_mutex_lock(&clients_mutex);
    clients[client_index].client_socket = 0;
    pthread_mutex_unlock(&clients_mutex);

    pthread_exit(NULL);
}

void start_groupChat_server(struct sockaddr_storage *addr, in_port_t port, int sm_socket, int pipe_write_fd)
{
    int                     server_socket;
    struct sockaddr_storage client_addr;
    socklen_t               client_addr_len;
    pthread_t               tid;
    uint8_t                 version = PROTOCOL_VERSION;

    server_socket = socket_create(addr->ss_family, SOCK_STREAM, 0);
    socket_bind(server_socket, addr, port);
    start_listening(server_socket, BASE_TEN);
    group_chat_setup_signal_handler();

    // Allocate memory for usernames
    for(int i = 0; i < MAX_CLIENTS; ++i)
    {
        clients[i].username = malloc(MAX_USERNAME_SIZE);
        if(clients[i].username == NULL)
        {
            perror("Memory allocation failed");
            free_usernames();
            exit(EXIT_FAILURE);
        }
    }

    while(!group_chat_exit_flag)
    {
        int    max_sd;
        int    activity;
        fd_set readfds;
        memset(&readfds, 0, sizeof(readfds));
        FD_SET(server_socket, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sm_socket, &readfds);
        FD_SET(pipe_write_fd, &readfds);
        max_sd = server_socket;
        for(int i = 0; i < MAX_CLIENTS; ++i)
        {
            if(clients[i].client_socket > 0)    // Check if the client socket is valid
            {
                FD_SET(clients[i].client_socket, &readfds);
                if(clients[i].client_socket > max_sd)
                {
                    max_sd = clients[i].client_socket;
                }
            }
        }

        // Wait for activity on one of the sockets
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if(activity == -1)
        {
            //             perror("select");
            continue;    // Keep listening for connections
        }

        // New connection
        if(FD_ISSET(server_socket, &readfds))
        {
            uint16_t           content_size;
            int                client_socket;
            int                client_index = -1;
            struct ClientInfo *client_info;
            char               welcome_message[BUFFER_SIZE];
            ssize_t            bytes_written;

            client_addr_len = sizeof(client_addr);
            client_socket   = socket_accept_connection(server_socket, &client_addr, &client_addr_len);

            if(client_socket == -1)
            {
                // TODO: error hand
                continue;    // Continue listening for connections
            }

            pthread_mutex_lock(&clients_mutex);

            for(int i = 0; i < MAX_CLIENTS; ++i)
            {
                if(clients[i].client_socket == 0)
                {
                    client_index = i;
                    client_count++;
                    break;
                }
            }

            if(client_index == -1)
            {
                const char *rejection_message = SERVER_FULL;
                // Use send_with_protocol to include the protocol header
                if(send_with_protocol(client_socket, version, rejection_message) == -1)
                {
                    perror("Error sending rejection message");
                }
                close(client_socket);
                pthread_mutex_unlock(&clients_mutex);
                continue;    // Continue listening for connections
            }

            printf("\nNew connection from %s:%d, assigned to Client%d\n", inet_ntoa(((struct sockaddr_in *)&client_addr)->sin_addr), ntohs(((struct sockaddr_in *)&client_addr)->sin_port), client_index + 1);
            printf("Population: %d/%d\n", client_count, MAX_CLIENTS);

            // Send the updated client count to the admin server
            bytes_written = write(pipe_write_fd, &client_count, sizeof(client_count));

            if(bytes_written != sizeof(client_count))
            {
                perror("Failed to write client count to pipe");
            }
            printf("client count sending to wrapper: %d\n", client_count);

            fflush(stdout);

            clients[client_index].client_socket = client_socket;
            clients[client_index].client_index  = client_index;
            snprintf(clients[client_index].username, MAX_USERNAME_SIZE, "Client%d", client_index + 1);

            pthread_mutex_unlock(&clients_mutex);

            // Create the welcome message
            sprintf(welcome_message, "%s%s!\n\n", WELCOME_MESSAGE, clients[client_index].username);
            content_size = (uint16_t)strlen(welcome_message);

            // Send the welcome message with protocol header
            if(send_header(client_socket, version, content_size) == -1)
            {
                perror("Error sending welcome header");
                close(client_socket);
                return;
            }
            if(send(client_socket, welcome_message, content_size, 0) == -1)
            {
                perror("Error sending welcome message");
                close(client_socket);
                return;
            }

            // Send the command list with protocol header
            content_size = strlen(COMMAND_LIST);
            if(send_header(client_socket, version, content_size) == -1)
            {
                perror("Error sending command list header");
                close(client_socket);
                return;
            }
            if(send(client_socket, COMMAND_LIST, content_size, 0) == -1)
            {
                perror("Error sending command list");
                close(client_socket);
                return;
            }

            // Create a new thread to handle the client
            client_info = &clients[client_index];    // Pass the address of the struct in the array
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type"
            if(pthread_create(&tid, NULL, (void *(*)(void *))handle_client, (void *)client_info) != 0)
            {
                perror("Thread creation failed");
                close(client_socket);
                continue;
            }
#pragma clang diagnostic pop
            pthread_detach(tid);
        }
    }

    for(int i = 0; i < MAX_CLIENTS; ++i)
    {
        if(clients[i].client_socket != 0)
        {
            if(send_with_protocol(clients[i].client_socket, version, SHUTDOWN_MESSAGE) == -1)
            {
                perror("Error sending shutdown message with protocol");
            }
        }
    }

    // Close server socket
    shutdown(server_socket, SHUT_RDWR);
    socket_close(server_socket);
    free_usernames();
}

void handle_message(const char *buffer, int sender_fd)
{
    uint8_t version = PROTOCOL_VERSION;
    if(buffer[0] == '/')
    {
        // Extract command
        char command[BUFFER_SIZE];
        sscanf(buffer, "/%19s", command);

        // Check command and call corresponding function
        if(strcmp(command, "h") == 0)
        {
            if(send_with_protocol(sender_fd, version, COMMAND_LIST) == -1)
            {
                perror("Error sending command list with protocol");
            }
        }
        else if(strcmp(command, "ul") == 0)
        {
            send_user_list(sender_fd);
        }
        else if(strcmp(command, "u") == 0)
        {
            set_username(sender_fd, buffer);
        }
        else if(strcmp(command, "w") == 0)
        {
            direct_message(sender_fd, buffer);
        }
        else
        {
            if(send_with_protocol(sender_fd, version, COMMAND_NOT_FOUND) == -1)
            {
                perror("Error sending 'command not found' message with protocol");
            }
        }
    }
    else
    {
        char message_with_sender[MESSAGE_SIZE];

        for(int i = 0; i < MAX_CLIENTS; ++i)
        {
            if(clients[i].client_socket == sender_fd)
            {
                sprintf(message_with_sender, "[All] %s: %s", clients[i].username, buffer);
                break;
            }
        }

        pthread_mutex_lock(&clients_mutex);

        for(int i = 0; i < MAX_CLIENTS; ++i)
        {
            if(clients[i].client_socket != 0 && clients[i].client_socket != sender_fd)
            {
                // Use the send_with_protocol function to send the message
                if(send_with_protocol(clients[i].client_socket, version, message_with_sender) == -1)
                {
                    // Handle the error case here if needed
                    fprintf(stderr, "Error sending message to client %d\n", i);
                }
            }
        }
        pthread_mutex_unlock(&clients_mutex);
    }
}

void send_user_list(int sender_fd)
{
    uint8_t version = PROTOCOL_VERSION;
    char    user_list[BUFFER_SIZE];
    memset(user_list, 0, sizeof(user_list));    // Initialize user_list

    // Copy "USER LIST" to user_list
    strncpy(user_list, "USER LIST\n", sizeof(user_list) - 1);    // Use strncpy to avoid buffer overflow

    // Concatenate each user name to the message
    for(int i = 0; i < MAX_CLIENTS; ++i)
    {
        // Check if the client socket is valid and username is not NULL
        if(clients[i].client_socket != 0 && clients[i].username != NULL)
        {
            // Concatenate username to user_list
            strncat(user_list, clients[i].username, sizeof(user_list) - strlen(user_list) - 1);    // Use strncat to avoid buffer overflow

            if(sender_fd == clients[i].client_socket)
            {
                strncat(user_list, "(you)", sizeof(user_list) - strlen(user_list) - 1);
            }

            strncat(user_list, "\n", sizeof(user_list) - strlen(user_list) - 1);    // Add newline character
        }
    }

    // Send user_list to the sender_fd with protocol
    if(send_with_protocol(sender_fd, version, user_list) == -1)
    {
        perror("Error sending user list with protocol");
    }
}

void set_username(int sender_fd, const char *buffer)
{
    char    command[BASE_TEN];
    char    username[MAX_USERNAME_SIZE];    // Adjusted size to match the maximum username size
    char    nothing[BUFFER_SIZE];           // Buffer to capture any extra input
    char    response[BUFFER_SIZE];
    uint8_t version = PROTOCOL_VERSION;

    // Adjusted the sscanf format string to limit the username size
    if(sscanf(buffer, "/%9s %14s %999s", command, username, nothing) != 2)
    {
        if(send_with_protocol(sender_fd, version, INVALID_NUM_ARGS) == -1)
        {
            perror("Error sending invalid arguments message with protocol");
        }
        return;
    }

    // Removed the check for username length as it's now enforced by sscanf

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(strcmp(clients[i].username, username) == 0)
        {
            if(send_with_protocol(sender_fd, version, USERNAME_FAILURE) == -1)
            {
                perror("Error sending username failure message with protocol");
            }
            return;
        }
    }

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(sender_fd == clients[i].client_socket)
        {
            strncpy(clients[i].username, username, MAX_USERNAME_SIZE - 1);    // Use strncpy to prevent overflow
            clients[i].username[MAX_USERNAME_SIZE - 1] = '\0';                // Ensure null termination
            break;
        }
    }

    snprintf(response, sizeof(response), "%s%s.\n", USERNAME_SUCCESS, username);
    if(send_with_protocol(sender_fd, version, response) == -1)
    {
        perror("Error sending username success message with protocol");
    }
}

void direct_message(int sender_fd, const char *buffer)
{
    char    command[BASE_TEN];
    char    receiver[MAX_USERNAME_SIZE + 1];
    char    message[BUFFER_SIZE];
    char    sent_message[MESSAGE_SIZE];    // Adjust the size to accommodate the maximum possible message length
    int     sender_id;
    uint8_t version = PROTOCOL_VERSION;

    if(sscanf(buffer, "/%9s %14s %1023[^\n]", command, receiver, message) != 3)
    {
        // Use send_with_protocol to send the invalid number of arguments message
        if(send_with_protocol(sender_fd, version, INVALID_NUM_ARGS) == -1)
        {
            perror("Error sending invalid number of arguments message");
        }
        return;
    }

    for(sender_id = 0; sender_id < MAX_CLIENTS; sender_id++)
    {
        if(clients[sender_id].client_socket == sender_fd)
        {
            break;
        }
    }

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(strcmp(clients[i].username, receiver) == 0)
        {
            if(sender_id == i)
            {
                snprintf(sent_message, sizeof(sent_message), "[Note] %s: %s", clients[sender_id].username, message);
            }
            else
            {
                snprintf(sent_message, sizeof(sent_message), "[Direct] %s: %s", clients[sender_id].username, message);
            }

            // Use send_with_protocol to send the direct message
            if(send_with_protocol(clients[i].client_socket, version, sent_message) == -1)
            {
                perror("Error sending direct message");
            }
            return;
        }
    }

    // Use send_with_protocol to send the invalid receiver message
    if(send_with_protocol(sender_fd, version, INVALID_RECEIVER) == -1)
    {
        perror("Error sending invalid receiver message");
    }
}

void free_usernames(void)
{
    for(int i = 0; i < MAX_CLIENTS; ++i)
    {
        if(clients[i].username != NULL)
        {
            free(clients[i].username);
            clients[i].username = NULL;    // Optional: Set the pointer to NULL after freeing
        }
    }
}

void handle_arguments(const char *ip_address, const char *port_str, in_port_t *port)
{
    if(ip_address == NULL)
    {
        printf("ip is null\n");
        exit(EXIT_FAILURE);
    }

    if(port_str == NULL)
    {
        printf("port str is null\n");
        exit(EXIT_FAILURE);
    }

    *port = parse_in_port_t(port_str);
}

in_port_t parse_in_port_t(const char *str)
{
    char     *endptr;
    uintmax_t parsed_value;

    errno        = 0;
    parsed_value = strtoumax(str, &endptr, BASE_TEN);

    if(errno != 0)
    {
        perror("Error parsing in_port_t\n");
        exit(EXIT_FAILURE);
    }

    if(*endptr != '\0')
    {
        printf("non-numerics inside port\n");
        exit(EXIT_FAILURE);
    }

    if(parsed_value > UINT16_MAX)
    {
        printf("port out of range\n");
        exit(EXIT_FAILURE);
    }

    return (in_port_t)parsed_value;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

#pragma GCC diagnostic pop

void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;
    }
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

int socket_create(int domain, int type, int protocol)
{
    int sockfd;
    int opt = 1;

    sockfd = socket(domain, type, protocol);

    if(sockfd == -1)
    {
        perror("Socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    socklen_t addr_len;
    void     *vaddr;
    in_port_t net_port;

    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        addr_len            = sizeof(*ipv4_addr);
        ipv4_addr->sin_port = net_port;
        vaddr               = (void *)&(((struct sockaddr_in *)addr)->sin_addr);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        addr_len             = sizeof(*ipv6_addr);
        ipv6_addr->sin6_port = net_port;
        vaddr                = (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr);
    }
    else
    {
        fprintf(stderr,
                "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: "
                "%d\n",
                addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(inet_ntop(addr->ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop\n");
        exit(EXIT_FAILURE);
    }

    if(bind(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        perror("Binding failed");
        fprintf(stderr, "Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    printf("Bound to socket: %s:%u\n", addr_str, port);
}

void start_listening(int server_fd, int backlog)
{
    if(listen(server_fd, backlog) == -1)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");
}

int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len)
{
    int  client_fd;
    char client_host[NI_MAXHOST];
    char client_service[NI_MAXSERV];

    errno     = 0;
    client_fd = accept(server_fd, (struct sockaddr *)client_addr, client_addr_len);

    if(client_fd == -1)
    {
        if(errno != EINTR)
        {
            perror("accept failed\n");
        }

        return -1;
    }

    if(getnameinfo((struct sockaddr *)client_addr, *client_addr_len, client_host, NI_MAXHOST, client_service, NI_MAXSERV, 0) == 0)
    {
        // printf("Received new request from -> %s:%s\n\n", client_host, client_service);
    }
    else
    {
        printf("Unable to get client information\n");
    }

    return client_fd;
}

void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket\n");
        exit(EXIT_FAILURE);
    }
}

void group_chat_sigint_handler(int signum)
{
    (void)signum;
    group_chat_exit_flag = 1;
}

void group_chat_setup_signal_handler(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = group_chat_sigint_handler;
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if(sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}
