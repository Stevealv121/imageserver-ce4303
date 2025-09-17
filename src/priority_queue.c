#include "priority_queue.h"
#include "logger.h"
#include "image_processor.h"
#include "server.h"

// Variables globales
extern file_stats_t *get_file_stats(void);
priority_queue_t processing_queue;
pthread_t processor_thread;
int processor_running = 0;

// Inicializar la cola de prioridad
int init_priority_queue(void)
{
    LOG_INFO("Inicializando cola de prioridad para procesamiento de archivos...");

    processing_queue.size = 0;
    processing_queue.active = 1;

    // Inicializar mutex y condition variables
    if (pthread_mutex_init(&processing_queue.queue_mutex, NULL) != 0)
    {
        LOG_ERROR("Error inicializando mutex de cola: %s", strerror(errno));
        return 0;
    }

    if (pthread_cond_init(&processing_queue.queue_not_empty, NULL) != 0)
    {
        LOG_ERROR("Error inicializando condition variable queue_not_empty: %s", strerror(errno));
        pthread_mutex_destroy(&processing_queue.queue_mutex);
        return 0;
    }

    if (pthread_cond_init(&processing_queue.queue_not_full, NULL) != 0)
    {
        LOG_ERROR("Error inicializando condition variable queue_not_full: %s", strerror(errno));
        pthread_cond_destroy(&processing_queue.queue_not_empty);
        pthread_mutex_destroy(&processing_queue.queue_mutex);
        return 0;
    }

    LOG_INFO("Cola de prioridad inicializada correctamente (capacidad: %d)", MAX_QUEUE_SIZE);
    return 1;
}

// Destruir la cola de prioridad
void destroy_priority_queue(void)
{
    LOG_INFO("Destruyendo cola de prioridad...");

    pthread_mutex_lock(&processing_queue.queue_mutex);
    processing_queue.active = 0;

    // Despertar a todos los hilos esperando
    pthread_cond_broadcast(&processing_queue.queue_not_empty);
    pthread_cond_broadcast(&processing_queue.queue_not_full);
    pthread_mutex_unlock(&processing_queue.queue_mutex);

    // Destruir synchronization objects
    pthread_cond_destroy(&processing_queue.queue_not_empty);
    pthread_cond_destroy(&processing_queue.queue_not_full);
    pthread_mutex_destroy(&processing_queue.queue_mutex);

    LOG_INFO("Cola de prioridad destruida");
}

// Comparar prioridad (menor tamaño = mayor prioridad)
static int compare_priority(const priority_queue_item_t *a, const priority_queue_item_t *b)
{
    if (a->file_size < b->file_size)
        return -1;
    if (a->file_size > b->file_size)
        return 1;

    // Si tienen el mismo tamaño, prioridad por orden de llegada
    if (a->received_time < b->received_time)
        return -1;
    if (a->received_time > b->received_time)
        return 1;

    return 0;
}

// Intercambiar elementos en la cola
static void swap_items(int i, int j)
{
    priority_queue_item_t temp = processing_queue.items[i];
    processing_queue.items[i] = processing_queue.items[j];
    processing_queue.items[j] = temp;
}

// Heapify hacia arriba (para inserción)
static void heapify_up(int index)
{
    if (index == 0)
        return;

    int parent = (index - 1) / 2;

    if (compare_priority(&processing_queue.items[index], &processing_queue.items[parent]) < 0)
    {
        swap_items(index, parent);
        heapify_up(parent);
    }
}

// Heapify hacia abajo (para extracción)
static void heapify_down(int index)
{
    int left = 2 * index + 1;
    int right = 2 * index + 2;
    int smallest = index;

    if (left < processing_queue.size &&
        compare_priority(&processing_queue.items[left], &processing_queue.items[smallest]) < 0)
    {
        smallest = left;
    }

    if (right < processing_queue.size &&
        compare_priority(&processing_queue.items[right], &processing_queue.items[smallest]) < 0)
    {
        smallest = right;
    }

    if (smallest != index)
    {
        swap_items(index, smallest);
        heapify_down(smallest);
    }
}

