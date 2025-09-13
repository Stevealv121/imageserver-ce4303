#include "image_processor.h"
#include "config.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>

// Función para calcular histograma de una imagen
void calculate_histogram(const unsigned char *image_data, int width, int height, int channels, int histogram[256])
{
    // Inicializar histograma
    for (int i = 0; i < 256; i++)
    {
        histogram[i] = 0;
    }

    // Calcular histograma basado en luminancia para imágenes a color
    // o usar el canal único para imágenes en escala de grises
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int pixel_index = (y * width + x) * channels;
            int luminance;

            if (channels >= 3)
            {
                // RGB: calcular luminancia usando fórmula estándar
                int r = image_data[pixel_index];
                int g = image_data[pixel_index + 1];
                int b = image_data[pixel_index + 2];
                luminance = (int)(0.299 * r + 0.587 * g + 0.114 * b);
            }
            else
            {
                // Escala de grises
                luminance = image_data[pixel_index];
            }

            // Asegurar que luminancia esté en rango válido
            if (luminance < 0)
                luminance = 0;
            if (luminance > 255)
                luminance = 255;

            histogram[luminance]++;
        }
    }

    LOG_DEBUG("Histograma calculado para imagen %dx%d con %d canales", width, height, channels);
}

// Función para calcular frecuencias acumuladas
void calculate_cumulative_histogram(const int histogram[256], int cumulative[256])
{
    cumulative[0] = histogram[0];

    for (int i = 1; i < 256; i++)
    {
        cumulative[i] = cumulative[i - 1] + histogram[i];
    }

    LOG_DEBUG("Frecuencias acumuladas calculadas");
}

// Función para ecualizar histograma
int equalize_histogram(unsigned char *image_data, int width, int height, int channels)
{
    LOG_INFO("Iniciando ecualización de histograma para imagen %dx%d", width, height);

    int histogram[256];
    int cumulative[256];
    int total_pixels = width * height;

    // Calcular histograma original
    calculate_histogram(image_data, width, height, channels, histogram);

    // Calcular frecuencias acumuladas
    calculate_cumulative_histogram(histogram, cumulative);

    // Crear tabla de mapeo para la ecualización
    unsigned char lookup_table[256];
    for (int i = 0; i < 256; i++)
    {
        // Aplicar la fórmula: nuevo_pixel = (frecuencia_acumulada * 255) / total_pixels
        lookup_table[i] = (unsigned char)((cumulative[i] * 255) / total_pixels);
    }

    // Aplicar la ecualización a cada pixel
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int pixel_index = (y * width + x) * channels;

            if (channels >= 3)
            {
                // Para imágenes RGB, ecualizar cada canal por separado
                image_data[pixel_index] = lookup_table[image_data[pixel_index]];         // R
                image_data[pixel_index + 1] = lookup_table[image_data[pixel_index + 1]]; // G
                image_data[pixel_index + 2] = lookup_table[image_data[pixel_index + 2]]; // B

                // Si hay canal alpha, mantenerlo sin cambios
                if (channels == 4)
                {
                    // Alpha channel permanece igual
                }
            }
            else
            {
                // Para escala de grises
                image_data[pixel_index] = lookup_table[image_data[pixel_index]];
            }
        }
    }

    LOG_INFO("Ecualización de histograma completada exitosamente");
    return 1;
}

// Función para determinar color predominante
color_category_t get_predominant_color(const unsigned char *image_data, int width, int height, int channels)
{
    if (channels < 3)
    {
        LOG_DEBUG("Imagen en escala de grises, clasificando como indefinida");
        return COLOR_UNDEFINED;
    }

    long long red_sum = 0, green_sum = 0, blue_sum = 0;
    int total_pixels = width * height;

    LOG_DEBUG("Analizando color predominante en imagen %dx%d", width, height);

    // Sumar valores de cada canal
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int pixel_index = (y * width + x) * channels;
            red_sum += image_data[pixel_index];
            green_sum += image_data[pixel_index + 1];
            blue_sum += image_data[pixel_index + 2];
        }
    }

    // Calcular promedios
    int red_avg = (int)(red_sum / total_pixels);
    int green_avg = (int)(green_sum / total_pixels);
    int blue_avg = (int)(blue_sum / total_pixels);

    LOG_DEBUG("Promedios de color: R=%d, G=%d, B=%d", red_avg, green_avg, blue_avg);

    // Determinar color predominante
    if (red_avg > green_avg && red_avg > blue_avg)
    {
        // Verificar que la diferencia sea significativa (al menos 20 puntos)
        if (red_avg - green_avg > 20 && red_avg - blue_avg > 20)
        {
            LOG_INFO("Color predominante detectado: ROJO (R=%d)", red_avg);
            return COLOR_RED;
        }
    }
    else if (green_avg > red_avg && green_avg > blue_avg)
    {
        if (green_avg - red_avg > 20 && green_avg - blue_avg > 20)
        {
            LOG_INFO("Color predominante detectado: VERDE (G=%d)", green_avg);
            return COLOR_GREEN;
        }
    }
    else if (blue_avg > red_avg && blue_avg > green_avg)
    {
        if (blue_avg - red_avg > 20 && blue_avg - green_avg > 20)
        {
            LOG_INFO("Color predominante detectado: AZUL (B=%d)", blue_avg);
            return COLOR_BLUE;
        }
    }

    LOG_INFO("No se detectó color predominante claro, clasificando como indefinida");
    return COLOR_UNDEFINED;
}

