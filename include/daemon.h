#ifndef DAEMON_H
#define DAEMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define PID_FILE "/run/imageserver.pid"
#define DAEMON_NAME "imageserver"

// Variables globales del daemon
extern volatile sig_atomic_t keep_running;
extern volatile sig_atomic_t reload_config;

// Funciones del daemon
int daemonize(void);
int create_pid_file(void);
int remove_pid_file(void);
void setup_signal_handlers(void);
void signal_handler(int sig);
void cleanup_daemon(void);
int check_if_running(void);

// Estados del daemon
typedef enum {
    DAEMON_STOPPED = 0,
    DAEMON_STARTING = 1,
    DAEMON_RUNNING = 2,
    DAEMON_STOPPING = 3
} daemon_status_t;

extern daemon_status_t daemon_status;

#endif // DAEMON_H
