#include "server.h"
#include "logger.h"
#include "config.h"

// Variable global del servidor
tcp_server_t main_server;

// Inicializar el servidor
int init_server(void) {
    LOG_INFO("Inicializando servidor TCP...");
    
    // Inicializar estructura del servidor
    memset(&main_server, 0, sizeof(tcp_server_t));
    main_server.status = SERVER_STOPPED;
    main_server.server_socket = -1;
    main_server.client_count = 0;
    
    // Inicializar mutex para clientes
    if (pthread_mutex_init(&main_server.clients_mutex, NULL) != 0) {
        LOG_ERROR("Error inicializando mutex de clientes: %s", strerror(errno));
        return 0;
    }
    
    // Crear socket del servidor
    main_server.server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (main_server.server_socket == -1) {
        LOG_ERROR("Error creando socket: %s", strerror(errno));
        return 0;
    }
    
    // Configurar opción SO_REUSEADDR
    int opt = 1;
    if (setsockopt(main_server.server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARNING("Error configurando SO_REUSEADDR: %s", strerror(errno));
    }
    
    // Configurar dirección del servidor
    memset(&main_server.server_addr, 0, sizeof(main_server.server_addr));
    main_server.server_addr.sin_family = AF_INET;
    main_server.server_addr.sin_addr.s_addr = INADDR_ANY;
    main_server.server_addr.sin_port = htons(server_config.port);
    
    // Bind del socket
    if (bind(main_server.server_socket, (struct sockaddr*)&main_server.server_addr, 
             sizeof(main_server.server_addr)) < 0) {
        LOG_ERROR("Error en bind puerto %d: %s", server_config.port, strerror(errno));
        close(main_server.server_socket);
        return 0;
    }
    
    LOG_INFO("Servidor inicializado correctamente en puerto %d", server_config.port);
    return 1;
}

// Iniciar el servidor
int start_server(void) {
    if (main_server.status == SERVER_RUNNING) {
        LOG_WARNING("El servidor ya está ejecutándose");
        return 1;
    }
    
    main_server.status = SERVER_STARTING;
    LOG_INFO("Iniciando servidor TCP...");
    
    // Poner socket en modo escucha
    if (listen(main_server.server_socket, server_config.max_connections) < 0) {
        LOG_ERROR("Error en listen: %s", strerror(errno));
        main_server.status = SERVER_STOPPED;
        return 0;
    }
    
    // Crear hilo del servidor
    if (pthread_create(&main_server.server_thread, NULL, server_thread_func, NULL) != 0) {
        LOG_ERROR("Error creando hilo del servidor: %s", strerror(errno));
        main_server.status = SERVER_STOPPED;
        return 0;
    }
    
    main_server.status = SERVER_RUNNING;
    LOG_INFO("Servidor TCP iniciado - Escuchando en puerto %d", server_config.port);
    LOG_INFO("Máximo de conexiones: %d", server_config.max_connections);
    
    return 1;
}

// Hilo principal del servidor
void* server_thread_func(void* arg) {
    (void)arg; // Evitar warning de parámetro no usado
    
    LOG_INFO("Hilo del servidor iniciado");
    
    while (main_server.status == SERVER_RUNNING) {
        // Aceptar nueva conexión
        if (accept_client_connection() < 0) {
            if (main_server.status == SERVER_RUNNING) {
                LOG_ERROR("Error aceptando conexión");
                usleep(100000); // Esperar 100ms antes de intentar de nuevo
            }
        }
        
        // Limpiar clientes inactivos periódicamente
        cleanup_inactive_clients();
    }
    
    LOG_INFO("Hilo del servidor terminando...");
    return NULL;
}

// Aceptar conexión de cliente
int accept_client_connection(void) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_socket;
    
    // Aceptar conexión
    client_socket = accept(main_server.server_socket, (struct sockaddr*)&client_addr, &client_len);
    if (client_socket < 0) {
        if (errno != EINTR && main_server.status == SERVER_RUNNING) {
            LOG_ERROR("Error en accept: %s", strerror(errno));
        }
        return -1;
    }
    
    // Verificar límite de conexiones
    pthread_mutex_lock(&main_server.clients_mutex);
    if (main_server.client_count >= server_config.max_connections) {
        pthread_mutex_unlock(&main_server.clients_mutex);
        LOG_WARNING("Máximo de conexiones alcanzado, rechazando cliente");
        
        // Enviar respuesta de servidor ocupado
        send_http_response(client_socket, 503, "text/plain", "Server busy", 11);
        close(client_socket);
        return -1;
    }
    pthread_mutex_unlock(&main_server.clients_mutex);
    
    // Agregar cliente y crear hilo
    int client_index = add_client(client_socket, &client_addr);
    if (client_index < 0) {
        LOG_ERROR("Error agregando cliente");
        close(client_socket);
        return -1;
    }
    
    return 0;
}

// Agregar cliente a la lista
int add_client(int socket_fd, struct sockaddr_in* client_addr) {
    pthread_mutex_lock(&main_server.clients_mutex);
    
    // Buscar slot libre
    int client_index = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!main_server.clients[i].active) {
            client_index = i;
            break;
        }
    }
    
    if (client_index == -1) {
        pthread_mutex_unlock(&main_server.clients_mutex);
        return -1;
    }
    
    // Configurar información del cliente
    main_server.clients[client_index].socket_fd = socket_fd;
    main_server.clients[client_index].address = *client_addr;
    main_server.clients[client_index].active = 1;
    
    // Convertir IP a string
    inet_ntop(AF_INET, &client_addr->sin_addr, 
              main_server.clients[client_index].ip_str, INET_ADDRSTRLEN);
    
    main_server.client_count++;
    
    LOG_INFO("Cliente conectado: %s (Total: %d)", 
             main_server.clients[client_index].ip_str, main_server.client_count);
    
    // Crear hilo para el cliente
    if (pthread_create(&main_server.clients[client_index].thread_id, NULL, 
                       client_handler_thread, &main_server.clients[client_index]) != 0) {
        LOG_ERROR("Error creando hilo para cliente: %s", strerror(errno));
        main_server.clients[client_index].active = 0;
        main_server.client_count--;
        pthread_mutex_unlock(&main_server.clients_mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&main_server.clients_mutex);
    return client_index;
}