// Función para generar nombre de archivo procesado
void generate_processed_filename(const char *original_filename, const char *suffix, char *output_filename, size_t output_size)
{
    const char *ext = strrchr(original_filename, '.');
    if (ext)
    {
        size_t base_len = ext - original_filename;
        snprintf(output_filename, output_size, "%.*s_%s%s", (int)base_len, original_filename, suffix, ext);
    }
    else
    {
        snprintf(output_filename, output_size, "%s_%s.jpg", original_filename, suffix);
    }
}

// Función para obtener directorio de destino según color
const char *get_color_directory(color_category_t color)
{
    switch (color)
    {
    case COLOR_RED:
        return server_config.red_path;
    case COLOR_GREEN:
        return server_config.green_path;
    case COLOR_BLUE:
        return server_config.blue_path;
    default:
        return server_config.processed_path;
    }
}

// Función para procesar imagen completa
int process_image_complete(const char *input_filepath, const char *original_filename, processed_image_info_t *result)
{
    LOG_INFO("Iniciando procesamiento completo de imagen: %s", input_filepath);

    // Inicializar estructura de resultado
    memset(result, 0, sizeof(processed_image_info_t));
    strncpy(result->original_path, input_filepath, sizeof(result->original_path) - 1);

    // Guardar el nombre original en la estructura result
    if (original_filename && strlen(original_filename) > 0)
    {
        strncpy(result->original_filename, original_filename, sizeof(result->original_filename) - 1);
        result->original_filename[sizeof(result->original_filename) - 1] = '\0';
    }

    // Cargar imagen
    int width, height, channels;
    unsigned char *image_data = stbi_load(input_filepath, &width, &height, &channels, 0);
    if (!image_data)
    {
        LOG_ERROR("Error cargando imagen: %s (%s)", input_filepath, stbi_failure_reason());
        return -1;
    }

    LOG_INFO("Imagen cargada: %dx%d, %d canales", width, height, channels);

    // 1. Determinar color predominante ANTES de ecualizar
    color_category_t predominant_color = get_predominant_color(image_data, width, height, channels);
    result->predominant_color = predominant_color;

    // 2. Aplicar ecualización de histograma
    if (!equalize_histogram(image_data, width, height, channels))
    {
        LOG_ERROR("Error aplicando ecualización de histograma");
        stbi_image_free(image_data);
        return -1;
    }

    // 3. Generar nombres de archivos de salida
    // USAR EL NOMBRE ORIGINAL SI ESTÁ DISPONIBLE
    const char *filename_to_use;
    if (original_filename && strlen(original_filename) > 0)
    {
        filename_to_use = original_filename;
        LOG_DEBUG("Usando nombre original proporcionado: %s", filename_to_use);
    }
    else
    {
        // Fallback: extraer del path
        filename_to_use = strrchr(input_filepath, '/');
        if (!filename_to_use)
            filename_to_use = input_filepath;
        else
            filename_to_use++; // Saltar el '/'
        LOG_DEBUG("Usando nombre extraído del path: %s", filename_to_use);
    }

    // Archivo ecualizado (siempre se guarda en processed)
    char equalized_filename[256];
    generate_processed_filename(filename_to_use, "equalized", equalized_filename, sizeof(equalized_filename));
    snprintf(result->equalized_path, sizeof(result->equalized_path), "%s/%s", server_config.processed_path, equalized_filename);

    // 4. Guardar imagen ecualizada
    int save_result = 0;
    const char *ext = strrchr(filename_to_use, '.');
    if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".PNG") == 0))
    {
        save_result = stbi_write_png(result->equalized_path, width, height, channels, image_data, width * channels);
    }
    else
    {
        // Por defecto guardar como JPG
        save_result = stbi_write_jpg(result->equalized_path, width, height, channels, image_data, 90);
    }

    if (!save_result)
    {
        LOG_ERROR("Error guardando imagen ecualizada: %s", result->equalized_path);
        stbi_image_free(image_data);
        return -1;
    }

    LOG_INFO("Imagen ecualizada guardada: %s", result->equalized_path);

    // 5. Si tiene color predominante, guardar copia clasificada
    if (predominant_color != COLOR_UNDEFINED)
    {
        const char *color_dir = get_color_directory(predominant_color);
        const char *color_names[] = {"undefined", "red", "green", "blue"};

        char classified_filename[256];
        generate_processed_filename(filename_to_use, color_names[predominant_color], classified_filename, sizeof(classified_filename));
        snprintf(result->classified_path, sizeof(result->classified_path), "%s/%s", color_dir, classified_filename);

        // Guardar copia en directorio de color
        if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".PNG") == 0))
        {
            save_result = stbi_write_png(result->classified_path, width, height, channels, image_data, width * channels);
        }
        else
        {
            save_result = stbi_write_jpg(result->classified_path, width, height, channels, image_data, 90);
        }

        if (save_result)
        {
            LOG_INFO("Imagen clasificada guardada: %s", result->classified_path);
        }
        else
        {
            LOG_ERROR("Error guardando imagen clasificada: %s", result->classified_path);
        }
    }

    // 6. Limpiar memoria
    stbi_image_free(image_data);

    // 7. Llenar información del resultado
    result->width = width;
    result->height = height;
    result->channels = channels;
    result->processing_successful = 1;

    LOG_INFO("Procesamiento completo exitoso para: %s", input_filepath);
    return 0;
}

// Función para limpiar imagen temporal después del procesamiento
int cleanup_temp_image(const char *temp_filepath)
{
    if (unlink(temp_filepath) == 0)
    {
        LOG_DEBUG("Archivo temporal eliminado: %s", temp_filepath);
        return 1;
    }
    else
    {
        LOG_WARNING("No se pudo eliminar archivo temporal: %s (%s)", temp_filepath, strerror(errno));
        return 0;
    }
}
