#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "file_handler.h"
#include "server.h"

#define MAX_QUEUE_SIZE 100

// Estructura para elementos en la cola de prioridad
typedef struct
{
    file_upload_info_t upload_info;
    size_t file_size;
    time_t received_time;
    char temp_filepath[512];
    char client_ip[64];
    int client_socket;
    int priority; // Menor tamaño = mayor prioridad (menor número)
} priority_queue_item_t;

// Cola de prioridad (min-heap basada en tamaño de archivo)
typedef struct
{
    priority_queue_item_t items[MAX_QUEUE_SIZE];
    int size;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_not_empty;
    pthread_cond_t queue_not_full;
    int active; // Para controlar el shutdown del procesador
} priority_queue_t;

// Variable global de la cola de prioridad
extern priority_queue_t processing_queue;

// Funciones de la cola de prioridad
int init_priority_queue(void);
void destroy_priority_queue(void);

// Operaciones de la cola
int enqueue_file_for_processing(const file_upload_info_t *upload_info,
                                const char *temp_filepath,
                                const char *client_ip,
                                int client_socket);

int dequeue_file_for_processing(priority_queue_item_t *item);

// Funciones auxiliares
int is_queue_empty(void);
int is_queue_full(void);
int get_queue_size(void);
void print_queue_status(void);

// Función para debugging
void debug_print_queue(void);

// Hilo procesador de archivos
void *file_processor_thread(void *arg);
int start_file_processor(void);
void stop_file_processor(void);

void get_queue_statistics(int *total_files, int *total_bytes, int *avg_file_size);

// Variables globales del procesador
extern pthread_t processor_thread;
extern int processor_running;

#endif // PRIORITY_QUEUE_H