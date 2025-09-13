#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "config.h"
#include "logger.h"
#include "daemon.h"
#include "file_handler.h"
#include "server.h"

// Función para crear directorios necesarios (mejorada)
int create_directories(void)
{
    LOG_INFO("Creando directorios necesarios...");

    struct
    {
        const char *path;
        const char *name;
    } dirs[] = {
        {server_config.image_base_path, "directorio base"},
        {server_config.processed_path, "directorio procesados"},
        {server_config.green_path, "directorio verdes"},
        {server_config.red_path, "directorio rojos"},
        {server_config.blue_path, "directorio azules"},
        {server_config.temp_path, "directorio temporal"},
        {NULL, NULL} // Terminador
    };

    for (int i = 0; dirs[i].path != NULL; i++)
    {
        // Crear directorio recursivamente
        char command[512];
        snprintf(command, sizeof(command), "mkdir -p %s", dirs[i].path);

        if (system(command) != 0)
        {
            LOG_ERROR("Error creando %s: %s", dirs[i].name, dirs[i].path);
            return 0;
        }

        // Verificar que se creó correctamente
        struct stat st;
        if (stat(dirs[i].path, &st) != 0 || !S_ISDIR(st.st_mode))
        {
            LOG_ERROR("No se pudo verificar %s: %s", dirs[i].name, dirs[i].path);
            return 0;
        }

        // Configurar permisos
        chmod(dirs[i].path, 0755);

        LOG_DEBUG("Creado %s: %s", dirs[i].name, dirs[i].path);
    }

    LOG_INFO("Todos los directorios creados correctamente");
    return 1;
}

// Función para mostrar estadísticas del servidor
void show_server_stats(void)
{
    const file_stats_t *stats = get_file_stats();

    LOG_INFO("=== Estadísticas del Servidor ===");
    LOG_INFO("Estado: %s", is_server_running() ? "EJECUTÁNDOSE" : "DETENIDO");
    LOG_INFO("Puerto: %d", server_config.port);
    LOG_INFO("Conexiones activas: %d/%d", get_active_clients(), server_config.max_connections);
    LOG_INFO("Total uploads: %d", stats->total_uploads);
    LOG_INFO("Uploads exitosos: %d", stats->successful_uploads);
    LOG_INFO("Uploads fallidos: %d", stats->failed_uploads);
    LOG_INFO("Bytes procesados: %zu", stats->total_bytes_processed);
    LOG_INFO("=====================================");
}

// Función para ejecutar el bucle principal del daemon con servidor
void daemon_main_loop(void)
{
    LOG_INFO("Iniciando bucle principal del daemon con servidor TCP");

    // Crear directorios necesarios
    if (!create_directories())
    {
        LOG_ERROR("Error creando directorios necesarios, terminando daemon");
        return;
    }

    // Inicializar servidor
    if (!init_server())
    {
        LOG_ERROR("Error inicializando servidor, terminando daemon");
        return;
    }

    // Iniciar servidor TCP
    if (!start_server())
    {
        LOG_ERROR("Error iniciando servidor TCP, terminando daemon");
        cleanup_server();
        return;
    }

    LOG_INFO("Servidor TCP iniciado - Puerto: %d", server_config.port);
    LOG_INFO("Daemon ejecutándose completamente...");

    // Contador para estadísticas periódicas
    int stats_counter = 0;

    // Bucle principal del daemon
    while (keep_running)
    {
        // Verificar si necesitamos recargar configuración
        if (reload_config)
        {
            LOG_INFO("Recargando configuración...");

            // Mostrar estadísticas antes de recargar
            show_server_stats();

            // Detener servidor temporalmente
            stop_server();

            if (load_config(CONFIG_FILE_PATH))
            {
                if (validate_config())
                {
                    LOG_INFO("Configuración recargada exitosamente");

                    // Recrear directorios si es necesario
                    create_directories();

                    // Reiniciar servidor con nueva configuración
                    cleanup_server();
                    if (init_server() && start_server())
                    {
                        LOG_INFO("Servidor reiniciado con nueva configuración - Puerto: %d",
                                 server_config.port);
                    }
                    else
                    {
                        LOG_ERROR("Error reiniciando servidor con nueva configuración");
                        break;
                    }
                }
                else
                {
                    LOG_ERROR("Configuración recargada es inválida");
                    // Intentar reiniciar con configuración anterior
                    if (init_server() && start_server())
                    {
                        LOG_WARNING("Continuando con configuración anterior");
                    }
                    else
                    {
                        LOG_ERROR("No se pudo reiniciar el servidor");
                        break;
                    }
                }
            }
            else
            {
                LOG_ERROR("Error recargando configuración");
            }

            reload_config = 0;
        }

        // Mostrar estadísticas periódicamente (cada 5 minutos)
        stats_counter++;
        if (stats_counter >= 10)
        { // 10 * 30s = 5 minutos
            show_server_stats();
            stats_counter = 0;
        }

        // El servidor TCP maneja las conexiones en su propio hilo
        // Aquí solo monitoreamos el estado general
        if (is_server_running())
        {
            LOG_DEBUG("Daemon activo - Conexiones: %d/%d",
                      get_active_clients(), server_config.max_connections);
        }
        else
        {
            LOG_WARNING("El servidor TCP no está ejecutándose");
            break;
        }

        sleep(30); // Chequear estado cada 30 segundos
    }

    LOG_INFO("Saliendo del bucle principal del daemon");

    // Mostrar estadísticas finales
    show_server_stats();

    // Limpiar servidor
    cleanup_server();
}

