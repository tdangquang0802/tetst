
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

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define NAME_SIZE 32

typedef struct {
    int fd;
    char name[NAME_SIZE];
} Client;

static Client clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Send all bytes */
static int send_all(int fd, const char *data, size_t length)
{
    while (length > 0) {
        ssize_t n = send(fd, data, length, MSG_NOSIGNAL);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0)
            return -1;

        data += n;
        length -= (size_t)n;
    }

    return 0;
}

/* Count connected users. Must hold mutex. */
static int count_clients_locked(void)
{
    int count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0)
            count++;
    }

    return count;
}

/* Broadcast message to all clients. Must hold mutex. */
static void broadcast_locked(const char *message, size_t length)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) {
            send_all(clients[i].fd, message, length);
        }
    }
}

/* Read username terminated by newline */
static int read_name(int fd, char *name, size_t capacity)
{
    size_t used = 0;

    while (used + 1 < capacity) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);

        if (n == 0)
            return -1;

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (ch == '\n')
            break;

        if (ch != '\r')
            name[used++] = ch;
    }

    name[used] = '\0';

    return used > 0 ? 0 : -1;
}

/* Remove disconnected client */
static void remove_client(int fd)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == fd) {
            char name[NAME_SIZE];
            char message[NAME_SIZE + 48];

            snprintf(name, sizeof(name), "%s", clients[i].name);

            clients[i].fd = -1;
            clients[i].name[0] = '\0';

            close(fd);

            printf("[SERVER] %s left the chat.\n", name);
            printf("[SERVER] Online users: %d/%d\n",
                   count_clients_locked(), MAX_CLIENTS);

            fflush(stdout);

            int n = snprintf(message, sizeof(message),
                             "*** %s left the chat ***\n", name);

            if (n > 0)
                broadcast_locked(message, (size_t)n);

            pthread_mutex_unlock(&clients_mutex);
            return;
        }
    }

    close(fd);
    pthread_mutex_unlock(&clients_mutex);
}

/* Handle each client */
static void *client_thread(void *arg)
{
    int fd = *(int *)arg;
    free(arg);

    char name[NAME_SIZE];

    /* Receive username */
    if (read_name(fd, name, sizeof(name)) == -1) {
        close(fd);
        return NULL;
    }

    pthread_mutex_lock(&clients_mutex);

    int slot = -1;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd < 0) {
            slot = i;
            break;
        }
    }

    /* Server full */
    if (slot == -1) {
        pthread_mutex_unlock(&clients_mutex);

        const char *msg =
            "Server full (maximum 10 users). Goodbye.\n";

        send_all(fd, msg, strlen(msg));
        close(fd);

        return NULL;
    }

    /* Register client */
    clients[slot].fd = fd;

    snprintf(clients[slot].name,
             sizeof(clients[slot].name), "%s", name);

    printf("\n[SERVER] %s joined the chat.\n", name);

    printf("[SERVER] Online users: %d/%d\n",
           count_clients_locked(), MAX_CLIENTS);

    fflush(stdout);

    /* Notify all clients */
    char joined[NAME_SIZE + 48];

    int joined_len = snprintf(
        joined, sizeof(joined),
        "*** %s joined the chat ***\n", name
    );

    if (joined_len > 0)
        broadcast_locked(joined, (size_t)joined_len);

    pthread_mutex_unlock(&clients_mutex);

    /*
     * TCP is a byte stream.
     * Reconstruct messages using newline as delimiter.
     */
    char receive_buffer[BUFFER_SIZE];
    char line_buffer[BUFFER_SIZE];

    size_t line_length = 0;
    int discard_line = 0;

    while (1) {
        ssize_t n = recv(fd, receive_buffer,
                         sizeof(receive_buffer), 0);

        if (n == 0)
            break;

        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        for (ssize_t j = 0; j < n; j++) {
            char ch = receive_buffer[j];

            if (discard_line) {
                if (ch == '\n')
                    discard_line = 0;
                continue;
            }

            if (ch == '\n') {
                if (line_length > 0 &&
                    line_buffer[line_length - 1] == '\r') {
                    line_length--;
                }

                if (line_length > 0) {
                    line_buffer[line_length] = '\0';

                    pthread_mutex_lock(&clients_mutex);

                    int active = 0;

                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (clients[i].fd == fd) {
                            active = 1;
                            break;
                        }
                    }

                    if (active) {
                        printf("[%s]: %s\n", name, line_buffer);
                        fflush(stdout);

                        /*
                         * Broadcast raw message.
                         * Compatible with clients that add
                         * their username before sending.
                         */
                        char message[BUFFER_SIZE + 1];

                        memcpy(message, line_buffer, line_length);
                        message[line_length] = '\n';

                        broadcast_locked(message, line_length + 1);
                    }

                    pthread_mutex_unlock(&clients_mutex);
                }

                line_length = 0;
                continue;
            }

            if (line_length < BUFFER_SIZE - 1) {
                line_buffer[line_length++] = ch;
            } else {
                line_length = 0;
                discard_line = 1;

                const char *warning =
                    "*** Message too long. ***\n";

                send_all(fd, warning, strlen(warning));
            }
        }
    }

    remove_client(fd);

    return NULL;
}

/* Main server */
int main(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Usage: %s PORT\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *end = NULL;
    long port = strtol(argv[1], &end, 10);

    if (end == argv[1] || *end != '\0' ||
        port < 1 || port > 65535) {
        printf("Invalid port number.\n");
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].fd = -1;

    /* Create TCP socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int reuse = 1;

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));

    /* Server address */
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((unsigned short)port);

    /* Bind */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    /* Listen */
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("========================================\n");
    printf("       TCP GROUP CHAT SERVER\n");
    printf("========================================\n");
    printf("Server listening on port: %ld\n", port);
    printf("IP address: 0.0.0.0\n");
    printf("Maximum clients: %d\n", MAX_CLIENTS);
    printf("========================================\n");

    fflush(stdout);

    /* Accept clients continuously */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_addr,
            &addr_len
        );

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN] = "unknown";

        inet_ntop(AF_INET, &client_addr.sin_addr,
                  client_ip, sizeof(client_ip));

        printf("[SERVER] Connection from %s:%u\n",
               client_ip,
               (unsigned)ntohs(client_addr.sin_port));

        fflush(stdout);

        int *arg = malloc(sizeof(int));

        if (arg == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }

        *arg = client_fd;

        pthread_t tid;

        int result = pthread_create(
            &tid, NULL, client_thread, arg
        );

        if (result != 0) {
            fprintf(stderr, "pthread_create: %s\n",
                    strerror(result));

            close(client_fd);
            free(arg);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_fd);

    return EXIT_SUCCESS;
}