// Hilo manejador de cliente
void* client_handler_thread(void* arg) {
    client_info_t* client = (client_info_t*)arg;
    char buffer[MAX_BUFFER_SIZE];
    int bytes_received;
    
    LOG_INFO("Iniciando manejo de cliente: %s", client->ip_str);
    
    // Recibir datos del cliente
    bytes_received = recv(client->socket_fd, buffer, MAX_BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        
        // Parsear petición HTTP
        char method[16], path[256], filename[MAX_FILENAME_SIZE];
        if (parse_http_request(buffer, method, path, filename) == 0) {
            LOG_INFO("Petición: %s %s desde %s", method, path, client->ip_str);
            
            if (strcmp(method, "POST") == 0) {
                // Manejar subida de archivo
                handle_file_upload(client->socket_fd, filename, buffer, bytes_received);
            } else {
                // Respuesta simple para otros métodos
                const char* response = "ImageServer v1.0 - Listo para recibir imágenes";
                send_http_response(client->socket_fd, 200, "text/plain", response, strlen(response));
            }
        } else {
            LOG_WARNING("Petición HTTP inválida desde %s", client->ip_str);
            send_http_response(client->socket_fd, 400, "text/plain", "Bad Request", 11);
        }
    } else if (bytes_received == 0) {
        LOG_INFO("Cliente desconectado: %s", client->ip_str);
    } else {
        LOG_ERROR("Error recibiendo datos de %s: %s", client->ip_str, strerror(errno));
    }
    
    // Cerrar conexión y marcar como inactivo
    close(client->socket_fd);
    client->active = 0;
    
    pthread_mutex_lock(&main_server.clients_mutex);
    main_server.client_count--;
    LOG_INFO("Cliente desconectado: %s (Total: %d)", client->ip_str, main_server.client_count);
    pthread_mutex_unlock(&main_server.clients_mutex);
    
    return NULL;
}

// Enviar respuesta HTTP
int send_http_response(int client_socket, int status_code, const char* content_type, 
                       const char* content, size_t content_length) {
    char response[MAX_BUFFER_SIZE];
    char* status_text;
    
    switch(status_code) {
        case 200: status_text = "OK"; break;
        case 400: status_text = "Bad Request"; break;
        case 500: status_text = "Internal Server Error"; break;
        case 503: status_text = "Service Unavailable"; break;
        default: status_text = "Unknown"; break;
    }
    
    int header_len = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, content_type, content_length);
    
    // Enviar header
    if (send(client_socket, response, header_len, 0) < 0) {
        return -1;
    }
    
    // Enviar contenido si existe
    if (content && content_length > 0) {
        if (send(client_socket, content, content_length, 0) < 0) {
            return -1;
        }
    }
    
    return 0;
}

// Parsear petición HTTP básica
int parse_http_request(const char* request, char* method, char* path, char* filename) {
    if (sscanf(request, "%15s %255s", method, path) != 2) {
        return -1;
    }
    
    // Extraer nombre de archivo del path
    char* file_part = strrchr(path, '/');
    if (file_part) {
        strncpy(filename, file_part + 1, MAX_FILENAME_SIZE - 1);
        filename[MAX_FILENAME_SIZE - 1] = '\0';
    } else {
        strcpy(filename, "unknown");
    }
    
    return 0;
}

// Manejar subida de archivo (básico por ahora)
int handle_file_upload(int client_socket, const char* filename, const char* request, size_t request_len) {
    (void)request; (void)request_len; // Evitar warnings
    
    LOG_INFO("Procesando archivo: %s", filename);
    
    // Por ahora solo responder que se recibió
    char response[256];
    snprintf(response, sizeof(response), "Archivo recibido: %s", filename);
    
    return send_http_response(client_socket, 200, "text/plain", response, strlen(response));
}

// Limpiar clientes inactivos
void cleanup_inactive_clients(void) {
    // Esta función se ejecuta periódicamente para limpiar hilos terminados
    // Por ahora es básica, se puede mejorar después
}

// Detener servidor
int stop_server(void) {
    if (main_server.status != SERVER_RUNNING) {
        return 1;
    }
    
    LOG_INFO("Deteniendo servidor TCP...");
    main_server.status = SERVER_STOPPING;
    
    // Cerrar socket principal
    if (main_server.server_socket != -1) {
        close(main_server.server_socket);
    }
    
    // Esperar a que termine el hilo del servidor
    pthread_join(main_server.server_thread, NULL);
    
    LOG_INFO("Servidor TCP detenido");
    return 1;
}

// Limpiar recursos del servidor
void cleanup_server(void) {
    LOG_INFO("Limpiando recursos del servidor...");
    
    stop_server();
    
    // Cerrar todas las conexiones de clientes
    pthread_mutex_lock(&main_server.clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (main_server.clients[i].active) {
            close(main_server.clients[i].socket_fd);
            main_server.clients[i].active = 0;
        }
    }
    pthread_mutex_unlock(&main_server.clients_mutex);
    
    // Destruir mutex
    pthread_mutex_destroy(&main_server.clients_mutex);
    
    main_server.status = SERVER_STOPPED;
    LOG_INFO("Limpieza del servidor completada");
}
