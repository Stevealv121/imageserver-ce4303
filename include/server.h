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
#define MAX_UPLOAD_SIZE (50 * 1024 * 1024) // 50MB máximo para archivos
#define MAX_IMAGE_SIZE_MB 50
#define DEFAULT_PORT 1717
#define DEFAULT_MAX_CONNECTIONS 10

// Macro para convertir a string
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// Estados del servidor
typedef enum {
    SERVER_STOPPED,
    SERVER_STARTING,
    SERVER_RUNNING,
    SERVER_STOPPING
} server_status_t;

// Estructura para información de cliente
typedef struct {
    int socket_fd;
    struct sockaddr_in address;
    char ip_str[INET_ADDRSTRLEN];
    pthread_t thread_id;
    int active;
    time_t connection_time;
} client_info_t;

// Estructura principal del servidor TCP
typedef struct {
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

// ================================
// FUNCIONES PRINCIPALES DEL SERVIDOR
// ================================

/**
 * Inicializar el servidor TCP
 * @return 1 si exitoso, 0 si error
 */
int init_server(void);

/**
 * Iniciar el servidor
 * @return 1 si exitoso, 0 si error
 */
int start_server(void);

/**
 * Detener el servidor
 * @return 1 si exitoso, 0 si error
 */
int stop_server(void);

/**
 * Limpiar todos los recursos del servidor
 */
void cleanup_server(void);

// ================================
// FUNCIONES DE HILOS Y CONEXIONES
// ================================

/**
 * Función principal del hilo del servidor
 * @param arg Parámetro del hilo (no usado)
 * @return NULL
 */
void* server_thread_func(void* arg);

/**
 * Aceptar nueva conexión de cliente
 * @return 0 si exitoso, -1 si error
 */
int accept_client_connection(void);

/**
 * Agregar cliente a la lista de clientes activos
 * @param socket_fd Socket del cliente
 * @param client_addr Dirección del cliente
 * @return índice del cliente o -1 si error
 */
int add_client(int socket_fd, struct sockaddr_in* client_addr);

/**
 * Función del hilo manejador de cliente
 * @param arg Puntero a client_info_t
 * @return NULL
 */
void* client_handler_thread(void* arg);

/**
 * Limpiar clientes inactivos
 */
void cleanup_inactive_clients(void);

// ================================
// FUNCIONES DE PROTOCOLO HTTP
// ================================

/**
 * Recibir petición HTTP completa con soporte para archivos grandes
 * @param client_socket Socket del cliente
 * @param buffer Buffer para almacenar la petición
 * @param buffer_size Tamaño del buffer
 * @param total_received Puntero para almacenar bytes totales recibidos
 * @return 0 si exitoso, -1 si error
 */
int receive_complete_request(int client_socket, char* buffer, size_t buffer_size, size_t* total_received);

/**
 * Parsear petición HTTP básica
 * @param request Petición HTTP completa
 * @param method Buffer para almacenar el método HTTP
 * @param path Buffer para almacenar la ruta
 * @return 0 si exitoso, -1 si error
 */
int parse_http_request(const char* request, char* method, char* path);

/**
 * Enviar respuesta HTTP completa
 * @param client_socket Socket del cliente
 * @param status_code Código de estado HTTP
 * @param content_type Tipo de contenido
 * @param content Contenido de la respuesta
 * @param content_length Longitud del contenido
 * @return 0 si exitoso, -1 si error
 */
int send_http_response(int client_socket, int status_code, const char* content_type, 
                       const char* content, size_t content_length);

// ================================
// MANEJADORES DE PETICIONES HTTP
// ================================

/**
 * Manejar petición GET
 * @param client_socket Socket del cliente
 * @param path Ruta solicitada
 * @param client_ip IP del cliente (para logging)
 * @return 0 si exitoso, -1 si error
 */
int handle_get_request(int client_socket, const char* path, const char* client_ip);

/**
 * Manejar petición POST no-multipart
 * @param client_socket Socket del cliente
 * @param request_data Datos de la petición
 * @param request_len Longitud de los datos
 * @param client_ip IP del cliente (para logging)
 * @return 0 si exitoso, -1 si error
 */
int handle_post_request(int client_socket, const char* request_data, size_t request_len, const char* client_ip);

// ================================
// FUNCIONES DE UTILIDAD
// ================================

/**
 * Obtener estado actual del servidor
 * @return Estado del servidor (server_status_t)
 */
static inline server_status_t get_server_status(void) {
    return main_server.status;
}

/**
 * Obtener número de clientes conectados
 * @return Número de clientes activos
 */
static inline int get_active_clients(void) {
    return main_server.client_count;
}

/**
 * Verificar si el servidor está ejecutándose
 * @return 1 si está ejecutándose, 0 si no
 */
static inline int is_server_running(void) {
    return (main_server.status == SERVER_RUNNING);
}

// ================================
// FUNCIONES DE RESPUESTA HTTP RÁPIDA
// ================================

/**
 * Enviar respuesta de éxito (200 OK)
 */
static inline int send_success_response(int socket, const char* content_type, const char* content) {
    return send_http_response(socket, 200, content_type, content, strlen(content));
}

/**
 * Enviar respuesta de error
 */
static inline int send_error_response(int socket, int status_code, const char* message) {
    char error_json[256];
    snprintf(error_json, sizeof(error_json), "{\"error\":\"%s\",\"code\":%d}", message, status_code);
    return send_http_response(socket, status_code, "application/json", error_json, strlen(error_json));
}

// ================================
// FUNCIONES EXTERNAS REQUERIDAS
// ================================

// Estas funciones deben estar implementadas en otros módulos:

// De logger.h
void LOG_INFO(const char* format, ...);
void LOG_ERROR(const char* format, ...);
void LOG_WARNING(const char* format, ...);
void LOG_DEBUG(const char* format, ...);

// De config.h
extern struct {
    int port;
    int max_connections;
    int max_image_size_mb;
    char supported_formats[256];
    char temp_dir[256];
    char output_dir[256];
} server_config;

// De file_handler.h
int handle_file_upload_request(int client_socket, const char* request_data, 
                              size_t request_len, const char* client_ip);
void init_file_stats(void);
void log_file_stats(void);
void log_client_activity(const char* client_ip, const char* file_name, 
                        const char* operation, const char* status);
int cleanup_old_temp_files(int hours_old);

typedef struct {
    int total_uploads;
    int successful_uploads;
    int failed_uploads;
    size_t total_bytes_processed;
} file_stats_t;

const file_stats_t* get_file_stats(void);

#endif // SERVER_H
