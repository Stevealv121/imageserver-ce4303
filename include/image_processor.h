#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

// Definiciones de constantes
#define MAX_FILEPATH 512
#define MAX_FILENAME 256

// Enumeración para categorías de color
typedef enum
{
    COLOR_UNDEFINED = 0,
    COLOR_RED = 1,
    COLOR_GREEN = 2,
    COLOR_BLUE = 3
} color_category_t;

// Estructura para información de imagen procesada
typedef struct
{
    char original_path[MAX_FILEPATH];     // Ruta del archivo original
    char original_filename[MAX_FILENAME]; // Solo el nombre del archivo original
    char equalized_path[MAX_FILEPATH];    // Ruta del archivo ecualizado
    char classified_path[MAX_FILEPATH];   // Ruta del archivo clasificado por color

    int width;    // Ancho de la imagen
    int height;   // Alto de la imagen
    int channels; // Número de canales

    color_category_t predominant_color; // Color predominante detectado
    int processing_successful;          // 1 si el procesamiento fue exitoso

    time_t processing_time; // Timestamp del procesamiento
} processed_image_info_t;

// Funciones de histograma
/**
 * Calcula el histograma de una imagen
 * @param image_data: datos de la imagen
 * @param width: ancho de la imagen
 * @param height: alto de la imagen
 * @param channels: número de canales
 * @param histogram: array de 256 elementos para almacenar el histograma
 */
void calculate_histogram(const unsigned char *image_data, int width, int height, int channels, int histogram[256]);

/**
 * Calcula las frecuencias acumuladas del histograma
 * @param histogram: histograma original
 * @param cumulative: array para almacenar frecuencias acumuladas
 */
void calculate_cumulative_histogram(const int histogram[256], int cumulative[256]);

/**
 * Aplica ecualización de histograma a una imagen
 * @param image_data: datos de la imagen (se modifica in-place)
 * @param width: ancho de la imagen
 * @param height: alto de la imagen
 * @param channels: número de canales
 * @return: 1 si exitoso, 0 si error
 */
int equalize_histogram(unsigned char *image_data, int width, int height, int channels);

// Funciones de clasificación por color
/**
 * Determina el color predominante en una imagen
 * @param image_data: datos de la imagen
 * @param width: ancho de la imagen
 * @param height: alto de la imagen
 * @param channels: número de canales
 * @return: categoría de color predominante
 */
color_category_t get_predominant_color(const unsigned char *image_data, int width, int height, int channels);

/**
 * Obtiene el directorio de destino según la categoría de color
 * @param color: categoría de color
 * @return: ruta del directorio correspondiente
 */
const char *get_color_directory(color_category_t color);

// Funciones de utilidad
/**
 * Genera nombre de archivo procesado con sufijo
 * @param original_filename: nombre del archivo original
 * @param suffix: sufijo a agregar
 * @param output_filename: buffer para el nombre de salida
 * @param output_size: tamaño del buffer de salida
 */
void generate_processed_filename(const char *original_filename, const char *suffix,
                                 char *output_filename, size_t output_size);

// Función principal de procesamiento
/**
 * Procesa una imagen completa: ecualización y clasificación
 * @param input_filepath: ruta del archivo de entrada
 * @param result: estructura para almacenar información del resultado
 * @return: 0 si exitoso, -1 si error
 */
int process_image_complete(const char *input_filepath, processed_image_info_t *result);

/**
 * Limpia archivo temporal después del procesamiento
 * @param temp_filepath: ruta del archivo temporal
 * @return: 1 si exitoso, 0 si error
 */
int cleanup_temp_image(const char *temp_filepath);

#endif // IMAGE_PROCESSOR_H