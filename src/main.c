#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Definir implementaciones de STB (solo en un archivo .c)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "config.h"

int main() {
    printf("=== ImageServer v1.0 - Iniciando ===\n");
    printf("PID: %d\n", getpid());
    printf("STB Image Library cargada correctamente\n");
    
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
    
    // Mostrar configuración cargada
    print_config();
    
    // Información del sistema
    printf("--- Estado del Sistema ---\n");
    printf("Formatos soportados por STB: JPG, PNG, GIF, BMP\n");
    printf("Puerto configurado: %d\n", server_config.port);
    printf("Servidor listo para iniciar\n");
    
    printf("\n=== Prueba de configuración completada ===\n");
    return 0;
}
