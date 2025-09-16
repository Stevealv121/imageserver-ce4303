#include "image_processor.h"
#include "logger.h"
#include "config.h"
#include "server.h"
#include "file_handler.h"
#include "priority_queue.h"

// Variable global del servidor
tcp_server_t main_server;
// Estadísticas globales de archivos
static file_stats_t global_stats = {0};
// Buffer para recibir datos grandes (para archivos)
// static char large_buffer[MAX_UPLOAD_SIZE];

// Función auxiliar para strcasestr en sistemas que no la tienen
#ifndef strcasestr
char *strcasestr(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);
    size_t haystack_len = strlen(haystack);

    for (size_t i = 0; i <= haystack_len - needle_len; i++)
    {
        if (strncasecmp(&haystack[i], needle, needle_len) == 0)
        {
            return (char *)&haystack[i];
        }
    }
    return NULL;
}
#endif

// Inicializar estadísticas de archivos
void init_file_stats(void)
{
    memset(&global_stats, 0, sizeof(global_stats));
}

// Obtener puntero a las estadísticas
file_stats_t *get_file_stats(void)
{
    return &global_stats;
}

// Loggear estadísticas
void log_file_stats(void)
{
    LOG_INFO("Estadísticas - Uploads: %d (éxitos: %d, fallos: %d), Bytes: %zu",
             global_stats.total_uploads, global_stats.successful_uploads,
             global_stats.failed_uploads, global_stats.total_bytes_processed);
}

