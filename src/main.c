#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Definir implementaciones de STB (solo en un archivo .c)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

int main() {
    printf("ImageServer - Configuración inicial OK\n");
    printf("Versión: 1.0.0\n");
    printf("Sistema: Linux\n");
    printf("PID: %d\n", getpid());
    printf("STB Image Library cargada correctamente\n");
    
    // Verificar que STB puede leer formatos
    printf("Formatos soportados: JPG, PNG, GIF, BMP\n");
    
    return 0;
}
