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
#include "server.h"

// Función para crear directorios necesarios
void create_directories(void) {
    char command[512];
    
    LOG_INFO("Creando directorios necesarios...");
    
    // Crear directorio base
    snprintf(command, sizeof(command), "mkdir -p %s", server_config.image_base_path);
    system(command);
    
    // Crear directorios específicos
    snprintf(command, sizeof(command), "mkdir -p %s", server_config.processed_path);
    system(command);
    
    snprintf(command, sizeof(command), "mkdir -p %s", server_config.green_path);
    system(command);
    
    snprintf(command, sizeof(command), "mkdir -p %s", server_config.red_path);
    system(command);
    
    snprintf(command, sizeof(command), "mkdir -p %s", server_config.blue_path);
    system(command);
    
    snprintf(command, sizeof(command), "mkdir -p %s", server_config.temp_path);
    system(command);
    
    LOG_INFO("Directorios creados correctamente");
}

// Función para ejecutar el bucle principal del daemon con servidor
void daemon_main_loop(void) {
    LOG_INFO("Iniciando bucle principal del daemon con servidor TCP");
    
    // Crear directorios necesarios
    create_directories();
    
    // Inicializar servidor
    if (!init_server()) {
        LOG_ERROR("Error inicializando servidor, terminando daemon");
        return;
    }
    
    // Iniciar servidor TCP
    if (!start_server()) {
        LOG_ERROR("Error iniciando servidor TCP, terminando daemon");
        cleanup_server();
        return;
    }
    
    LOG_INFO("Servidor TCP iniciado - Puerto: %d", server_config.port);
    LOG_INFO("Daemon ejecutándose completamente...");
    
    // Bucle principal del daemon
    while (keep_running) {
        // Verificar si necesitamos recargar configuración
        if (reload_config) {
            LOG_INFO("Recargando configuración...");
            
            // Detener servidor temporalmente
            stop_server();
            
            if (load_config(CONFIG_FILE_PATH)) {
                if (validate_config()) {
                    LOG_INFO("Configuración recargada exitosamente");
                    
                    // Reiniciar servidor con nueva configuración
                    cleanup_server();
                    if (init_server() && start_server()) {
                        LOG_INFO("Servidor reiniciado con nueva configuración");
                    } else {
                        LOG_ERROR("Error reiniciando servidor con nueva configuración");
                        break;
                    }
                } else {
                    LOG_ERROR("Configuración recargada es inválida");
                    // Intentar reiniciar con configuración anterior
                    if (init_server() && start_server()) {
                        LOG_WARNING("Continuando con configuración anterior");
                    }
                }
            } else {
                LOG_ERROR("Error recargando configuración");
            }
            
            reload_config = 0;
        }
        
        // El servidor TCP maneja las conexiones en su propio hilo
        // Aquí solo monitoreamos el estado general
        LOG_DEBUG("Daemon activo - Conexiones: %d", main_server.client_count);
        
        sleep(30); // Chequear estado cada 30 segundos
    }
    
    LOG_INFO("Saliendo del bucle principal del daemon");
    
    // Limpiar servidor
    cleanup_server();
}

// Función para modo prueba con servidor
void test_mode_with_server(void) {
    printf("=== Modo de Prueba con Servidor TCP ===\n");
    
    // Crear directorios necesarios
    create_directories();
    
    // Inicializar servidor
    if (!init_server()) {
        LOG_ERROR("Error inicializando servidor");
        return;
    }
    
    // Iniciar servidor TCP
    if (!start_server()) {
        LOG_ERROR("Error iniciando servidor TCP");
        cleanup_server();
        return;
    }
    
    printf("Servidor TCP iniciado en puerto %d\n", server_config.port);
    printf("Presiona Ctrl+C para detener el servidor\n");
    printf("Puedes probar conexiones con:\n");
    printf("  curl http://localhost:%d/\n", server_config.port);
    printf("  telnet localhost %d\n", server_config.port);
    
    // Bucle de prueba
    while (keep_running) {
        LOG_INFO("Servidor en modo prueba - Conexiones activas: %d", main_server.client_count);
        sleep(10);
    }
    
    printf("\nDeteniendo servidor...\n");
    cleanup_server();
    printf("Servidor detenido\n");
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    int test_server = 0;
    
    printf("=== ImageServer v1.0 ===\n");
    
    // Verificar argumentos de línea de comandos
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
        } else if (strcmp(argv[i], "--test-server") == 0) {
            test_server = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Uso: %s [opciones]\n", argv[0]);
            printf("Opciones:\n");
            printf("  -d              Ejecutar como daemon\n");
            printf("  --test-server   Probar servidor TCP (modo interactivo)\n");
            printf("  --help          Mostrar esta ayuda\n");
            return 0;
        }
    }
    
    // Cargar configuración
    if (!load_config(CONFIG_FILE_PATH)) {
        printf("Usando configuración por defecto\n");
    }
    
    // Mostrar configuración
    print_config();
    
    // Validar configuración
    if (!validate_config()) {
        printf("Error: Configuración inválida, terminando...\n");
        return 1;
    }
    
    // Inicializar logger
    if (!init_logger(LOG_FILE_PATH, server_config.log_level)) {
        printf("Error: No se pudo inicializar el logger\n");
        return 1;
    }
    
    printf("Configuración cargada correctamente\n");
    
    if (daemon_mode) {
        // Modo daemon completo
        printf("Iniciando como daemon con servidor TCP...\n");
        
        // Daemonizar
        if (!daemonize()) {
            LOG_ERROR("Error al daemonizar");
            close_logger();
            return 1;
        }
        
        // Configurar manejadores de señales
        setup_signal_handlers();
        
        LOG_INFO("STB Image Library cargada correctamente");
        LOG_INFO("Daemon iniciado exitosamente");
        LOG_INFO("Puerto: %d, Max conexiones: %d", 
                 server_config.port, server_config.max_connections);
        
        // Ejecutar bucle principal con servidor
        daemon_main_loop();
        
        // Limpieza
        cleanup_daemon();
        
    } else if (test_server) {
        // Modo prueba de servidor
        printf("=== Modo Prueba de Servidor TCP ===\n");
        printf("Este modo iniciará el servidor TCP para pruebas\n");
        
        // Configurar señales básicas
        setup_signal_handlers();
        
        LOG_INFO("STB Image Library cargada correctamente");
        LOG_INFO("Iniciando modo prueba de servidor");
        
        // Ejecutar servidor en modo prueba
        test_mode_with_server();
        
        close_logger();
        
    } else {
        // Modo prueba básico (sin servidor)
        printf("Ejecutando en modo de prueba básico\n");
        printf("Usa -d para ejecutar como daemon\n");
        printf("Usa --test-server para probar el servidor TCP\n");
        
        // Configurar señales básicas
        setup_signal_handlers();
        
        LOG_INFO("STB Image Library cargada correctamente");
        LOG_INFO("Puerto configurado: %d", server_config.port);
        LOG_INFO("=== Modo de prueba básico ===");
        
        // Simular actividad básica
        log_client_activity("127.0.0.1", "test.jpg", "test", "success");
        
        LOG_INFO("Prueba básica completada");
        printf("\nOpciones disponibles:\n");
        printf("  ./imageserver -d              (modo daemon)\n");
        printf("  ./imageserver --test-server   (probar servidor TCP)\n");
        
        close_logger();
    }
    
    return 0;
}