// Inicializar el servidor
int init_server(void)
{
    LOG_INFO("Inicializando servidor TCP...");

    // Inicializar estructura del servidor
    memset(&main_server, 0, sizeof(tcp_server_t));
    main_server.status = SERVER_STOPPED;
    main_server.server_socket = -1;
    main_server.client_count = 0;

    // Inicializar mutex para clientes
    if (pthread_mutex_init(&main_server.clients_mutex, NULL) != 0)
    {
        LOG_ERROR("Error inicializando mutex de clientes: %s", strerror(errno));
        return 0;
    }

    // Inicializar cola de prioridad
    if (!init_priority_queue())
    {
        LOG_ERROR("Error inicializando cola de prioridad");
        pthread_mutex_destroy(&main_server.clients_mutex);
        return 0;
    }

    // Inicializar estadísticas de archivos
    init_file_stats();

    // Crear socket del servidor
    main_server.server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (main_server.server_socket == -1)
    {
        LOG_ERROR("Error creando socket: %s", strerror(errno));
        destroy_priority_queue();
        pthread_mutex_destroy(&main_server.clients_mutex);
        return 0;
    }

    // Configurar opción SO_REUSEADDR
    int opt = 1;
    if (setsockopt(main_server.server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        LOG_WARNING("Error configurando SO_REUSEADDR: %s", strerror(errno));
    }

    // Configurar timeout para recv (30 segundos)
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    if (setsockopt(main_server.server_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        LOG_WARNING("Error configurando timeout de recepción: %s", strerror(errno));
    }

    // Configurar direccion del servidor
    memset(&main_server.server_addr, 0, sizeof(main_server.server_addr));
    main_server.server_addr.sin_family = AF_INET;
    main_server.server_addr.sin_addr.s_addr = INADDR_ANY;
    main_server.server_addr.sin_port = htons(server_config.port);

    // Bind del socket
    if (bind(main_server.server_socket, (struct sockaddr *)&main_server.server_addr,
             sizeof(main_server.server_addr)) < 0)
    {
        LOG_ERROR("Error en bind puerto %d: %s", server_config.port, strerror(errno));
        close(main_server.server_socket);
        destroy_priority_queue();
        pthread_mutex_destroy(&main_server.clients_mutex);
        return 0;
    }

    // Inicializar cola de prioridad
    if (!init_priority_queue())
    {
        LOG_ERROR("Error inicializando cola de prioridad");
        close(main_server.server_socket);
        return 0;
    }

    // Iniciar procesador de archivos
    if (!start_file_processor())
    {
        LOG_ERROR("Error iniciando procesador de archivos");
        destroy_priority_queue();
        close(main_server.server_socket);
        return 0;
    }

    LOG_INFO("Servidor inicializado correctamente en puerto %d", server_config.port);
    return 1;
}

// Iniciar el servidor
int start_server(void)
{
    if (main_server.status == SERVER_RUNNING)
    {
        LOG_WARNING("El servidor ya está ejecutándose");
        return 1;
    }

    main_server.status = SERVER_STARTING;
    LOG_INFO("Iniciando servidor TCP...");

    // Poner socket en modo escucha
    if (listen(main_server.server_socket, server_config.max_connections) < 0)
    {
        LOG_ERROR("Error en listen: %s", strerror(errno));
        main_server.status = SERVER_STOPPED;
        return 0;
    }

    // Iniciar procesador de archivos
    if (!start_file_processor())
    {
        LOG_ERROR("Error iniciando procesador de archivos");
        main_server.status = SERVER_STOPPED;
        return 0;
    }

    // Crear hilo del servidor
    if (pthread_create(&main_server.server_thread, NULL, server_thread_func, NULL) != 0)
    {
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
void *server_thread_func(void *arg)
{
    (void)arg; // Evitar warning de parámetro no usado

    LOG_INFO("Hilo del servidor iniciado");

    while (main_server.status == SERVER_RUNNING)
    {
        // Aceptar nueva conexión
        if (accept_client_connection() < 0)
        {
            if (main_server.status == SERVER_RUNNING)
            {
                LOG_ERROR("Error aceptando conexión");
                usleep(100000); // Esperar 100ms antes de intentar de nuevo
            }
        }

        // Limpiar clientes inactivos periódicamente
        cleanup_inactive_clients();

        // Limpiar archivos temporales antiguos cada cierto tiempo
        static time_t last_cleanup = 0;
        time_t now = time(NULL);
        if (now - last_cleanup > 3600)
        {                                             // Cada hora
            int cleaned = cleanup_old_temp_files(24); // Limpiar archivos > 24h
            if (cleaned > 0)
            {
                LOG_INFO("Limpiados %d archivos temporales antiguos", cleaned);
            }
            last_cleanup = now;
        }
    }

    LOG_INFO("Hilo del servidor terminando...");
    return NULL;
}

// Aceptar conexión de cliente
int accept_client_connection(void)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_socket;

    // Aceptar conexión
    client_socket = accept(main_server.server_socket, (struct sockaddr *)&client_addr, &client_len);
    if (client_socket < 0)
    {
        if (errno != EINTR && main_server.status == SERVER_RUNNING)
        {
            LOG_ERROR("Error en accept: %s", strerror(errno));
        }
        return -1;
    }

    // Verificar límite de conexiones
    pthread_mutex_lock(&main_server.clients_mutex);
    if (main_server.client_count >= server_config.max_connections)
    {
        pthread_mutex_unlock(&main_server.clients_mutex);
        LOG_WARNING("Máximo de conexiones alcanzado, rechazando cliente");

        // Enviar respuesta de servidor ocupado
        send_http_response(client_socket, 503, "application/json",
                           "{\"error\":\"Server busy\",\"code\":503}", 31);
        close(client_socket);
        return -1;
    }
    pthread_mutex_unlock(&main_server.clients_mutex);

    // Agregar cliente y crear hilo
    int client_index = add_client(client_socket, &client_addr);
    if (client_index < 0)
    {
        LOG_ERROR("Error agregando cliente");
        close(client_socket);
        return -1;
    }

    return 0;
}

// Agregar cliente a la lista
int add_client(int socket_fd, struct sockaddr_in *client_addr)
{
    pthread_mutex_lock(&main_server.clients_mutex);

    // Buscar slot libre
    int client_index = -1;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!main_server.clients[i].active)
        {
            client_index = i;
            break;
        }
    }

    if (client_index == -1)
    {
        pthread_mutex_unlock(&main_server.clients_mutex);
        return -1;
    }

    // Configurar información del cliente
    main_server.clients[client_index].socket_fd = socket_fd;
    main_server.clients[client_index].address = *client_addr;
    main_server.clients[client_index].active = 1;
    main_server.clients[client_index].connection_time = time(NULL);

    // Convertir IP a string
    inet_ntop(AF_INET, &client_addr->sin_addr,
              main_server.clients[client_index].ip_str, INET_ADDRSTRLEN);

    main_server.client_count++;

    LOG_INFO("Cliente conectado: %s (Total: %d)",
             main_server.clients[client_index].ip_str, main_server.client_count);

    // Crear hilo para el cliente
    if (pthread_create(&main_server.clients[client_index].thread_id, NULL,
                       client_handler_thread, &main_server.clients[client_index]) != 0)
    {
        LOG_ERROR("Error creando hilo para cliente: %s", strerror(errno));
        main_server.clients[client_index].active = 0;
        main_server.client_count--;
        pthread_mutex_unlock(&main_server.clients_mutex);
        return -1;
    }

    pthread_mutex_unlock(&main_server.clients_mutex);
    return client_index;
}

// Recibir petición HTTP completa con soporte para archivos grandes
int receive_complete_request(int client_socket, char *buffer, size_t buffer_size, size_t *total_received)
{
    *total_received = 0;
    size_t content_length = 0;
    size_t headers_end_pos = 0;
    int headers_complete = 0;

    // Configurar timeout para el socket
    struct timeval timeout;
    timeout.tv_sec = 30; // 30 segundos timeout
    timeout.tv_usec = 0;

    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        LOG_ERROR("Error configurando timeout del socket: %s", strerror(errno));
    }

    while (*total_received < buffer_size - 1)
    {
        ssize_t bytes_received = recv(client_socket, buffer + *total_received,
                                      buffer_size - *total_received - 1, 0);

        if (bytes_received <= 0)
        {
            if (bytes_received == 0)
            {
                LOG_DEBUG("Cliente cerró la conexión");
                break;
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                LOG_WARNING("Timeout recibiendo datos del cliente");
                break;
            }
            else
            {
                LOG_ERROR("Error recibiendo datos: %s", strerror(errno));
                return -1;
            }
        }

        *total_received += bytes_received;
        buffer[*total_received] = '\0';

        // Verificar si hemos recibido los headers completos
        if (!headers_complete)
        {
            char *headers_end = strstr(buffer, "\r\n\r\n");
            if (headers_end)
            {
                headers_end_pos = headers_end - buffer + 4;
                headers_complete = 1;

                // Buscar Content-Length en los headers
                char *content_length_header = strcasestr(buffer, "content-length:");
                if (content_length_header)
                {
                    content_length = strtoul(content_length_header + 15, NULL, 10);
                    LOG_DEBUG("Content-Length detectado: %zu", content_length);

                    // Verificar límite de tamaño
                    if (content_length > MAX_UPLOAD_SIZE)
                    {
                        LOG_ERROR("Content-Length demasiado grande: %zu bytes (máximo: %d)",
                                  content_length, MAX_UPLOAD_SIZE);
                        return -1;
                    }
                }
            }
        }

        // Si hemos recibido los headers y conocemos el Content-Length
        if (headers_complete && content_length > 0)
        {
            size_t expected_total = headers_end_pos + content_length;

            if (*total_received >= expected_total)
            {
                LOG_DEBUG("Petición completa recibida: %zu bytes (headers: %zu, body: %zu)",
                          *total_received, headers_end_pos, content_length);
                break;
            }

            // Verificar si el buffer es suficiente
            if (expected_total >= buffer_size)
            {
                LOG_ERROR("Buffer insuficiente para la petición completa");
                return -1;
            }
        }

        // Si no hay Content-Length y hemos recibido headers, asumir que es el final
        if (headers_complete && content_length == 0)
        {
            LOG_DEBUG("Petición sin body recibida: %zu bytes", *total_received);
            break;
        }

        // Protección contra buffer overflow
        if (*total_received >= buffer_size - 1)
        {
            LOG_WARNING("Buffer lleno, terminando recepción");
            break;
        }
    }

    return (*total_received > 0) ? 0 : -1;
}

