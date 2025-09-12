#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATH_LENGTH 256
#define MAX_CONFIG_LINE 512
#define CONFIG_FILE_PATH "/etc/server/config.conf"

// Estructura para almacenar la configuración
typedef struct {
    int port;
    char log_level[32];
    int max_connections;
    int thread_pool_size;
    
    // Rutas de directorios
    char image_base_path[MAX_PATH_LENGTH];
    char processed_path[MAX_PATH_LENGTH];
    char green_path[MAX_PATH_LENGTH];
    char red_path[MAX_PATH_LENGTH];
    char blue_path[MAX_PATH_LENGTH];
    char temp_path[MAX_PATH_LENGTH];
    
    // Configuración de procesamiento
    int max_image_size_mb;
    char supported_formats[256];
    int histogram_bins;
} server_config_t;

// Configuración global
extern server_config_t server_config;

// Funciones de configuración
int load_config(const char* config_file);
void print_config(void);
int validate_config(void);
void set_default_config(void);

#endif // CONFIG_H