// Función para modo prueba con servidor (mejorada)
void test_mode_with_server(void)
{
    printf("=== Modo de Prueba con Servidor TCP ===\n");

    // Crear directorios necesarios
    if (!create_directories())
    {
        printf("Error: No se pudieron crear los directorios necesarios\n");
        return;
    }

    printf("Directorios creados correctamente\n");

    // Inicializar servidor
    if (!init_server())
    {
        LOG_ERROR("Error inicializando servidor");
        printf("Error: No se pudo inicializar el servidor\n");
        return;
    }

    // Iniciar servidor TCP
    if (!start_server())
    {
        LOG_ERROR("Error iniciando servidor TCP");
        printf("Error: No se pudo iniciar el servidor TCP\n");
        cleanup_server();
        return;
    }

    printf("Servidor TCP iniciado exitosamente\n");
    printf("Puerto: %d\n", server_config.port);
    printf("Conexiones máximas: %d\n", server_config.max_connections);
    printf("Formatos soportados: %s\n", server_config.supported_formats);
    printf("Tamaño máximo: %d MB\n", server_config.max_image_size_mb);

    printf("\n=== Endpoints disponibles ===\n");
    printf("GET  http://localhost:%d/         - Estado del servidor\n", server_config.port);
    printf("GET  http://localhost:%d/status   - Estado del servidor\n", server_config.port);
    printf("GET  http://localhost:%d/upload   - Información de upload\n", server_config.port);
    printf("POST http://localhost:%d/         - Subir imagen (multipart/form-data)\n", server_config.port);

    printf("\n=== Comandos de prueba ===\n");
    printf("curl http://localhost:%d/status\n", server_config.port);
    printf("curl -X POST -F \"image=@tu_imagen.jpg\" http://localhost:%d/\n", server_config.port);

    printf("\nPresiona Ctrl+C para detener el servidor\n");
    printf("Monitoreando servidor...\n\n");

    // Bucle de prueba con más información
    int loop_count = 0;
    while (keep_running)
    {
        if (is_server_running())
        {
            int clients = get_active_clients();
            if (clients > 0)
            {
                printf("[%d] Servidor activo - Conexiones: %d\n", ++loop_count, clients);
            }
            else if (loop_count % 6 == 0)
            { // Cada minuto mostrar estado
                printf("[%d] Servidor activo - Sin conexiones\n", ++loop_count);
            }

            // Mostrar estadísticas cada 2 minutos
            if (loop_count % 12 == 0)
            {
                const file_stats_t *stats = get_file_stats();
                if (stats->total_uploads > 0)
                {
                    printf("  Estadísticas: %d uploads (%d exitosos, %d fallidos)\n",
                           stats->total_uploads, stats->successful_uploads, stats->failed_uploads);
                }
            }
        }
        else
        {
            printf("ADVERTENCIA: El servidor TCP se ha detenido inesperadamente\n");
            break;
        }

        sleep(10);
    }

    printf("\nDeteniendo servidor...\n");
    cleanup_server();
    printf("Servidor detenido correctamente\n");
}