void mark_client_inactive(int client_socket)
{
    pthread_mutex_lock(&main_server.clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (main_server.clients[i].active &&
            main_server.clients[i].socket_fd == client_socket)
        {

            main_server.clients[i].active = 0;
            main_server.client_count--;

            LOG_INFO("Cliente marcado como inactivo: %s (Total: %d)",
                     main_server.clients[i].ip_str, main_server.client_count);
            break;
        }
    }

    pthread_mutex_unlock(&main_server.clients_mutex);
}

// Hilo manejador de cliente
void *client_handler_thread(void *arg)
{
    client_info_t *client = (client_info_t *)arg;

    if (!client || client->socket_fd <= 0)
    {
        LOG_ERROR("Cliente inválido pasado al handler");
        return NULL;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client->address.sin_addr), client_ip, INET_ADDRSTRLEN);

    LOG_INFO("Iniciando manejo de cliente: %s", client_ip);

    // Buffer para la petición
    char *request_buffer = malloc(MAX_UPLOAD_SIZE + MAX_BUFFER_SIZE);
    if (!request_buffer)
    {
        LOG_ERROR("Error allocando buffer para cliente %s", client_ip);
        close(client->socket_fd);
        mark_client_inactive(client->socket_fd);
        return NULL;
    }

    size_t total_received = 0;
    int result = receive_complete_request(client->socket_fd, request_buffer,
                                          MAX_UPLOAD_SIZE + MAX_BUFFER_SIZE, &total_received);

    if (result < 0 || total_received == 0)
    {
        LOG_ERROR("Error recibiendo petición de %s", client_ip);
        send_error_response(client->socket_fd, 400, "Bad Request");
        goto cleanup;
    }

    LOG_DEBUG("Petición recibida de %s: %zu bytes", client_ip, total_received);

    // Parsear método y ruta
    char method[16];
    char path[512];

    if (parse_http_request(request_buffer, method, path) != 0)
    {
        LOG_ERROR("Error parseando petición HTTP de %s", client_ip);
        send_error_response(client->socket_fd, 400, "Malformed Request");
        goto cleanup;
    }

    LOG_INFO("Petición: %s %s desde %s (%zu bytes)", method, path, client_ip, total_received);

    // Procesar según el método
    if (strcasecmp(method, "GET") == 0)
    {
        if (handle_get_request(client->socket_fd, path, client_ip) != 0)
        {
            LOG_ERROR("Error procesando GET de %s", client_ip);
        }
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        // Verificar que es un upload de archivo
        if (strstr(request_buffer, "multipart/form-data") != NULL)
        {
            LOG_INFO("Detectado upload de archivo desde %s", client_ip);

            if (handle_post_request(client->socket_fd, request_buffer, total_received, client_ip) != 0)
            {
                LOG_ERROR("Error procesando POST de %s", client_ip);
            }
        }
        else
        {
            LOG_WARNING("POST sin multipart/form-data desde %s", client_ip);
            send_error_response(client->socket_fd, 400, "Expected multipart/form-data");
        }
    }
    else
    {
        LOG_WARNING("Método HTTP no soportado: %s desde %s", method, client_ip);
        send_error_response(client->socket_fd, 405, "Method Not Allowed");
    }