// Agregar archivo a la cola de procesamiento
int enqueue_file_for_processing(const file_upload_info_t *upload_info,
                                const char *temp_filepath,
                                const char *client_ip,
                                int client_socket)
{

    if (!upload_info || !temp_filepath || !client_ip)
    {
        LOG_ERROR("Parámetros inválidos para encolar archivo");
        return -1;
    }

    pthread_mutex_lock(&processing_queue.queue_mutex);

    // Verificar si la cola está llena
    if (processing_queue.size >= MAX_QUEUE_SIZE)
    {
        LOG_ERROR("Cola de procesamiento llena (%d/%d)", processing_queue.size, MAX_QUEUE_SIZE);
        pthread_mutex_unlock(&processing_queue.queue_mutex);
        return -1;
    }

    // Verificar que el archivo temporal existe y es válido
    struct stat file_stat;
    if (stat(temp_filepath, &file_stat) != 0)
    {
        LOG_ERROR("No se puede acceder al archivo temporal: %s", temp_filepath);
        pthread_mutex_unlock(&processing_queue.queue_mutex);
        return -1;
    }

    if (!S_ISREG(file_stat.st_mode))
    {
        LOG_ERROR("El path temporal no es un archivo regular: %s", temp_filepath);
        pthread_mutex_unlock(&processing_queue.queue_mutex);
        return -1;
    }

    // Crear elemento para la cola
    priority_queue_item_t new_item;
    memset(&new_item, 0, sizeof(new_item));

    // Copiar información
    new_item.upload_info = *upload_info;
    new_item.file_size = upload_info->file_size;
    new_item.received_time = time(NULL);
    new_item.client_socket = client_socket;
    new_item.priority = (int)upload_info->file_size; // Menor tamaño = mayor prioridad

    strncpy(new_item.temp_filepath, temp_filepath, sizeof(new_item.temp_filepath) - 1);
    strncpy(new_item.client_ip, client_ip, sizeof(new_item.client_ip) - 1);

    // Insertar en la cola manteniendo orden de prioridad (min-heap)
    int pos = processing_queue.size;
    processing_queue.items[pos] = new_item;
    processing_queue.size++;

    // Bubble up para mantener propiedad de min-heap
    while (pos > 0)
    {
        int parent = (pos - 1) / 2;
        if (processing_queue.items[parent].priority <= processing_queue.items[pos].priority)
        {
            break;
        }

        // Intercambiar con padre
        priority_queue_item_t temp = processing_queue.items[parent];
        processing_queue.items[parent] = processing_queue.items[pos];
        processing_queue.items[pos] = temp;

        pos = parent;
    }

    // Logging detallado
    LOG_INFO("   ARCHIVO ENCOLADO:");
    LOG_INFO("   Archivo: %s (%zu bytes)", upload_info->original_filename, upload_info->file_size);
    LOG_INFO("   Cliente: %s", client_ip);
    LOG_INFO("   Posición en cola: %d/%d", processing_queue.size, MAX_QUEUE_SIZE);

    // Mostrar próximos archivos a procesar
    LOG_INFO("   Orden de procesamiento (próximos 3):");
    int show_count = (processing_queue.size < 3) ? processing_queue.size : 3;
    for (int i = 0; i < show_count; i++)
    {
        LOG_INFO("     %d. %s (%zu bytes)",
                 i + 1,
                 processing_queue.items[i].upload_info.original_filename,
                 processing_queue.items[i].file_size);
    }

    // Notificar al hilo procesador
    pthread_cond_signal(&processing_queue.queue_not_empty);
    pthread_mutex_unlock(&processing_queue.queue_mutex);

    return 0;
}