// Función para mostrar ayuda detallada
void show_help(const char *program_name)
{
    printf("=== ImageServer v1.0 - Servidor de Procesamiento de Imágenes ===\n\n");
    printf("DESCRIPCIÓN:\n");
    printf("  Servidor daemon que procesa imágenes aplicando ecualización de histograma\n");
    printf("  y clasificación por color predominante (rojo, verde, azul).\n\n");

    printf("USO: %s [opciones]\n\n", program_name);

    printf("OPCIONES:\n");
    printf("  -d, --daemon         Ejecutar como daemon del sistema\n");
    printf("  --test-server        Modo de prueba interactivo del servidor TCP\n");
    printf("  --help               Mostrar esta ayuda\n\n");

    printf("ARCHIVOS DE CONFIGURACIÓN:\n");
    printf("  %s     - Configuración principal\n", CONFIG_FILE_PATH);
    printf("  %s          - Archivo de logs\n\n", LOG_FILE_PATH);

    printf("FORMATOS SOPORTADOS:\n");
    printf("  Entrada: JPG, JPEG, PNG, GIF\n");
    printf("  Salida: JPG (procesadas), PNG (clasificadas)\n\n");

    printf("ENDPOINTS HTTP:\n");
    printf("  GET  /status    - Estado y estadísticas del servidor\n");
    printf("  GET  /upload    - Información sobre cómo subir archivos\n");
    printf("  POST /          - Subir imagen (multipart/form-data)\n\n");

    printf("EJEMPLOS:\n");
    printf("  %s -d                    # Ejecutar como daemon\n", program_name);
    printf("  %s --test-server         # Probar servidor en modo interactivo\n", program_name);
    printf("  curl http://localhost:1717/status  # Verificar estado\n");
    printf("  curl -F \"image=@foto.jpg\" http://localhost:1717/\n\n");
}

// Función para ejecutar comandos del sistema y mostrar salida
int execute_system_command(const char *command)
{
    int result = system(command);
    return WEXITSTATUS(result);
}

// Función para manejar comandos de servicio
int handle_service_command(const char *command)
{
    char system_command[256];

    if (strcmp(command, "start") == 0)
    {
        printf("Iniciando ImageServer...\n");
        snprintf(system_command, sizeof(system_command), "systemctl start ImageServer");
        int result = execute_system_command(system_command);
        if (result == 0)
        {
            printf("ImageServer iniciado correctamente\n");
            // Mostrar status después de iniciar
            snprintf(system_command, sizeof(system_command), "systemctl status ImageServer --no-pager -l");
            execute_system_command(system_command);
        }
        else
        {
            printf("Error iniciando ImageServer\n");
        }
        return result;
    }

    else if (strcmp(command, "stop") == 0)
    {
        printf("Deteniendo ImageServer...\n");
        snprintf(system_command, sizeof(system_command), "systemctl stop ImageServer");
        int result = execute_system_command(system_command);
        if (result == 0)
        {
            printf("ImageServer detenido correctamente\n");
        }
        else
        {
            printf("Error deteniendo ImageServer\n");
        }
        return result;
    }

    else if (strcmp(command, "status") == 0)
    {
        printf("Estado de ImageServer:\n");
        snprintf(system_command, sizeof(system_command), "systemctl status ImageServer --no-pager -l");
        int result = execute_system_command(system_command);

        // Información adicional
        printf("\n=== Información Adicional ===\n");

        // Verificar proceso
        printf("Procesos:\n");
        execute_system_command("ps aux | grep '[i]mageserver' || echo 'No hay procesos imageserver ejecutándose'");

        // Verificar puerto
        printf("\nPuerto 1717:\n");
        execute_system_command("netstat -tlnp 2>/dev/null | grep ':1717' || echo 'Puerto 1717 no está en uso'");

        // Verificar archivo PID
        printf("\nArchivo PID:\n");
        if (access("/var/run/imageserver.pid", F_OK) == 0)
        {
            execute_system_command("echo -n 'PID: ' && cat /var/run/imageserver.pid");
        }
        else
        {
            printf("Archivo PID no existe\n");
        }

        return result;
    }

    else if (strcmp(command, "restart") == 0)
    {
        printf("Reiniciando ImageServer...\n");
        snprintf(system_command, sizeof(system_command), "systemctl restart ImageServer");
        int result = execute_system_command(system_command);
        if (result == 0)
        {
            printf("ImageServer reiniciado correctamente\n");
            // Esperar un poco y mostrar status
            sleep(2);
            snprintf(system_command, sizeof(system_command), "systemctl status ImageServer --no-pager -l");
            execute_system_command(system_command);
        }
        else
        {
            printf("Error reiniciando ImageServer\n");
        }
        return result;
    }

    else
    {
        printf("Error: Comando de servicio desconocido '%s'\n", command);
        printf("Comandos disponibles: start, stop, status, restart\n");
        return 1;
    }
}

