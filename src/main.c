#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// Definir implementaciones de STB (solo una vez)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "config.h"
#include "logger.h"
#include "daemon.h"

// Función para ejecutar el bucle principal del daemon
void daemon_main_loop(void) {
    LOG_INFO("Iniciando bucle principal del daemon");
    
    while (keep_running) {
        // Verificar si necesitamos recargar configuración
        if (reload_config) {
            LOG_INFO("Recargando configuración...");
            
            if (load_config(CONFIG_FILE_PATH)) {
                if (validate_config()) {
                    LOG_INFO("Configuración recargada exitosamente");
                } else {
                    LOG_ERROR("Configuración recargada es inválida");
                }
            } else {
                LOG_ERROR("Error recargando configuración");
            }
            
            reload_config = 0;
        }
        
        // Por ahora, el daemon solo espera
        // En próximas partes aquí estará el servidor de sockets
        LOG_INFO("Daemon ejecutándose... Puerto: %d", server_config.port);
        
        sleep(10); // Esperar 10 segundos
    }
    
    LOG_INFO("Saliendo del bucle principal del daemon");
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    
    // Verificar argumentos de línea de comandos
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }
    
    printf("=== ImageServer v1.0 ===\n");
    
    // Cargar configuración
    if (!load_config(CONFIG_FILE_PATH)) {
        printf("Usando configuración por defecto\n");
    }
    
    // Validar configuración
    if (!validate_config()) {
        printf("Error: Configuración inválida, terminando...\n");
        return 1;
    }
    
    printf("Configuración cargada correctamente\n");
    
    if (daemon_mode) {
        // Modo daemon
        printf("Iniciando como daemon...\n");
        
        // Inicializar logger antes de daemonizar
        if (!init_logger(LOG_FILE_PATH, server_config.log_level)) {
            printf("Error: No se pudo inicializar el logger\n");
            return 1;
        }
        
        // Daemonizar
        if (!daemonize()) {
            LOG_ERROR("Error al daemonizar");
            return 1;
        }
        
        // Configurar manejadores de señales
        setup_signal_handlers();
        
        LOG_INFO("STB Image Library cargada correctamente");
        LOG_INFO("Daemon iniciado exitosamente");
        LOG_INFO("Puerto: %d, Max conexiones: %d", 
                 server_config.port, server_config.max_connections);
        
        // Ejecutar bucle principal
        daemon_main_loop();
        
        // Limpieza
        cleanup_daemon();
        
    } else {
        // Modo prueba (no daemon)
        printf("Ejecutando en modo de prueba (no daemon)\n");
        printf("Usa -d para ejecutar como daemon\n");
        
        // Inicializar logger
        if (!init_logger(LOG_FILE_PATH, server_config.log_level)) {
            printf("Error: No se pudo inicializar el logger\n");
            return 1;
        }
        
        // Configurar señales básicas
        setup_signal_handlers();
        
        LOG_INFO("STB Image Library cargada correctamente");
        LOG_INFO("Puerto configurado: %d", server_config.port);
        LOG_INFO("=== Modo de prueba ===");
        
        // Simular actividad
        log_client_activity("127.0.0.1", "test.jpg", "test", "success");
        
        LOG_INFO("Prueba completada - usa './imageserver -d' para modo daemon");
        
        close_logger();
    }
    
    return 0;
}