cleanup:
    free(request_buffer);

    // Cerrar socket de forma limpia
    if (shutdown(client->socket_fd, SHUT_RDWR) < 0)
    {
        LOG_DEBUG("Error en shutdown del socket: %s", strerror(errno));
    }

    close(client->socket_fd);
    mark_client_inactive(client->socket_fd);

    LOG_INFO("Cliente desconectado: %s", client_ip);

    return NULL;
}

// Manejar petición GET
int handle_get_request(int client_socket, const char *path, const char *client_ip)
{
    if (strcmp(path, "/") == 0 || strcmp(path, "/status") == 0)
    {
        // Página de status del servidor
        char response_body[1024];
        const file_stats_t *stats = get_file_stats();

        snprintf(response_body, sizeof(response_body),
                 "{\n"
                 "  \"service\": \"ImageServer\",\n"
                 "  \"version\": \"1.0\",\n"
                 "  \"status\": \"running\",\n"
                 "  \"port\": %d,\n"
                 "  \"active_connections\": %d,\n"
                 "  \"max_connections\": %d,\n"
                 "  \"processing_queue\": {\n"
                 "    \"size\": %d,\n"
                 "    \"max_size\": %d,\n"
                 "    \"processor_status\": \"%s\"\n"
                 "  },\n"
                 "  \"stats\": {\n"
                 "    \"total_uploads\": %d,\n"
                 "    \"successful_uploads\": %d,\n"
                 "    \"failed_uploads\": %d,\n"
                 "    \"total_bytes_processed\": %zu\n"
                 "  },\n"
                 "  \"supported_formats\": \"%s\",\n"
                 "  \"max_file_size_mb\": %d\n"
                 "}",
                 server_config.port, main_server.client_count, server_config.max_connections,
                 get_queue_size(), MAX_QUEUE_SIZE, processor_running ? "running" : "stopped",
                 stats->total_uploads, stats->successful_uploads, stats->failed_uploads,
                 stats->total_bytes_processed, server_config.supported_formats,
                 server_config.max_image_size_mb);

        send_success_response(client_socket, "application/json", response_body);
        log_client_activity(client_ip, path, "GET", "success");
        return 0;
    }
    else if (strcmp(path, "/upload") == 0)
    {
        // Página de información de upload
        const char *upload_info =
            "{\n"
            "  \"message\": \"POST multipart/form-data to this endpoint\",\n"
            "  \"supported_formats\": [\"jpg\", \"jpeg\", \"png\", \"gif\"],\n"
            "  \"max_size_mb\": " STR(MAX_IMAGE_SIZE_MB) ",\n"
                                                         "  \"field_name\": \"image\",\n"
                                                         "  \"processing_note\": \"Files are processed by size - smaller files first\"\n"
                                                         "}";

        send_success_response(client_socket, "application/json", upload_info);
        log_client_activity(client_ip, path, "GET", "success");
        return 0;
    }
    else if (strcmp(path, "/queue") == 0)
    {
        // NUEVA RUTA: Información específica de la cola
        char queue_info[512];
        snprintf(queue_info, sizeof(queue_info),
                 "{\n"
                 "  \"queue_size\": %d,\n"
                 "  \"max_queue_size\": %d,\n"
                 "  \"processor_running\": %s,\n"
                 "  \"queue_full\": %s,\n"
                 "  \"processing_policy\": \"Smaller files processed first\"\n"
                 "}",
                 get_queue_size(), MAX_QUEUE_SIZE,
                 processor_running ? "true" : "false",
                 is_queue_full() ? "true" : "false");

        send_success_response(client_socket, "application/json", queue_info);
        log_client_activity(client_ip, path, "GET", "success");
        return 0;
    }
    else
    {
        // Recurso no encontrado
        send_error_response(client_socket, 404, "Not Found");
        log_client_activity(client_ip, path, "GET", "not_found");
        return -1;
    }
}

