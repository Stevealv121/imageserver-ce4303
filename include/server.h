#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_BUFFER_SIZE 8192
#define MAX_FILENAME_SIZE 256
#define MAX_CLIENTS 100

// Estados del servidor
typedef enum {
    SERVER_STOPPED = 0,
    SERVER_STARTING = 1,
    SERVER_RUNNING = 2,
    SERVER_STOPPING = 3
} server_status_t;

// Estructura para información del cliente
typedef struct {
    int socket_fd;
    struct sockaddr_in address;
    char ip_str[INET_ADDRSTRLEN];
    pthread_t thread_id;
    int active;
} client_info_t;

// Estructura para el servidor
typedef struct {
    int server_socket;
    struct sockaddr_in server_addr;
    server_status_t status;
    pthread_t server_thread;
    client_info_t clients[MAX_CLIENTS];
    pthread_mutex_t clients_mutex;
    int client_count;
} tcp_server_t;

// Variable global del servidor
extern tcp_server_t main_server;

// Funciones principales del servidor
int init_server(void);
int start_server(void);
int stop_server(void);
void cleanup_server(void);

// Funciones de manejo de conexiones
void* server_thread_func(void* arg);
void* client_handler_thread(void* arg);
int accept_client_connection(void);

// Funciones auxiliares
int add_client(int socket_fd, struct sockaddr_in* client_addr);
void remove_client(int client_index);
void cleanup_inactive_clients(void);
int send_http_response(int client_socket, int status_code, const char* content_type, 
                       const char* content, size_t content_length);

// Funciones para manejo HTTP
int parse_http_request(const char* request, char* method, char* path, char* filename);
int handle_file_upload(int client_socket, const char* filename, const char* request, size_t request_len);

#endif // SERVER_H
