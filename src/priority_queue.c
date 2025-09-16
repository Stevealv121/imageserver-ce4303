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
        LOG_ERROR("Parámetros inválidos para enqueue_file_for_processing");
        return -1;
    }

    pthread_mutex_lock(&processing_queue.queue_mutex);

    // Verificar si la cola está activa
    if (!processing_queue.active)
    {
        LOG_WARNING("Cola de procesamiento inactiva, rechazando archivo");
        pthread_mutex_unlock(&processing_queue.queue_mutex);
        return -1;
    }

    // Esperar si la cola está llena
    while (processing_queue.size >= MAX_QUEUE_SIZE && processing_queue.active)
    {
        LOG_WARNING("Cola llena (%d/%d), esperando espacio...",
                    processing_queue.size, MAX_QUEUE_SIZE);
        pthread_cond_wait(&processing_queue.queue_not_full, &processing_queue.queue_mutex);
    }

    if (!processing_queue.active)
    {
        pthread_mutex_unlock(&processing_queue.queue_mutex);
        return -1;
    }

    // Crear nuevo item
    priority_queue_item_t *item = &processing_queue.items[processing_queue.size];

    // Copiar información del upload
    item->upload_info = *upload_info;
    item->file_size = upload_info->file_size;
    item->received_time = time(NULL);
    item->client_socket = client_socket;
    item->priority = (int)upload_info->file_size; // Prioridad = tamaño

    // Copiar strings
    strncpy(item->temp_filepath, temp_filepath, sizeof(item->temp_filepath) - 1);
    item->temp_filepath[sizeof(item->temp_filepath) - 1] = '\0';

    strncpy(item->client_ip, client_ip, sizeof(item->client_ip) - 1);
    item->client_ip[sizeof(item->client_ip) - 1] = '\0';

    // Insertar en heap
    processing_queue.size++;
    heapify_up(processing_queue.size - 1);

    LOG_INFO("   ARCHIVO ENCOLADO:");
    LOG_INFO("   Archivo: %s (%zu bytes)", upload_info->original_filename, upload_info->file_size);
    LOG_INFO("   Cliente: %s", client_ip);
    LOG_INFO("   Posición en cola: %d/%d", processing_queue.size, MAX_QUEUE_SIZE);

    // Mostrar orden actual de la cola (primeros 3)
    LOG_INFO("   Orden de procesamiento (próximos 3):");
    int show_count = (processing_queue.size < 3) ? processing_queue.size : 3;
    for (int i = 0; i < show_count; i++)
    {
        LOG_INFO("     %d. %s (%zu bytes)", i + 1,
                 processing_queue.items[i].upload_info.original_filename,
                 processing_queue.items[i].file_size);
    }

    // Notificar que hay un nuevo elemento
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

    // Esperar hasta que haya elementos o se desactive la cola
    while (processing_queue.size == 0 && processing_queue.active)
    {
        pthread_cond_wait(&processing_queue.queue_not_empty, &processing_queue.queue_mutex);
    }

    if (!processing_queue.active || processing_queue.size == 0)
    {
        pthread_mutex_unlock(&processing_queue.queue_mutex);
        return -1;
    }

    // Extraer el elemento con mayor prioridad (índice 0)
    *item = processing_queue.items[0];

    // Mover el último elemento al inicio y reducir tamaño
    processing_queue.items[0] = processing_queue.items[processing_queue.size - 1];
    processing_queue.size--;

    // Rebalancear heap si no está vacío
    if (processing_queue.size > 0)
    {
        heapify_down(0);
    }

    LOG_DEBUG("Archivo extraído de cola: %s (%zu bytes) - Elementos restantes: %d",
              item->upload_info.original_filename, item->file_size, processing_queue.size);

    // Notificar que hay espacio disponible
    pthread_cond_signal(&processing_queue.queue_not_full);
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

// Hilo procesador de archivos
void *file_processor_thread(void *arg)
{
    (void)arg; // Evitar warning

    LOG_INFO("Hilo procesador de archivos iniciado");

    while (processor_running)
    {
        priority_queue_item_t item;

        // Extraer archivo de la cola (BLOQUEA hasta que haya elementos)
        if (dequeue_file_for_processing(&item) == 0)
        {
            LOG_INFO("=== PROCESANDO ARCHIVO ===");
            LOG_INFO("Archivo: %s (%zu bytes) desde %s",
                     item.upload_info.original_filename, item.file_size, item.client_ip);
            LOG_INFO("Cola restante: %d archivos", get_queue_size());

            // Actualizar estadísticas
            file_stats_t *stats = get_file_stats();
            stats->total_uploads++;
            stats->total_bytes_processed += item.file_size;

            // Procesar imagen completa
            processed_image_info_t result;
            memset(&result, 0, sizeof(result));

            if (process_image_complete(item.temp_filepath, item.upload_info.original_filename, &result) == 0)
            {
                LOG_INFO("✓ Imagen procesada exitosamente: %s", item.upload_info.original_filename);

                // Actualizar estadísticas de éxito
                file_stats_t *stats = get_file_stats();
                stats->successful_uploads++;

                // Enviar respuesta de éxito al cliente
                char response_body[512];
                snprintf(response_body, sizeof(response_body),
                         "{\n"
                         "  \"status\": \"success\",\n"
                         "  \"message\": \"File processed successfully\",\n"
                         "  \"filename\": \"%s\",\n"
                         "  \"size\": %zu,\n"
                         "  \"processed_path\": \"%s\",\n"
                         "  \"predominant_color\": \"%s\",\n"
                         "  \"processing_time\": %ld\n"
                         "}",
                         item.upload_info.original_filename,
                         item.file_size,
                         result.equalized_path,
                         (result.predominant_color == COLOR_RED) ? "red" : (result.predominant_color == COLOR_GREEN) ? "green"
                                                                       : (result.predominant_color == COLOR_BLUE)    ? "blue"
                                                                                                                     : "undefined",
                         time(NULL) - item.received_time);

                send_success_response(item.client_socket, "application/json", response_body);
                log_client_activity(item.client_ip, item.upload_info.original_filename, "process", "success");
            }
            else
            {
                LOG_ERROR("✗ Error procesando imagen: %s", item.upload_info.original_filename);

                // Actualizar estadísticas de fallo
                file_stats_t *stats = get_file_stats();
                stats->failed_uploads++;

                send_error_response(item.client_socket, 500, "Failed to process image");
                log_client_activity(item.client_ip, item.upload_info.original_filename, "process", "failed");
            }

            // Limpiar archivo temporal
            cleanup_temp_image(item.temp_filepath);

            // IMPORTANTE: Cerrar socket del cliente al final
            close(item.client_socket);

            LOG_INFO("=== PROCESAMIENTO COMPLETADO ===");
            LOG_INFO("Cliente desconectado: %s", item.client_ip);
        }
        else
        {
            // Si dequeue falla, puede ser porque se está cerrando el procesador
            if (processor_running)
            {
                LOG_DEBUG("dequeue_file_for_processing falló, reintentando...");
                usleep(100000); // 100ms
            }
        }
    }

    LOG_INFO("Hilo procesador de archivos terminando");
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