// Manejar petición POST no-multipart
int handle_post_request(int client_socket, const char *request_data, size_t request_len, const char *client_ip)
{
    (void)request_data;
    (void)request_len; // Evitar warnings

    const char *response =
        "{\n"
        "  \"error\": \"POST request must be multipart/form-data for file uploads\",\n"
        "  \"usage\": \"Send files using multipart/form-data with field name 'image'\"\n"
        "}";

    send_http_response(client_socket, 400, "application/json", response, strlen(response));
    log_client_activity(client_ip, "POST", "non-multipart", "bad_request");

    return -1;
}

// Enviar respuesta HTTP (función original mantenida para compatibilidad)
int send_http_response(int client_socket, int status_code, const char *content_type,
                       const char *content, size_t content_length)
{
    char response[MAX_BUFFER_SIZE];
    char *status_text;

    switch (status_code)
    {
    case 200:
        status_text = "OK";
        break;
    case 400:
        status_text = "Bad Request";
        break;
    case 404:
        status_text = "Not Found";
        break;
    case 405:
        status_text = "Method Not Allowed";
        break;
    case 500:
        status_text = "Internal Server Error";
        break;
    case 503:
        status_text = "Service Unavailable";
        break;
    default:
        status_text = "Unknown";
        break;
    }

    int header_len = snprintf(response, sizeof(response),
                              "HTTP/1.1 %d %s\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n"
                              "Server: ImageServer/1.0\r\n"
                              "\r\n",
                              status_code, status_text, content_type, content_length);

    // Enviar header
    if (send(client_socket, response, header_len, 0) < 0)
    {
        return -1;
    }

    // Enviar contenido si existe
    if (content && content_length > 0)
    {
        if (send(client_socket, content, content_length, 0) < 0)
        {
            return -1;
        }
    }

    return 0;
}

