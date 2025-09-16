#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

// Definiciones de constantes
#define MAX_CLIENTS 50
#define MAX_BUFFER_SIZE 8192
#define MAX_UPLOAD_SIZE (50 * 1024 * 1024)
#define MAX_IMAGE_SIZE_MB 50
#define DEFAULT_PORT 1717
#define DEFAULT_MAX_CONNECTIONS 10

// Macro para convertir a string
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// Estados del servidor
typedef enum
{
    SERVER_STOPPED,
    SERVER_STARTING,
    SERVER_RUNNING,
    SERVER_STOPPING
} server_status_t;

// Estructura para información de cliente
typedef struct
{
    int socket_fd;
    struct sockaddr_in address;
    char ip_str[INET_ADDRSTRLEN];
    pthread_t thread_id;
    int active;
    time_t connection_time;
} client_info_t;

// Estructura principal del servidor TCP
typedef struct
{
    int server_socket;
    struct sockaddr_in server_addr;
    server_status_t status;
    pthread_t server_thread;

    // Manejo de clientes
    client_info_t clients[MAX_CLIENTS];
    int client_count;
    pthread_mutex_t clients_mutex;
} tcp_server_t;

// Variable global del servidor
extern tcp_server_t main_server;

// Estructura para estadísticas de archivos
typedef struct
{
    int total_uploads;
    int successful_uploads;
    int failed_uploads;
    size_t total_bytes_processed;
} file_stats_t;

// Declaraciones de funciones
file_stats_t *get_file_stats(void);
void init_file_stats(void);
void log_file_stats(void);

// ================================
// FUNCIONES PRINCIPALES DEL SERVIDOR
// ================================

int init_server(void);
int start_server(void);
int stop_server(void);
void cleanup_server(void);

// ================================
// FUNCIONES DE HILOS Y CONEXIONES
// ================================

void *server_thread_func(void *arg);
int accept_client_connection(void);
int add_client(int socket_fd, struct sockaddr_in *client_addr);
void *client_handler_thread(void *arg);
void cleanup_inactive_clients(void);

// ================================
// FUNCIONES DE PROTOCOLO HTTP
// ================================

int receive_complete_request(int client_socket, char *buffer, size_t buffer_size, size_t *total_received);
int parse_http_request(const char *request, char *method, char *path);
int send_http_response(int client_socket, int status_code, const char *content_type,
                       const char *content, size_t content_length);

// ================================
// MANEJADORES DE PETICIONES HTTP
// ================================

int handle_get_request(int client_socket, const char *path, const char *client_ip);
int handle_post_request(int client_socket, const char *request_data, size_t request_len, const char *client_ip);

// ================================
// FUNCIONES DE UTILIDAD
// ================================

static inline server_status_t get_server_status(void)
{
    return main_server.status;
}

static inline int get_active_clients(void)
{
    return main_server.client_count;
}

static inline int is_server_running(void)
{
    return (main_server.status == SERVER_RUNNING);
}

// ================================
// FUNCIONES DE LOGGING Y ESTADÍSTICAS
// ================================
void mark_client_inactive(int client_socket);
void show_detailed_server_stats(void);

// ================================
// FUNCIONES DE RESPUESTA HTTP RÁPIDA
// ================================

static inline int send_success_response(int socket, const char *content_type, const char *content)
{
    return send_http_response(socket, 200, content_type, content, strlen(content));
}

static inline int send_error_response(int socket, int status_code, const char *message)
{
    char error_json[256];
    snprintf(error_json, sizeof(error_json), "{\"error\":\"%s\",\"code\":%d}", message, status_code);
    return send_http_response(socket, status_code, "application/json", error_json, strlen(error_json));
}

#endif // SERVER_H