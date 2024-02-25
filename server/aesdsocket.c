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
#include <pthread.h>
#include <sys/queue.h>
#include <sys/time.h>

#define PORT 9000
#define MAX_PACKET_SIZE 1024

int sockfd;
FILE *datafile;
pthread_mutex_t filewrite_mutex = PTHREAD_MUTEX_INITIALIZER;


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
    } else if (signal == SIGALRM)
    {
        // Get mutex lock
        if(pthread_mutex_lock(&filewrite_mutex) != 0) {
            syslog(LOG_ERR, "Failed to get mutex lock");
            return;
        }

        // Print with this "%a, %d %b %Y %T %z"
        time_t rawtime;
        struct tm *timeinfo;
        char *buffer = malloc(80);
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        strftime(buffer, 80, "timestamp:%a, %d %b %Y %T %z\n", timeinfo);
        fwrite(buffer, sizeof(char), strlen(buffer), datafile);
        fflush(datafile);
        free(buffer);
        if(pthread_mutex_unlock(&filewrite_mutex) != 0) {
            syslog(LOG_ERR, "Failed to release mutex lock");
            return;
        }

    }
    
}

struct connection_thread_args {
    int clientfd;
    struct sockaddr_in client_addr;
    bool completed;
};

struct thread_data {
    pthread_t thread;
    struct connection_thread_args args;
};

struct threads {
    pthread_t thread;
    struct connection_thread_args args;
    SLIST_ENTRY(threads) threads;
};

SLIST_HEAD(threadhead, threads) head;
int thread_count = 0;

void* handle_connection_thread(void* arg) {
    int clientfd = ((struct connection_thread_args*)arg)->clientfd;
    struct sockaddr_in client_addr = ((struct connection_thread_args*)arg)->client_addr;

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    char buffer[MAX_PACKET_SIZE];
    ssize_t bytes_received;
    bool completed = false;
    char *data = malloc(sizeof(char));
    if (data == NULL) {
        syslog(LOG_ERR, "Failed to allocate memory for data buffer");
        close(clientfd);
        return arg;
    }
    char *file_buffer = malloc(MAX_PACKET_SIZE);
    if (file_buffer == NULL) {
        syslog(LOG_ERR, "Failed to allocate memory for file buffer");
        close(clientfd);
        return arg;
    }

    // Get mutex lock
    if(pthread_mutex_lock(&filewrite_mutex) != 0) {
        syslog(LOG_ERR, "Failed to get mutex lock");
        close(clientfd);
        return arg;
    }

    while (!completed){
        bytes_received = recv(clientfd, data, sizeof(char), 0);
        if (bytes_received > 0) {
            if(*data == '\n') {
                completed = true;
            }
            fwrite(data, sizeof(char), bytes_received, datafile);
        } else {
            completed = true;
        }
    }
    fflush(datafile);
    fseek(datafile, 0, SEEK_SET);
    size_t file_size;
    do {
        file_size = fread(file_buffer, sizeof(char), MAX_PACKET_SIZE, datafile);
        send(clientfd, file_buffer, file_size, 0);
    } while (file_size == MAX_PACKET_SIZE);
    free(data);
    free(file_buffer);
    // Release mutex lock
    if(pthread_mutex_unlock(&filewrite_mutex) != 0) {
        syslog(LOG_ERR, "Failed to release mutex lock");
        close(clientfd);
        return arg;
    }

    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    close(clientfd);
    ((struct connection_thread_args*)arg)->completed = true;
    return arg;
}

void handle_connection(int clientfd, struct sockaddr_in client_addr) {
    struct threads *node = malloc(sizeof(struct threads));
    node->args.clientfd = clientfd;
    node->args.client_addr = client_addr;
    node->args.completed = false;

    if (pthread_create(&node->thread, NULL, handle_connection_thread, &node->args) != 0) {
        syslog(LOG_ERR, "Failed to create thread for connection handling");
        free(node);
        close(clientfd);
        return;
    }

    if(SLIST_EMPTY(&head)) {
        SLIST_INSERT_HEAD(&head, node, threads);
        thread_count = 1;
    } else {
        struct threads **marked_for_removal = malloc(sizeof(struct threads *) * thread_count);
        int i = 0;
        struct threads *current_thread;
        SLIST_FOREACH(current_thread, &head, threads) {
            // First check if the thread has completed
            if(current_thread->args.completed) {
                // Mark for removal
                marked_for_removal[i] = current_thread;
                i++;
                continue;
            }
            if(SLIST_NEXT(current_thread, threads) == NULL) {
                SLIST_INSERT_AFTER(current_thread, node, threads);
                thread_count++;
                break;
            }
        }
        while(i > 0) {
            SLIST_REMOVE(&head, marked_for_removal[i-1], threads, threads);
            if(pthread_join(marked_for_removal[i-1]->thread, NULL) != 0) {
                syslog(LOG_ERR, "Failed to join thread");
            }
            free(marked_for_removal[i-1]);
            i--;
        }
	free(marked_for_removal);
    }
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
    if(sigaction(SIGALRM, &sa, NULL) != 0) {
        syslog(LOG_ERR, "Failed to set signal handler for SIGALRM");
        closelog();
        return -1;
    }

    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_interval.tv_sec = 10;
    timer.it_value.tv_sec = 10;
    if(setitimer(ITIMER_REAL, &timer, NULL) != 0) {
        syslog(LOG_ERR, "Failed to set timer");
        closelog();
        return -1;
    }

    datafile = fopen("/var/tmp/aesdsocketdata", "w+");   
    if (datafile == NULL) {
        syslog(LOG_ERR, "Failed to open data file");
        close(sockfd);
        closelog();
        return -1;
    }

    SLIST_INIT(&head);

    if(pthread_mutex_init(&filewrite_mutex, NULL) != 0) {
        syslog(LOG_ERR, "Failed to initialize mutex");
        fclose(datafile);
        close(sockfd);
        closelog();
        return -1;
    }

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (clientfd == -1 && errno != EINTR) {
            syslog(LOG_ERR, "Failed to accept connection");
            close(sockfd);
            closelog();
            return -1;
        }

        handle_connection(clientfd, client_addr);
    }

    return 0;
}