// Parsear petición HTTP básica (mejorada)
int parse_http_request(const char *request, char *method, char *path)
{
    if (sscanf(request, "%15s %255s", method, path) != 2)
    {
        return -1;
    }

    // Validar método HTTP
    if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0 &&
        strcmp(method, "HEAD") != 0 && strcmp(method, "OPTIONS") != 0)
    {
        return -1;
    }

    return 0;
}

// Limpiar clientes inactivos
void cleanup_inactive_clients(void)
{
    pthread_mutex_lock(&main_server.clients_mutex);

    time_t current_time = time(NULL);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (main_server.clients[i].active)
        {
            // Verificar si el cliente lleva mucho tiempo conectado sin actividad
            if (difftime(current_time, main_server.clients[i].connection_time) > 300)
            { // 5 minutos
                LOG_WARNING("Cliente inactivo detectado: %s (conectado hace %.0f segundos)",
                            main_server.clients[i].ip_str,
                            difftime(current_time, main_server.clients[i].connection_time));

                // Cerrar socket si está aún abierto
                if (main_server.clients[i].socket_fd > 0)
                {
                    close(main_server.clients[i].socket_fd);
                }

                // Marcar como inactivo
                main_server.clients[i].active = 0;
                main_server.client_count--;

                // Hacer join del thread si es posible (no bloqueable)
                pthread_detach(main_server.clients[i].thread_id);
            }
        }
    }

    pthread_mutex_unlock(&main_server.clients_mutex);
}

// Detener servidor
int stop_server(void)
{
    if (main_server.status != SERVER_RUNNING)
    {
        return 1;
    }

    LOG_INFO("Deteniendo servidor TCP...");
    main_server.status = SERVER_STOPPING;

    // Cerrar socket principal
    if (main_server.server_socket != -1)
    {
        close(main_server.server_socket);
    }

    // Esperar a que termine el hilo del servidor
    pthread_join(main_server.server_thread, NULL);

    // Mostrar estadísticas finales
    log_file_stats();

    LOG_INFO("Servidor TCP detenido");
    return 1;
}

// Limpiar recursos del servidor
void cleanup_server(void)
{
    LOG_INFO("Limpiando recursos del servidor...");

    stop_server();

    // Detener procesador de archivos y destruir cola
    stop_file_processor();
    destroy_priority_queue();

    // Cerrar todas las conexiones de clientes
    pthread_mutex_lock(&main_server.clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (main_server.clients[i].active)
        {
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
