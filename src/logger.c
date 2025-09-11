#include "logger.h"

// Logger global
logger_t server_logger;

// Obtener timestamp actual
const char* get_timestamp(void) {
    static char timestamp[64];
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", local_time);
    return timestamp;
}

// Convertir nivel de log a string
const char* log_level_to_string(log_level_t level) {
    switch(level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO: return "INFO";
        case LOG_WARNING: return "WARNING";
        case LOG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// Convertir string a nivel de log
log_level_t string_to_log_level(const char* level_str) {
    if (strcmp(level_str, "DEBUG") == 0) return LOG_DEBUG;
    if (strcmp(level_str, "INFO") == 0) return LOG_INFO;
    if (strcmp(level_str, "WARNING") == 0) return LOG_WARNING;
    if (strcmp(level_str, "ERROR") == 0) return LOG_ERROR;
    return LOG_INFO; // Por defecto
}

// Inicializar el logger
int init_logger(const char* log_file_path, const char* log_level_str) {
    // Inicializar mutex
    if (pthread_mutex_init(&server_logger.log_mutex, NULL) != 0) {
        fprintf(stderr, "Error: No se pudo inicializar mutex del logger\n");
        return 0;
    }
    
    // Establecer nivel de log
    server_logger.current_level = string_to_log_level(log_level_str);
    server_logger.console_output = 1; // Activar salida por consola
    
    // Intentar abrir archivo de log
    server_logger.log_file = fopen(log_file_path, "a");
    if (!server_logger.log_file) {
        fprintf(stderr, "Warning: No se pudo abrir %s para logging\n", log_file_path);
        fprintf(stderr, "El logging continuará solo por consola\n");
        server_logger.log_file = NULL;
    } else {
        printf("Logger inicializado: %s (nivel: %s)\n", log_file_path, log_level_str);
    }
    
    // Log de inicio del sistema
    log_message(LOG_INFO, "=== ImageServer iniciado ===");
    log_message(LOG_INFO, "Logger inicializado - Nivel: %s", log_level_str);
    log_message(LOG_INFO, "PID: %d", getpid());
    
    return 1;
}

// Cerrar el logger
void close_logger(void) {
    log_message(LOG_INFO, "=== ImageServer terminando ===");
    
    if (server_logger.log_file) {
        fclose(server_logger.log_file);
        server_logger.log_file = NULL;
    }
    
    pthread_mutex_destroy(&server_logger.log_mutex);
}

// Escribir mensaje de log
void log_message(log_level_t level, const char* format, ...) {
    // Verificar si debe loggear este nivel
    if (level < server_logger.current_level) {
        return;
    }
    
    // Obtener el mutex
    pthread_mutex_lock(&server_logger.log_mutex);
    
    // Preparar mensaje
    char message[MAX_LOG_MESSAGE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // Crear línea de log completa
    const char* timestamp = get_timestamp();
    const char* level_str = log_level_to_string(level);
    
    // Escribir a archivo si está disponible
    if (server_logger.log_file) {
        fprintf(server_logger.log_file, "[%s] [%s] %s\n", timestamp, level_str, message);
        fflush(server_logger.log_file);
    }
    
    // Escribir a consola si está habilitado
    if (server_logger.console_output) {
        printf("[%s] [%s] %s\n", timestamp, level_str, message);
    }
    
    // Liberar mutex
    pthread_mutex_unlock(&server_logger.log_mutex);
}

// Log específico para actividad de clientes
void log_client_activity(const char* client_ip, const char* filename, const char* action, const char* status) {
    log_message(LOG_INFO, "Cliente: %s | Archivo: %s | Acción: %s | Estado: %s", 
                client_ip ? client_ip : "unknown", 
                filename ? filename : "unknown", 
                action ? action : "unknown", 
                status ? status : "unknown");
}
