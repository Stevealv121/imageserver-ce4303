#include "config.h"

// Variable global de configuración
server_config_t server_config;

// Establecer valores por defecto
void set_default_config(void) {
    server_config.port = 1717;
    strcpy(server_config.log_level, "INFO");
    server_config.max_connections = 10;
    server_config.thread_pool_size = 4;
    
    // Rutas por defecto
    strcpy(server_config.image_base_path, "/var/imageserver/images");
    strcpy(server_config.processed_path, "/var/imageserver/images/processed");
    strcpy(server_config.green_path, "/var/imageserver/images/verdes");
    strcpy(server_config.red_path, "/var/imageserver/images/rojas");
    strcpy(server_config.blue_path, "/var/imageserver/images/azules");
    strcpy(server_config.temp_path, "/var/imageserver/images/temp");
    
    // Configuración de procesamiento
    server_config.max_image_size_mb = 50;
    strcpy(server_config.supported_formats, "jpg,jpeg,png,gif");
    server_config.histogram_bins = 256;
}

// Función auxiliar para eliminar espacios en blanco
void trim_whitespace(char* str) {
    char* end;
    
    // Eliminar espacios del inicio
    while(*str == ' ' || *str == '\t') str++;
    
    // Si es string vacío
    if(*str == 0) return;
    
    // Eliminar espacios del final
    end = str + strlen(str) - 1;
    while(end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    
    // Agregar terminador
    end[1] = '\0';
}

// Cargar configuración desde archivo
int load_config(const char* config_file) {
    FILE *file;
    char line[MAX_CONFIG_LINE];
    char key[128], value[384];
    
    // Establecer valores por defecto primero
    set_default_config();
    
    // Abrir archivo de configuración
    file = fopen(config_file, "r");
    if (!file) {
        printf("Warning: No se pudo abrir %s, usando configuración por defecto\n", config_file);
        return 0; // No es error crítico, usamos defaults
    }
    
    printf("Cargando configuración desde: %s\n", config_file);
    
    // Leer línea por línea
    while (fgets(line, sizeof(line), file)) {
        // Saltar comentarios y líneas vacías
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        
        // Parsear línea: KEY=VALUE
        if (sscanf(line, "%127[^=]=%383[^\n]", key, value) == 2) {
            trim_whitespace(key);
            trim_whitespace(value);
            
            // Procesar cada clave de configuración
            if (strcmp(key, "PORT") == 0) {
                server_config.port = atoi(value);
            }
            else if (strcmp(key, "LOG_LEVEL") == 0) {
                strcpy(server_config.log_level, value);
            }
            else if (strcmp(key, "MAX_CONNECTIONS") == 0) {
                server_config.max_connections = atoi(value);
            }
            else if (strcmp(key, "THREAD_POOL_SIZE") == 0) {
                server_config.thread_pool_size = atoi(value);
            }
            else if (strcmp(key, "IMAGE_BASE_PATH") == 0) {
                strcpy(server_config.image_base_path, value);
            }
            else if (strcmp(key, "PROCESSED_PATH") == 0) {
                strcpy(server_config.processed_path, value);
            }
            else if (strcmp(key, "GREEN_PATH") == 0) {
                strcpy(server_config.green_path, value);
            }
            else if (strcmp(key, "RED_PATH") == 0) {
                strcpy(server_config.red_path, value);
            }
            else if (strcmp(key, "BLUE_PATH") == 0) {
                strcpy(server_config.blue_path, value);
            }
            else if (strcmp(key, "TEMP_PATH") == 0) {
                strcpy(server_config.temp_path, value);
            }
            else if (strcmp(key, "MAX_IMAGE_SIZE_MB") == 0) {
                server_config.max_image_size_mb = atoi(value);
            }
            else if (strcmp(key, "SUPPORTED_FORMATS") == 0) {
                strcpy(server_config.supported_formats, value);
            }
            else if (strcmp(key, "HISTOGRAM_BINS") == 0) {
                server_config.histogram_bins = atoi(value);
            }
        }
    }
    
    fclose(file);
    printf("Configuración cargada exitosamente\n");
    return 1;
}

// Imprimir configuración actual
void print_config(void) {
    printf("\n=== Configuración del Servidor ===\n");
    printf("Puerto: %d\n", server_config.port);
    printf("Nivel de Log: %s\n", server_config.log_level);
    printf("Max Conexiones: %d\n", server_config.max_connections);
    printf("Thread Pool: %d\n", server_config.thread_pool_size);
    printf("\nRutas:\n");
    printf("  Base: %s\n", server_config.image_base_path);
    printf("  Procesadas: %s\n", server_config.processed_path);
    printf("  Verdes: %s\n", server_config.green_path);
    printf("  Rojas: %s\n", server_config.red_path);
    printf("  Azules: %s\n", server_config.blue_path);
    printf("  Temporal: %s\n", server_config.temp_path);
    printf("\nProcesamiento:\n");
    printf("  Tamaño máximo: %d MB\n", server_config.max_image_size_mb);
    printf("  Formatos: %s\n", server_config.supported_formats);
    printf("  Histogram bins: %d\n", server_config.histogram_bins);
    printf("================================\n\n");
}

// Validar configuración
int validate_config(void) {
    if (server_config.port < 1024 || server_config.port > 65535) {
        printf("Error: Puerto inválido (%d). Debe estar entre 1024-65535\n", server_config.port);
        return 0;
    }
    
    if (server_config.max_connections <= 0 || server_config.max_connections > 1000) {
        printf("Error: Max conexiones inválido (%d)\n", server_config.max_connections);
        return 0;
    }
    
    if (server_config.thread_pool_size <= 0 || server_config.thread_pool_size > 50) {
        printf("Error: Thread pool size inválido (%d)\n", server_config.thread_pool_size);
        return 0;
    }
    
    printf("Configuración validada correctamente\n");
    return 1;
}
