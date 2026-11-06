#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define NAME_SIZE 32

static int send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, data, len, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

static void *receive_messages(void *arg)
{
    int fd = *(int *)arg;
    char buffer[BUFFER_SIZE];

    for (;;) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n == 0) {
            puts("\n[Server disconnected]");
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            break;
        }
        if (fwrite(buffer, 1, (size_t)n, stdout) != (size_t)n ||
            fflush(stdout) == EOF) {
            perror("stdout");
            break;
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s SERVER_IP PORT USERNAME\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *end = NULL;
    long port = strtol(argv[2], &end, 10);
    if (!end || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Port must be between 1 and 65535.\n");
        return EXIT_FAILURE;
    }

    if (strlen(argv[3]) == 0 || strlen(argv[3]) >= NAME_SIZE ||
        strchr(argv[3], '\n') || strchr(argv[3], '\r')) {
        fprintf(stderr, "Username must contain 1-%d characters and no newline.\n",
                NAME_SIZE - 1);
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, argv[1], &server.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        close(fd);
        return EXIT_FAILURE;
    }

    if (connect(fd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("connect");
        close(fd);
        return EXIT_FAILURE;
    }
 char name_line[NAME_SIZE + 2];
    int name_len = snprintf(name_line, sizeof(name_line), "%s\n", argv[3]);
    if (send_all(fd, name_line, (size_t)name_len) == -1) {
        perror("send username");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Connected to %s:%ld as %s\n", argv[1], port, argv[3]);
    puts("Type a message and press Enter. Type /quit to leave.");

    pthread_t receiver;
    if (pthread_create(&receiver, NULL, receive_messages, &fd) != 0) {
        fprintf(stderr, "Could not create receiver thread\n");
        close(fd);
        return EXIT_FAILURE;
    }

    char line[BUFFER_SIZE - NAME_SIZE - 8];
    char message[BUFFER_SIZE];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        if (strcmp(line, "/quit\n") == 0 || strcmp(line, "/quit") == 0)
            break;

        int message_len = snprintf(message, sizeof(message),
                                   "[%s]: %s", argv[3], line);
        if (message_len < 0 || (size_t)message_len >= sizeof(message)) {
            fprintf(stderr, "Message is too long.\n");
            continue;
        }

        if (send_all(fd, message, (size_t)message_len) == -1) {
            perror("send");
            break;
        }
    }

    shutdown(fd, SHUT_WR);
    pthread_join(receiver, NULL);
    close(fd);
    puts("Disconnected.");
    return EXIT_SUCCESS;
}