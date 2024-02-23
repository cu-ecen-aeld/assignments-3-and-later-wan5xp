#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>
#include <errno.h>


#define PORT 9000
#define MAX_PACKET_SIZE 1024

int sockfd;
FILE *datafile;

void handle_signal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        int status;
        do {
            status = close(sockfd);
        } while (status == -1 && (errno == EIO || errno == EINTR));

        if (datafile != NULL) {
            fclose(datafile);
            remove("/var/tmp/aesdsocketdata");
        }

        closelog();
        exit(0);
    }
}

void handle_connection(int clientfd, struct sockaddr_in client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    char buffer[MAX_PACKET_SIZE];
    ssize_t bytes_received;
    bool completed = false;
    char data;

    while (!completed){
        bytes_received = recv(clientfd, &data, sizeof(data), 0);
        if (bytes_received > 0) {
            if(data == '\n') {
                completed = true;
            }
            fwrite(&data, sizeof(char), bytes_received, datafile);
        } else {
            completed = true;
        }
    }
    fflush(datafile);
    fseek(datafile, 0, SEEK_SET);
    char file_buffer[MAX_PACKET_SIZE];
    size_t file_size;
    do {
        file_size = fread(file_buffer, sizeof(char), sizeof(file_buffer), datafile);
        send(clientfd, file_buffer, file_size, 0);
    } while (file_size == sizeof(file_buffer));

    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    close(clientfd);
}

int main(int argc, char *argv[]) {
    bool daemon_mode = false;
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    // Check if -d argument is present
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }

    struct addrinfo hints, *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, "9000", &hints, &servinfo) != 0) {
        syslog(LOG_ERR, "Failed to get address info");
        closelog();
        return -1;
    }

    // Loop through all the results and bind to the first we can
    struct addrinfo *p;
    bool bound = false;
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            syslog(LOG_ERR, "Failed to create socket");
            continue;
        }

        int yes = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            syslog(LOG_ERR, "Failed to set socket options");
            close(sockfd);
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            syslog(LOG_ERR, "Failed to bind socket, errno: %d", errno);
            close(sockfd);
            continue;
        }
        bound = true;
        break;
    }

    freeaddrinfo(servinfo);

    if (!bound) {
        syslog(LOG_ERR, "Failed to bind to any address");
        close(sockfd);
        closelog();
        return -1;
    }

    if (listen(sockfd, 10) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket");
        close(sockfd);
        closelog();
        return -1;
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid == -1) {
            syslog(LOG_ERR, "Failed to fork");
            close(sockfd);
            closelog();
            return -1;
        } else if (pid > 0) {
            // Parent process
            syslog(LOG_INFO, "Daemon started with PID %d", pid);
            close(sockfd);
            closelog();
            return 0;
        }

        if (setsid() == -1) {
            syslog(LOG_ERR, "Failed to create new session");
            close(sockfd);
            closelog();
            return -1;
        }

        if (chdir("/") == -1) {
            syslog(LOG_ERR, "Failed to change working directory");
            close(sockfd);
            closelog();
            return -1;
        }

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGINT, &sa, NULL) != 0) {
        syslog(LOG_ERR, "Failed to set signal handler for SIGINT");
        closelog();
        return -1;
    }
    if(sigaction(SIGTERM, &sa, NULL) != 0) {
        syslog(LOG_ERR, "Failed to set signal handler for SIGTERM");
        closelog();
        return -1;
    }

    datafile = fopen("/var/tmp/aesdsocketdata", "w+");    

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (clientfd == -1) {
            syslog(LOG_ERR, "Failed to accept connection");
            close(sockfd);
            closelog();
            return -1;
        }

        handle_connection(clientfd, client_addr);
    }

    return 0;
}