// Extraer archivo de la cola para procesamiento
int dequeue_file_for_processing(priority_queue_item_t *item)
{
    if (!item)
    {
        return -1;
    }

    pthread_mutex_lock(&processing_queue.queue_mutex);

    // Esperar hasta que haya elementos o se termine el procesador
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 1; // 1 segundo de timeout

    while (processing_queue.size == 0 && processing_queue.active)
    {
        int wait_result = pthread_cond_timedwait(&processing_queue.queue_not_empty,
                                                 &processing_queue.queue_mutex,
                                                 &timeout);
        if (wait_result == ETIMEDOUT)
        {
            pthread_mutex_unlock(&processing_queue.queue_mutex);
            return -1; // Timeout
        }
    }

    // Verificar si debemos terminar
    if (!processing_queue.active)
    {
        pthread_mutex_unlock(&processing_queue.queue_mutex);
        return -1;
    }

    // Verificar que hay elementos
    if (processing_queue.size == 0)
    {
        pthread_mutex_unlock(&processing_queue.queue_mutex);
        return -1;
    }

    // Extraer elemento con mayor prioridad (índice 0 del min-heap)
    *item = processing_queue.items[0];
    processing_queue.size--;

    // Reorganizar heap si quedan elementos
    if (processing_queue.size > 0)
    {
        // Mover último elemento al inicio
        processing_queue.items[0] = processing_queue.items[processing_queue.size];

        // Bubble down para mantener propiedad de min-heap
        int pos = 0;
        while (1)
        {
            int left_child = 2 * pos + 1;
            int right_child = 2 * pos + 2;
            int smallest = pos;

            // Encontrar el elemento más pequeño entre padre e hijos
            if (left_child < processing_queue.size &&
                processing_queue.items[left_child].priority < processing_queue.items[smallest].priority)
            {
                smallest = left_child;
            }

            if (right_child < processing_queue.size &&
                processing_queue.items[right_child].priority < processing_queue.items[smallest].priority)
            {
                smallest = right_child;
            }

            // Si el padre ya es el más pequeño, terminamos
            if (smallest == pos)
            {
                break;
            }

            // Intercambiar con el hijo más pequeño
            priority_queue_item_t temp = processing_queue.items[pos];
            processing_queue.items[pos] = processing_queue.items[smallest];
            processing_queue.items[smallest] = temp;

            pos = smallest;
        }
    }

    LOG_DEBUG("Archivo extraído de cola: %s (%zu bytes) - Elementos restantes: %d",
              item->upload_info.original_filename, item->file_size, processing_queue.size);

    pthread_mutex_unlock(&processing_queue.queue_mutex);
    return 0;
}

// Funciones auxiliares
int is_queue_empty(void)
{
    pthread_mutex_lock(&processing_queue.queue_mutex);
    int empty = (processing_queue.size == 0);
    pthread_mutex_unlock(&processing_queue.queue_mutex);
    return empty;
}

int is_queue_full(void)
{
    pthread_mutex_lock(&processing_queue.queue_mutex);
    int full = (processing_queue.size >= MAX_QUEUE_SIZE);
    pthread_mutex_unlock(&processing_queue.queue_mutex);
    return full;
}

int get_queue_size(void)
{
    pthread_mutex_lock(&processing_queue.queue_mutex);
    int size = processing_queue.size;
    pthread_mutex_unlock(&processing_queue.queue_mutex);
    return size;
}

void print_queue_status(void)
{
    pthread_mutex_lock(&processing_queue.queue_mutex);

    LOG_INFO("Estado de cola de procesamiento:");
    LOG_INFO("  Tamaño actual: %d/%d", processing_queue.size, MAX_QUEUE_SIZE);
    LOG_INFO("  Estado: %s", processing_queue.active ? "ACTIVA" : "INACTIVA");

    if (processing_queue.size > 0)
    {
        LOG_INFO("  Próximo archivo: %s (%zu bytes)",
                 processing_queue.items[0].upload_info.original_filename,
                 processing_queue.items[0].file_size);
    }

    pthread_mutex_unlock(&processing_queue.queue_mutex);
}

