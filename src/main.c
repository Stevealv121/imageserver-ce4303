#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Definir implementaciones de STB (solo en un archivo .c)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "config.h"
#include "logger.h"
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

int main() {
    printf("=== ImageServer v1.0 - Iniciando ===\n");
    
    // Cargar configuración
    printf("\n--- Cargando Configuración ---\n");
    if (!load_config(CONFIG_FILE_PATH)) {
        printf("Usando configuración por defecto\n");
    }
    
    // Validar configuración
    if (!validate_config()) {
        printf("Error: Configuración inválida, terminando...\n");
        return 1;
    }
    
    // Inicializar logger
    printf("\n--- Inicializando Logger ---\n");
    if (!init_logger(LOG_FILE_PATH, server_config.log_level)) {
        printf("Error: No se pudo inicializar el logger\n");
        return 1;
    }
    
    // Usar el logger para mostrar información
    LOG_INFO("STB Image Library cargada correctamente");
    LOG_INFO("Puerto configurado: %d", server_config.port);
    
    // Mostrar configuración usando el logger
    LOG_INFO("=== Configuración Cargada ===");
    LOG_INFO("Puerto: %d", server_config.port);
    LOG_INFO("Max Conexiones: %d", server_config.max_connections);
    LOG_INFO("Thread Pool: %d", server_config.thread_pool_size);
    LOG_INFO("Rutas configuradas correctamente");
    
    // Simular algunas actividades de clientes para probar el log
    LOG_INFO("--- Simulando actividad ---");
    log_client_activity("192.168.1.100", "test_image.jpg", "upload", "success");
    log_client_activity("192.168.1.101", "photo.png", "process", "success");
    log_client_activity("192.168.1.102", "invalid.gif", "upload", "error");
    
    LOG_WARNING("Ejemplo de mensaje de warning");
    LOG_ERROR("Ejemplo de mensaje de error (no crítico, solo prueba)");
    
    LOG_INFO("Formatos soportados por STB: JPG, PNG, GIF, BMP");
    LOG_INFO("Servidor listo para iniciar");
    
    LOG_INFO("=== Prueba de configuración y logging completada ===");
    
    // Cerrar logger
    close_logger();
    
    return 0;
}
