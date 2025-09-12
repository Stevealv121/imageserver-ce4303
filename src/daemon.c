#include "daemon.h"
#include "logger.h"
#include "config.h"

// Variables globales del daemon
volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t reload_config = 0;
daemon_status_t daemon_status = DAEMON_STOPPED;

// Manejador de señales
void signal_handler(int sig) {
    switch(sig) {
        case SIGTERM:
        case SIGINT:
            LOG_INFO("Recibida señal de terminación (%d)", sig);
            keep_running = 0;
            daemon_status = DAEMON_STOPPING;
            break;
        case SIGHUP:
            LOG_INFO("Recibida señal HUP - Recargando configuración");
            reload_config = 1;
            break;
        case SIGPIPE:
            LOG_WARNING("Recibida señal SIGPIPE - Ignorando");
            break;
        default:
            LOG_WARNING("Recibida señal no manejada: %d", sig);
            break;
    }
}

// Configurar manejadores de señales
void setup_signal_handlers(void) {
    struct sigaction sa;
    
    // Configurar manejador
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    
    // Registrar señales
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
    
    LOG_INFO("Manejadores de señales configurados");
}

// Verificar si el daemon ya está ejecutándose
int check_if_running(void) {
    FILE *pid_file;
    pid_t pid;
    
    pid_file = fopen(PID_FILE, "r");
    if (!pid_file) {
        return 0; // No existe, no está corriendo
    }
    
    if (fscanf(pid_file, "%d", &pid) == 1) {
        fclose(pid_file);
        
        // Verificar si el proceso existe
        if (kill(pid, 0) == 0) {
            return pid; // Está corriendo
        } else {
            // El PID file existe pero el proceso no
            remove(PID_FILE);
            return 0;
        }
    }
    
    fclose(pid_file);
    return 0;
}

// Crear archivo PID
int create_pid_file(void) {
    FILE *pid_file;
    pid_t pid = getpid();
    
    pid_file = fopen(PID_FILE, "w");
    if (!pid_file) {
        LOG_ERROR("No se pudo crear archivo PID: %s", strerror(errno));
        return 0;
    }
    
    fprintf(pid_file, "%d\n", pid);
    fclose(pid_file);
    
    LOG_INFO("Archivo PID creado: %s (PID: %d)", PID_FILE, pid);
    return 1;
}

// Eliminar archivo PID
int remove_pid_file(void) {
    if (unlink(PID_FILE) == 0) {
        LOG_INFO("Archivo PID eliminado: %s", PID_FILE);
        return 1;
    } else {
        LOG_WARNING("No se pudo eliminar archivo PID: %s", strerror(errno));
        return 0;
    }
}

// Convertir en daemon
int daemonize(void) {
    pid_t pid, sid;
    
    LOG_INFO("Iniciando proceso de daemonización...");
    
    // Verificar si ya está corriendo
    pid_t running_pid = check_if_running();
    if (running_pid > 0) {
        LOG_ERROR("El daemon ya está ejecutándose con PID: %d", running_pid);
        return 0;
    }
    
    // Fork del proceso padre
    pid = fork();
    if (pid < 0) {
        LOG_ERROR("Error en fork(): %s", strerror(errno));
        return 0;
    }
    
    // Terminar proceso padre
    if (pid > 0) {
        printf("Daemon iniciado con PID: %d\n", pid);
        exit(EXIT_SUCCESS);
    }
    
    // Crear nueva sesión
    sid = setsid();
    if (sid < 0) {
        LOG_ERROR("Error en setsid(): %s", strerror(errno));
        return 0;
    }
    
    // Segundo fork para prevenir adquisición de terminal
    pid = fork();
    if (pid < 0) {
        LOG_ERROR("Error en segundo fork(): %s", strerror(errno));
        return 0;
    }
    
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    // Cambiar directorio de trabajo
    if (chdir("/") < 0) {
        LOG_ERROR("Error cambiando directorio: %s", strerror(errno));
        return 0;
    }
    
    // Establecer umask
    umask(0);
    
    // Cerrar descriptores de archivo estándar
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Redirigir a /dev/null
    open("/dev/null", O_RDONLY); // stdin
    open("/dev/null", O_WRONLY); // stdout
    open("/dev/null", O_WRONLY); // stderr
    
    // Crear archivo PID
    if (!create_pid_file()) {
        return 0;
    }
    
    daemon_status = DAEMON_RUNNING;
    LOG_INFO("Daemonización completada exitosamente");
    
    return 1;
}

// Limpieza del daemon
void cleanup_daemon(void) {
    LOG_INFO("Iniciando limpieza del daemon...");
    
    daemon_status = DAEMON_STOPPING;
    
    // Eliminar archivo PID
    remove_pid_file();
    
    // Cerrar logger
    close_logger();
    
    daemon_status = DAEMON_STOPPED;
}
