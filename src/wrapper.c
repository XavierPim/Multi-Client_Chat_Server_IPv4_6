#include "../include/protocol.h"
#include "../include/server.h"
#include <stdbool.h>

int main(void)
{
    in_port_t               port;
    char                   *address  = NULL;
    char                   *port_str = NULL;
    struct sockaddr_storage addr;
    char                    input[MAX_INPUT_LENGTH];
    char                   *endptr;
    long                    choice;

    printf("****%s****\n", WELCOME_STARTUP);
    printf("1: %s\n", OPTION_NO_SM);
    printf("2: %s\n", OPTION_WITH_SM);

    // Prompt user for choice
    printf("\nEnter your choice (1 or 2): ");
    fgets(input, sizeof(input), stdin);
    choice = strtol(input, &endptr, BASE_TEN);

    // Switch statement based on user choice
    switch(choice)
    {
        case 1:
            printf("You chose option 1: No session management\n");
            handle_prompt(&address, &port_str);
            handle_arguments(address, port_str, &port);
            convert_address(address, &addr);
            start_groupChat_server(&addr, port, 0, 0);

            free(address);
            free(port_str);
            break;
        case 2:
            printf("You chose option 2: With session management\n");
            handle_prompt(&address, &port_str);
            handle_arguments(address, port_str, &port);
            convert_address(address, &addr);
            start_admin_server(&addr, port);

            free(address);
            free(port_str);
            break;
        default:
            printf("Invalid choice. Please enter 1 or 2.\n");
            printf("\nRandall be cute\n");
            break;
    }
    return 0;
}

void start_admin_server(struct sockaddr_storage *addr, in_port_t port)
{
    int                     server_socket;
    struct sockaddr_storage client_addr;
    socklen_t               client_addr_len;
    fd_set                  readfds;
    int                     max_sd;
    int                     pipe_fds[2];
    int                     server_manager_socket = 0;

    server_socket = socket_create(addr->ss_family, SOCK_STREAM, 0);
    socket_bind(server_socket, addr, port);
    start_listening(server_socket, BASE_TEN);
    admin_setup_signal_handler();

    // Create a pipe for communication between the admin server and the group chat server
    // if(pipe2(pipe_fds, O_CLOEXEC) == -1)    // use incase D'Arcy template
       if(pipe(pipe_fds) == -1) // use incase gcc
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    // Initialize the set of active sockets
    FD_ZERO(&readfds);
    FD_SET(server_socket, &readfds);
    FD_SET(pipe_fds[0], &readfds);
    max_sd = server_socket > pipe_fds[0] ? server_socket : pipe_fds[0];

    while(!admin_exit_flag)
    {
        // Wait for activity on the server socket or the pipe
        if(select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            if(errno == EINTR)
            {
                continue;    // Interrupted by signal, continue the loop
            }
            perror("select");
            exit(EXIT_FAILURE);
        }

        // Check if there is a new connection and no active server manager connection
        if(FD_ISSET(server_socket, &readfds) && server_manager_socket == 0)
        {
            server_manager_socket = handle_new_server_manager(server_socket, &client_addr, &client_addr_len, pipe_fds, addr, port);
            if(server_manager_socket > 0)
            {
                FD_SET(server_manager_socket, &readfds);
                if(server_manager_socket > max_sd)
                {
                    max_sd = server_manager_socket;
                }
            }
        }

        // Update the set of active sockets for the next iteration
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        FD_SET(pipe_fds[0], &readfds);
        if(server_manager_socket > 0)
        {
            FD_SET(server_manager_socket, &readfds);
        }
    }

    // Close the server socket and clean up
    close(server_socket);
    if(server_manager_socket > 0)
    {
        close(server_manager_socket);    // Close the server manager socket if it's still open
    }
}

void handle_prompt(char **address, char **port_str)
{
    printf("\nEnter the IP address to bind the server (default): ");
    *address = malloc(MAX_INPUT_LENGTH * sizeof(char));
    fgets(*address, MAX_INPUT_LENGTH, stdin);
    (*address)[strcspn(*address, "\n")] = 0;    // Remove newline character

    // Default to 192.168.0.247 if no input is detected
    if(strlen(*address) == 0)
    {
        free(*address);
        //        *address = strdup("192.168.0.247");
        *address = strdup("127.0.0.1");
        printf("No input det~ected. Defaulting to IP address: %s\n", *address);
    }

    printf("\nEnter the port to bind the server (default 8080): ");
    *port_str = malloc(MAX_INPUT_LENGTH * sizeof(char));
    fgets(*port_str, MAX_INPUT_LENGTH, stdin);
    (*port_str)[strcspn(*port_str, "\n")] = 0;    // Remove newline character

    // Default to 8080 if no input is detected
    if(strlen(*port_str) == 0)
    {
        free(*port_str);
        *port_str = strdup("8080");
        printf("No input detected. Defaulting to port: %s\n", *port_str);
    }
}