int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    int test_server = 0;
    char *service_command = NULL;

    printf("=== ImageServer v1.0 ===\n");

    // Verificar argumentos de línea de comandos
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0)
        {
            daemon_mode = 1;
        }
        else if (strcmp(argv[i], "--test-server") == 0)
        {
            test_server = 1;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            show_help(argv[0]);
            return 0;
        }
        // NUEVOS COMANDOS DE SERVICIO
        else if (strcmp(argv[i], "start") == 0)
        {
            service_command = "start";
        }
        else if (strcmp(argv[i], "stop") == 0)
        {
            service_command = "stop";
        }
        else if (strcmp(argv[i], "status") == 0)
        {
            service_command = "status";
        }
        else if (strcmp(argv[i], "restart") == 0)
        {
            service_command = "restart";
        }
        else
        {
            printf("Error: Opción desconocida '%s'\n", argv[i]);
            printf("Usa --help para ver las opciones disponibles\n");
            return 1;
        }
    }

    // MANEJAR COMANDOS DE SERVICIO PRIMERO
    if (service_command)
    {
        // Verificar que se ejecute como root para comandos de servicio
        if (getuid() != 0)
        {
            printf("Error: Los comandos de servicio requieren permisos de root\n");
            printf("Ejecuta: sudo %s %s\n", argv[0], service_command);
            return 1;
        }

        return handle_service_command(service_command);
    }

    // Verificar que no se usen múltiples opciones
    if (daemon_mode && test_server)
    {
        printf("Error: No se pueden usar -d y --test-server al mismo tiempo\n");
        return 1;
    }

    // Cargar configuración
    printf("Cargando configuración...\n");
    if (!load_config(CONFIG_FILE_PATH))
    {
        printf("Usando configuración por defecto (archivo no encontrado)\n");
    }
    else
    {
        printf("Configuración cargada desde %s\n", CONFIG_FILE_PATH);
    }

    // Validar configuración
    if (!validate_config())
    {
        printf("Error: Configuración inválida, terminando...\n");
        return 1;
    }

    // Mostrar configuración
    printf("Configuración actual:\n");
    print_config();

    // Inicializar logger
    printf("Inicializando sistema de logs...\n");
    if (!init_logger(LOG_FILE_PATH, server_config.log_level))
    {
        printf("Error: No se pudo inicializar el logger\n");
        return 1;
    }

    printf("Sistema inicializado correctamente\n\n");

    if (daemon_mode)
    {
        // Modo daemon completo
        printf("Iniciando como daemon con servidor TCP...\n");
        printf("Puerto: %d\n", server_config.port);
        printf("Max conexiones: %d\n", server_config.max_connections);
        printf("Logs: %s\n\n", LOG_FILE_PATH);

        // Daemonizar
        if (!daemonize())
        {
            LOG_ERROR("Error al daemonizar");
            printf("Error al daemonizar proceso\n");
            close_logger();
            return 1;
        }

        // Configurar manejadores de señales
        setup_signal_handlers();

        LOG_INFO("=== ImageServer Daemon Iniciado ===");
        LOG_INFO("STB Image Library cargada correctamente");
        LOG_INFO("Puerto: %d, Max conexiones: %d, Max tamaño: %d MB",
                 server_config.port, server_config.max_connections, server_config.max_image_size_mb);

        // Ejecutar bucle principal con servidor
        daemon_main_loop();

        LOG_INFO("=== Daemon Finalizando ===");

        // Limpieza
        cleanup_daemon();
    }
    else if (test_server)
    {
        // Modo prueba de servidor
        printf("Iniciando modo de prueba del servidor TCP...\n\n");

        // Configurar señales básicas
        setup_signal_handlers();

        LOG_INFO("=== Modo Prueba de Servidor TCP ===");
        LOG_INFO("STB Image Library cargada correctamente");
        LOG_INFO("Puerto: %d, Max conexiones: %d", server_config.port, server_config.max_connections);

        // Ejecutar servidor en modo prueba
        test_mode_with_server();

        close_logger();
    }
    else
    {
        // Modo información básica
        printf("Ejecutando en modo de información\n\n");

        // Configurar señales básicas
        setup_signal_handlers();

        LOG_INFO("=== Modo Información Básica ===");
        LOG_INFO("STB Image Library cargada correctamente");
        LOG_INFO("Puerto configurado: %d", server_config.port);

        printf("Configuración actual:\n");
        printf("   Puerto: %d\n", server_config.port);
        printf("   Conexiones máximas: %d\n", server_config.max_connections);
        printf("   Tamaño máximo de imagen: %d MB\n", server_config.max_image_size_mb);
        printf("   Formatos soportados: %s\n", server_config.supported_formats);
        printf("   Directorio base: %s\n", server_config.image_base_path);

        printf("\nOpciones disponibles:\n");
        printf("   ./imageserver -d              # Ejecutar como daemon\n");
        printf("   ./imageserver --test-server   # Probar servidor TCP\n");
        printf("   ./imageserver --help          # Ayuda completa\n\n");

        // Simular actividad básica para probar el logger
        log_client_activity("127.0.0.1", "test.jpg", "test", "success");

        LOG_INFO("Prueba básica completada");
        printf("Prueba básica del sistema completada\n");
        printf("Revisa el log: %s\n", LOG_FILE_PATH);

        close_logger();
    }

    return 0;
}