// Función para enviar respuestas de éxito con información de procesamiento
static int send_processing_success_response(int client_socket, const processed_image_info_t *result)
{
    char response_json[1024];
    const char *color_name = "unknown";

    // Convertir enum de color a string
    switch (result->predominant_color)
    {
    case COLOR_RED:
        color_name = "red";
        break;
    case COLOR_GREEN:
        color_name = "green";
        break;
    case COLOR_BLUE:
        color_name = "blue";
        break;
    case COLOR_UNDEFINED:
    default:
        color_name = "unknown";
        break;
    }

    // Calcular tiempo de procesamiento
    time_t current_time = time(NULL);
    int processing_time = (int)difftime(current_time, result->processing_time);

    // Calcular tamaño aproximado de la imagen
    long image_size = (long)result->width * result->height * result->channels;

    snprintf(response_json, sizeof(response_json),
             "{\n"
             "  \"status\": \"success\",\n"
             "  \"message\": \"File processed successfully\",\n"
             "  \"filename\": \"%s\",\n"
             "  \"size\": %ld,\n"
             "  \"processed_path\": \"%s\",\n"
             "  \"predominant_color\": \"%s\",\n"
             "  \"processing_time\": %d\n"
             "}",
             result->original_filename,
             image_size,
             result->equalized_path,
             color_name,
             processing_time);

    return send_http_response(client_socket, 200, "application/json",
                              response_json, strlen(response_json));
}

void get_queue_statistics(int *total_files, int *total_bytes, int *avg_file_size)
{
    if (!total_files || !total_bytes || !avg_file_size)
    {
        return;
    }

    pthread_mutex_lock(&processing_queue.queue_mutex);

    *total_files = processing_queue.size;
    *total_bytes = 0;

    for (int i = 0; i < processing_queue.size; i++)
    {
        *total_bytes += (int)processing_queue.items[i].file_size;
    }

    *avg_file_size = (*total_files > 0) ? (*total_bytes / *total_files) : 0;

    pthread_mutex_unlock(&processing_queue.queue_mutex);
}

// Hilo procesador de archivos
void *file_processor_thread(void *arg)
{

    (void)arg; // Suprimir warning
    LOG_INFO("Hilo procesador de archivos iniciado");

    while (processor_running)
    {
        priority_queue_item_t item;

        // Extraer elemento de la cola con timeout
        int dequeue_result = dequeue_file_for_processing(&item);

        if (dequeue_result != 0)
        {
            if (processor_running)
            {
                // Si hay error y el procesador debe seguir corriendo, esperar un poco
                usleep(100000); // 100ms
            }
            continue;
        }

        LOG_INFO("=== PROCESANDO ARCHIVO ===");
        LOG_INFO("Archivo: %s (%zu bytes) desde %s",
                 item.upload_info.original_filename,
                 item.file_size,
                 item.client_ip);
        LOG_INFO("Cola restante: %d archivos", get_queue_size());

        // Verificar que el archivo temporal existe
        if (access(item.temp_filepath, F_OK) != 0)
        {
            LOG_ERROR("Archivo temporal no encontrado: %s", item.temp_filepath);
            send_error_response(item.client_socket, 500, "Internal Server Error");
            close(item.client_socket);
            continue;
        }

        // Procesar la imagen
        processed_image_info_t result;
        memset(&result, 0, sizeof(result));

        int processing_result = process_image_complete(item.temp_filepath,
                                                       item.upload_info.original_filename,
                                                       &result);

        if (processing_result == 0)
        {
            // Procesamiento exitoso
            LOG_INFO("✓ Imagen procesada exitosamente: %s", item.upload_info.original_filename);
            log_client_activity(item.client_ip, item.upload_info.original_filename,
                                "process", "success");

            // Enviar respuesta de éxito
            send_processing_success_response(item.client_socket, &result);

            // Actualizar estadísticas
            update_file_stats(1, item.file_size, item.upload_info.original_filename);
        }
        else
        {
            // Error en procesamiento
            LOG_ERROR("✗ Error procesando imagen: %s", item.upload_info.original_filename);
            log_client_activity(item.client_ip, item.upload_info.original_filename,
                                "process", "error");

            // Enviar respuesta de error
            send_error_response(item.client_socket, 500, "Image processing failed");

            // Actualizar estadísticas
            update_file_stats(0, item.file_size, item.upload_info.original_filename);
        }

        // Limpiar archivo temporal
        if (cleanup_temp_image(item.temp_filepath))
        {
            LOG_DEBUG("Archivo temporal limpiado: %s", item.temp_filepath);
        }
        else
        {
            LOG_WARNING("No se pudo limpiar archivo temporal: %s", item.temp_filepath);
        }

        // Cerrar socket del cliente
        close(item.client_socket);

        LOG_INFO("=== PROCESAMIENTO COMPLETADO ===");
    }

    LOG_INFO("Hilo procesador de archivos terminado");
    return NULL;
}

