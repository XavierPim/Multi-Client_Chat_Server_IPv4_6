#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static void setup_signal_handler(void);

static void sigint_handler(int signum);

static void parse_arguments(int argc, char *argv[], char **ip_address, char **port);

static void handle_arguments(const char *ip_address, const char *port_str, in_port_t *port);

static in_port_t parse_in_port_t(const char *port_str);

static void convert_address(const char *address, struct sockaddr_storage *addr);

static int socket_create(int domain, int type, int protocol);

static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);

static void handle_connection(int client_sockfd);

static void socket_close(int sockfd);

#define EXPECTED_NUM_ARGS 3
#define IP_ADDR_INDEX 1
#define PORT_INDEX 2
#define BASE_TEN 10
#define BUFFER_SIZE 1000

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t exit_flag = 0;

int main(int argc, char *argv[])
{
    char *address;
    char *port_str;

    in_port_t port;

    int                     sockfd;
    struct sockaddr_storage addr;

    //    int bindResult;

    address  = NULL;
    port_str = NULL;

    parse_arguments(argc, argv, &address, &port_str);
    handle_arguments(address, port_str, &port);
    convert_address(address, &addr);
    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
    //    bindResult = socket_bind(sockfd, &addr, port);

    setup_signal_handler();

    //    if(bindResult != -1)
    //    {
    //        struct sockaddr_storage client_addr;
    //        socklen_t               client_addr_len = sizeof(client_addr);
    //        int                     client_sockfd;
    //        start_listening(sockfd);
    //
    //        client_sockfd = socket_accept_connection(sockfd, &client_addr, &client_addr_len);
    //        if(client_sockfd != -1)
    //        {
    //            handle_connection(client_sockfd);
    //            socket_close(client_sockfd);
    //        }
    //    }
    //    else
    //    {
    //        socket_connect(sockfd, &addr, port);
    //        handle_connection(sockfd);
    //    }

    socket_connect(sockfd, &addr, port);
    handle_connection(sockfd);

    socket_close(sockfd);

    return EXIT_SUCCESS;
}

static void parse_arguments(int argc, char *argv[], char **ip_address, char **port)
{
    if(argc == EXPECTED_NUM_ARGS)
    {
        *ip_address = argv[IP_ADDR_INDEX];
        *port       = argv[PORT_INDEX];
    }
    else
    {
        printf("invalid num args\n");
        printf("usage: ./chat [ip addr] [port]\n");
        printf("usage: ./chat [ip addr] [port] < [.txt file]\n");
        exit(EXIT_FAILURE);
    }
}

static void handle_arguments(const char *ip_address, const char *port_str, in_port_t *port)
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

static void sigint_handler(int signum)
{
    exit_flag = 1;
}

#pragma GCC diagnostic pop

static void convert_address(const char *address, struct sockaddr_storage *addr)
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

static int socket_create(int domain, int type, int protocol)
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

static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    in_port_t net_port;
    socklen_t addr_len;

    if(inet_ntop(addr->ss_family, addr->ss_family == AF_INET ? (void *)&(((struct sockaddr_in *)addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr), addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop\n");
        exit(EXIT_FAILURE);
    }

    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        ipv4_addr->sin_port = net_port;
        addr_len            = sizeof(struct sockaddr_in);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        ipv6_addr->sin6_port = net_port;
        addr_len             = sizeof(struct sockaddr_in6);
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(connect(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        printf("Port out of range or non existent ip addr\n");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to: %s:%u\n", addr_str, port);
    printf("You are now chatting with the host of %s:%u\n", addr_str, port);
}

#ifdef __clang__
    #pragma GCC diagnostic ignored "-Wdisabled-macro-expansion"
#endif

static void setup_signal_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void handle_connection(int sockfd)
{
    int flags;

    flags = fcntl(sockfd, F_GETFD);
    if(flags == -1)
    {
        close(sockfd);
        perror("Error getting flags on socket");
        exit(EXIT_FAILURE);
    }

    flags |= FD_CLOEXEC;
    if(fcntl(sockfd, F_SETFD, flags) == -1)
    {
        close(sockfd);
        perror("Error setting FD_CLOEXEC on socket");
        exit(EXIT_FAILURE);
    }

    //    printf("Connected to the server. Type your messages and press Enter to send. "
    //           "Press Ctrl-c to exit or Ctrl-D to close the Server Connection.\n");

    // Start a simple chat loop
    while(1)
    {
        int    activity;
        fd_set readfds;
        memset(&readfds, 0, sizeof(readfds));
        FD_SET((long unsigned int)sockfd, &readfds);
        FD_SET((long unsigned int)STDIN_FILENO, &readfds);

        // Wait for activity on the socket or user input
        activity = select(sockfd + 1, &readfds, NULL, NULL, NULL);

        if(activity < 0)
        {
            //            perror("Select error");
            break;
        }

        // Check if there is a message from the server or other clients
        if(FD_ISSET((long unsigned int)sockfd, &readfds))
        {
            char    server_buffer[BUFFER_SIZE];
            ssize_t bytes_received = recv(sockfd, server_buffer, sizeof(server_buffer) - 1, 0);

            if(bytes_received <= 0)
            {
                // this should be given from server side.
                //                printf("\nServer closed the connection.\n");
                break;
            }

            server_buffer[bytes_received] = '\0';

            printf("%s", server_buffer);
            fflush(stdout);
        }

        // Check if there is user input
        if(FD_ISSET((long unsigned int)STDIN_FILENO, &readfds))
        {
            char client_buffer[BUFFER_SIZE];
            if(fgets(client_buffer, sizeof(client_buffer), stdin) == NULL)
            {
                // Ctrl-D was pressed, causing EOF
                printf("EOF detected. Closing connection.\n");
                break;
            }

            if(send(sockfd, client_buffer, strlen(client_buffer), 0) == -1)
            {
                perror("Error sending message");
                break;
            }
        }
    }

    // Close the client socket when the loop exits
    close(sockfd);
}

#pragma GCC diagnostic pop

static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        //        perror("Error closing socket\n");
        exit(EXIT_FAILURE);
    }
}
