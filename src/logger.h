#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_LOG_MESSAGE 1024
#define MAX_LOG_FILENAME 256
#define LOG_DIR "/var/log/imageserver"
#define LOG_FILE_PATH "/var/log/imageserver/imageserver.log"

// Niveles de log
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARNING = 2,
    LOG_ERROR = 3
} log_level_t;

// Estructura para el logger
typedef struct {
    FILE *log_file;
    log_level_t current_level;
    pthread_mutex_t log_mutex;
    int console_output;
} logger_t;

// Logger global
extern logger_t server_logger;

// Funciones del logger
int init_logger(const char* log_file_path, const char* log_level_str);
void close_logger(void);
void log_message(log_level_t level, const char* format, ...);
void log_client_activity(const char* client_ip, const char* filename, const char* action, const char* status);
const char* get_timestamp(void);
const char* log_level_to_string(log_level_t level);
log_level_t string_to_log_level(const char* level_str);

// Macros para facilitar el uso
#define LOG_DEBUG(fmt, ...) log_message(LOG_DEBUG, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) log_message(LOG_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) log_message(LOG_WARNING, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_message(LOG_ERROR, fmt, ##__VA_ARGS__)

#endif // LOGGER_H
