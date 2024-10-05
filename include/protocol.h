#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <malloc.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PROTOCOL_VERSION 1

// Function prototypes
ssize_t send_byte(int sockfd, uint8_t byte);
ssize_t send_uint16(int sockfd, uint16_t value);
int     send_header(int sockfd, uint8_t version, uint16_t content_size);
int     send_with_protocol(int sockfd, uint8_t version, const char *message);

ssize_t recv_byte(int sockfd, uint8_t *byte);
ssize_t recv_uint16(int sockfd, uint16_t *value);
int     read_header(int sockfd, uint8_t *version, uint16_t *content_size);
ssize_t read_with_protocol(int sockfd, uint8_t *version, char *buffer, size_t buffer_size);

#endif    // PROTOCOL_H