void admin_setup_signal_handler(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = admin_sigint_handler;
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

void admin_sigint_handler(int signum)
{
    (void)signum;
    admin_exit_flag = 1;
}

int handle_new_server_manager(int server_socket, struct sockaddr_storage *client_addr, socklen_t *client_addr_len, const int pipe_fds[2], struct sockaddr_storage *addr, in_port_t port)
{
    char    passkey_buffer[TWO_FIFTY_SIX];
    int     attempts        = 0;
    bool    passkey_matched = false;
    int     sm_socket       = accept(server_socket, (struct sockaddr *)client_addr, client_addr_len);
    pid_t   pid             = 0;
    uint8_t version         = PROTOCOL_VERSION;
    char    msg[BUFFER_SIZE];
    fd_set  readfds;
    int     max_sd;
    int     server_running = 0;

    if(sm_socket < 0)
    {
        perror("accept");
        return -1;
    }

    printf("New connection from %s:%d\n", inet_ntoa(((struct sockaddr_in *)client_addr)->sin_addr), ntohs(((struct sockaddr_in *)client_addr)->sin_port));

    // Authenticate the server manager connection
    while(attempts < 3 && !passkey_matched)
    {
        ssize_t bytes_received;

        // Read the message with protocol
        bytes_received = read_with_protocol(sm_socket, &version, passkey_buffer, BUFFER_SIZE);

        if(bytes_received <= 0)
        {
            printf("Connection closed or error occurred.\n");
            close(sm_socket);
            return -1;
        }

        // Remove the newline character if present
        if(passkey_buffer[bytes_received - 1] == '\n')
        {
            passkey_buffer[bytes_received - 1] = '\0';
        }

        if(strcmp(passkey_buffer, PASSKEY) == 0)
        {
            passkey_matched = true;
            snprintf(msg, sizeof(msg), PASSKEY_MATCHED_MSG);
            send_with_protocol(sm_socket, version, msg);
        }
        else
        {
            snprintf(msg, sizeof(msg), INCORRECT_PASSKEY_MSG, 2 - attempts);
            send_with_protocol(sm_socket, version, msg);
            attempts++;
        }
    }

    version = PROTOCOL_VERSION;
    if(!passkey_matched)
    {
        snprintf(msg, sizeof(msg), AUTH_FAILED_MSG);
        send_with_protocol(sm_socket, version, msg);
        close(sm_socket);
        return -1;
    }
    // this uses magic to do its thang
    max_sd = sm_socket > pipe_fds[0] ? sm_socket : pipe_fds[0];

    // Listen for commands to start or stop the group chat server
    while(1)
    {
        FD_ZERO(&readfds);
        FD_SET(sm_socket, &readfds);
        FD_SET(pipe_fds[0], &readfds);

        if(select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            if(errno == EINTR)
            {
                continue;    // Interrupted by signal, continue the loop
            }
            break;
        }

        // Handle commands from the server manager
        if(FD_ISSET(sm_socket, &readfds))
        {
            char    command_buffer[BUFFER_SIZE];
            uint8_t command_version;
            ssize_t command_received;
            // Read the command with protocol
            command_received = read_with_protocol(sm_socket, &command_version, command_buffer, BUFFER_SIZE);

            if(command_received <= 0)
            {
                printf("Connection closed or error occurred.\n");
                break;
            }

            if((strcmp(command_buffer, "/s") == 0 && pid == 0) || (strcmp(command_buffer, "/s\n") == 0 && pid == 0))    // Start the server
            {
                send_with_protocol(sm_socket, version, STARTING_SERVER_MSG);
                pid = fork();
                if(pid == 0)
                {
                    close(pipe_fds[0]);
                    start_groupChat_server(addr, port + 1, sm_socket, pipe_fds[1]);
                    //                    close(pipe_fds[1]);
                    //                    exit(EXIT_SUCCESS);
                }
                else if(pid > 0)
                {
                    //                    close(pipe_fds[1]);
                    printf("Group chat server started.\n");
                }
                else
                {
                    perror("Failed to start group chat server");
                }
                // turns on the read from pipe
                server_running = 1;
            }
            else if((strcmp(command_buffer, "/q") == 0 && pid > 0) || (strcmp(command_buffer, "/q\n") == 0 && pid > 0))    // Stop the server
            {
                kill(pid, SIGTERM);
                waitpid(pid, NULL, 0);
                pid = 0;
                printf(STOPPING_SERVER_MSG);
                if(send_with_protocol(sm_socket, version, STOPPING_SERVER_MSG) == -1)
                {
                    perror("Error sending stop server message with protocol");
                }
                server_running = 0;
            }
            else
            {
                printf("Unknown command: %s\n", command_buffer);
            }
        }

        if(FD_ISSET(pipe_fds[0], &readfds) && server_running == 1)
        {
            printf("is server running:%d", server_running);
            read_from_pipe(pipe_fds[0], sm_socket);
        }
    }

    // Clean up before exiting
    if(pid > 0)
    {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }
    //    close(sm_socket);
    return sm_socket;
}

ssize_t read_from_pipe(int pipe_fd, int server_manager_socket)
{
    int     received_client_count;
    ssize_t bytes_read;

    // Read the client count from the pipe
    bytes_read = read(pipe_fd, &received_client_count, sizeof(received_client_count));

    if(bytes_read > 0)
    {
        uint8_t version                = PROTOCOL_VERSION;
        char    count_str[BUFFER_SIZE] = {0};

        snprintf(count_str, BUFFER_SIZE, "/d %d", received_client_count);

        // Send this information to the server manager with protocol
        if(send_with_protocol(server_manager_socket, version, count_str) == -1)
        {
            perror("Failed to send client count to server manager with protocol");
        }
    }
    else if(bytes_read == 0)
    {
        printf("Group chat server closed the pipe.\n");
    }
    else
    {
        perror("Failed to read from pipe");
    }
    return bytes_read;
}