// Iniciar el procesador de archivos
int start_file_processor(void)
{
    if (processor_running)
    {
        LOG_WARNING("El procesador de archivos ya está ejecutándose");
        return 1;
    }

    LOG_INFO("Iniciando procesador de archivos...");

    processor_running = 1;

    if (pthread_create(&processor_thread, NULL, file_processor_thread, NULL) != 0)
    {
        LOG_ERROR("Error creando hilo procesador: %s", strerror(errno));
        processor_running = 0;
        return 0;
    }

    LOG_INFO("Procesador de archivos iniciado correctamente");
    return 1;
}

// Detener el procesador de archivos
void stop_file_processor(void)
{
    if (!processor_running)
    {
        return;
    }

    LOG_INFO("Deteniendo procesador de archivos...");

    processor_running = 0;

    // Despertar el hilo procesador
    pthread_mutex_lock(&processing_queue.queue_mutex);
    pthread_cond_signal(&processing_queue.queue_not_empty);
    pthread_mutex_unlock(&processing_queue.queue_mutex);

    // Esperar a que termine el hilo
    pthread_join(processor_thread, NULL);

    LOG_INFO("Procesador de archivos detenido");
}

// Función para debugging: imprimir estado completo de la cola
void debug_print_queue(void)
{
    pthread_mutex_lock(&processing_queue.queue_mutex);
    LOG_DEBUG("=== Estado actual de la cola ===");
    LOG_DEBUG("Tamaño: %d elementos", processing_queue.size);
    for (int i = 0; i < processing_queue.size; i++)
    {
        LOG_DEBUG("  [%d] %s - %zu bytes (recibido: %ld)", i,
                  processing_queue.items[i].upload_info.original_filename,
                  processing_queue.items[i].file_size,
                  processing_queue.items[i].received_time);
    }
    LOG_DEBUG("===============================");
    pthread_mutex_unlock(&processing_queue.queue_mutex);
}

// Función para obtener estadísticas de la cola
void get_queue_statistics(int *total_files, int *total_bytes, int *avg_file_size)
{
    pthread_mutex_lock(&processing_queue.queue_mutex);

    *total_files = processing_queue.size;
    *total_bytes = 0;

    for (int i = 0; i < processing_queue.size; i++)
    {
        *total_bytes += processing_queue.items[i].file_size;
    }

    *avg_file_size = (*total_files > 0) ? (*total_bytes / *total_files) : 0;

    pthread_mutex_unlock(&processing_queue.queue_mutex);
}

// Función para mostrar estado detallado de la cola
void print_detailed_queue_status(void)
{
    pthread_mutex_lock(&processing_queue.queue_mutex);

    LOG_INFO("=== ESTADO DE LA COLA DE PROCESAMIENTO ===");
    LOG_INFO("Elementos en cola: %d/%d", processing_queue.size, MAX_QUEUE_SIZE);
    LOG_INFO("Estado del procesador: %s", processor_running ? "ACTIVO" : "INACTIVO");

    if (processing_queue.size > 0)
    {
        LOG_INFO("Próximos archivos a procesar:");
        for (int i = 0; i < processing_queue.size && i < 5; i++)
        {
            LOG_INFO("  %d. %s (%zu bytes) - Cliente: %s",
                     i + 1,
                     processing_queue.items[i].upload_info.original_filename,
                     processing_queue.items[i].file_size,
                     processing_queue.items[i].client_ip);
        }

        if (processing_queue.size > 5)
        {
            LOG_INFO("  ... y %d archivos más", processing_queue.size - 5);
        }
    }
    else
    {
        LOG_INFO("Cola vacía - esperando archivos...");
    }

    LOG_INFO("==========================================");

    pthread_mutex_unlock(&processing_queue.queue_mutex);
}
