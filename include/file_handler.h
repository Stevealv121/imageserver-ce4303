#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>

// Incluir STB para validación de imágenes
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

// Incluir headers del servidor para send_http_response
#include "server.h"

// Constantes
#define MAX_FILENAME_SIZE 256
#define MAX_PATH_LENGTH 512
#define MAX_BOUNDARY_SIZE 128
#define MAX_CONTENT_TYPE_SIZE 256
#define MAX_UPLOAD_SIZE (50 * 1024 * 1024)  // 50MB por defecto

// Formatos de imagen soportados
#define SUPPORTED_FORMATS "jpg,jpeg,png,gif"

// Códigos de error específicos para file handling
typedef enum {
    FILE_UPLOAD_SUCCESS = 0,
    FILE_UPLOAD_ERROR_INVALID_PARAMS = -1,
    FILE_UPLOAD_ERROR_UNSUPPORTED_FORMAT = -2,
    FILE_UPLOAD_ERROR_TOO_LARGE = -3,
    FILE_UPLOAD_ERROR_SAVE_FAILED = -4,
    FILE_UPLOAD_ERROR_INVALID_IMAGE = -5,
    FILE_UPLOAD_ERROR_PARSE_FAILED = -6,
    FILE_UPLOAD_ERROR_NO_BOUNDARY = -7,
    FILE_UPLOAD_ERROR_NO_CONTENT_TYPE = -8
} file_upload_error_t;

// Estructura para información de archivo subido
typedef struct {
    char original_filename[MAX_FILENAME_SIZE];   // Nombre original del archivo
    const char* file_data;                       // Puntero a los datos del archivo
    size_t file_size;                           // Tamaño del archivo en bytes
    char content_type[64];                      // Content-Type del archivo
    time_t upload_time;                         // Timestamp del upload
} file_upload_info_t;

// Estructura para estadísticas de archivos
typedef struct {
    int total_uploads;                          // Total de uploads procesados
    int successful_uploads;                     // Uploads exitosos
    int failed_uploads;                         // Uploads fallidos
    size_t total_bytes_processed;               // Total de bytes procesados
    time_t last_upload_time;                    // Último upload
    char last_uploaded_file[MAX_FILENAME_SIZE]; // Último archivo subido
} file_stats_t;

// Variables globales para estadísticas (opcional)
extern file_stats_t global_file_stats;

// =============================================================================
// FUNCIONES PRINCIPALES DE MANEJO DE ARCHIVOS
// =============================================================================

/**
 * Procesar una petición HTTP POST completa con archivo
 * @param client_socket Socket del cliente
 * @param request_data Datos completos de la petición HTTP
 * @param request_len Longitud de los datos de petición
 * @param client_ip IP del cliente (para logging)
 * @return 0 en éxito, código de error negativo en fallo
 */
int handle_file_upload_request(int client_socket, const char* request_data, 
                              size_t request_len, const char* client_ip);

/**
 * Parsear datos multipart/form-data y extraer información del archivo
 * @param data Datos multipart
 * @param data_len Longitud de los datos
 * @param boundary Boundary del multipart
 * @param upload_info Estructura donde guardar la información extraída
 * @return 0 en éxito, -1 en error
 */
int parse_multipart_data(const char* data, size_t data_len, const char* boundary, 
                        file_upload_info_t* upload_info);

/**
 * Guardar archivo subido en disco con validación
 * @param upload_info Información del archivo a guardar
 * @param saved_filepath Buffer donde guardar el path del archivo guardado
 * @param filepath_size Tamaño del buffer filepath
 * @return 0 en éxito, -1 en error
 */
int save_uploaded_file(const file_upload_info_t* upload_info, 
                       char* saved_filepath, size_t filepath_size);

// =============================================================================
// FUNCIONES DE VALIDACIÓN Y UTILIDADES
// =============================================================================

/**
 * Verificar si un formato de archivo es soportado
 * @param filename Nombre del archivo con extensión
 * @return 1 si es soportado, 0 si no
 */
int is_supported_format(const char* filename);

/**
 * Generar nombre único para archivo temporal
 * @param temp_filename Buffer para el nombre generado
 * @param size Tamaño del buffer
 * @param original_filename Nombre original del archivo
 */
void generate_temp_filename(char* temp_filename, size_t size, 
                           const char* original_filename);

/**
 * Obtener tamaño de un archivo abierto
 * @param file Puntero al archivo
 * @return Tamaño en bytes
 */
long get_file_size(FILE* file);

// =============================================================================
// FUNCIONES DE PARSING HTTP
// =============================================================================

/**
 * Extraer boundary del header Content-Type
 * @param content_type Header Content-Type completo
 * @param boundary Buffer donde guardar el boundary extraído
 * @param boundary_size Tamaño del buffer boundary
 * @return 1 en éxito, 0 en error
 */
int extract_boundary(const char* content_type, char* boundary, size_t boundary_size);

/**
 * Extraer filename del header Content-Disposition
 * @param disposition Header Content-Disposition
 * @param filename Buffer donde guardar el filename extraído
 * @param filename_size Tamaño del buffer filename
 * @return 1 en éxito, 0 en error
 */
int extract_filename_from_disposition(const char* disposition, 
                                     char* filename, size_t filename_size);

// =============================================================================
// FUNCIONES DE RESPUESTA HTTP
// =============================================================================

/**
 * Enviar respuesta de error HTTP con formato JSON
 * @param client_socket Socket del cliente
 * @param error_code Código de error HTTP
 * @param message Mensaje de error
 */
void send_error_response(int client_socket, int error_code, const char* message);

/**
 * Enviar respuesta de éxito HTTP
 * @param client_socket Socket del cliente
 * @param content_type Content-Type de la respuesta
 * @param body Cuerpo de la respuesta
 */
void send_success_response(int client_socket, const char* content_type, const char* body);

// =============================================================================
// FUNCIONES DE ESTADÍSTICAS (OPCIONALES)
// =============================================================================

/**
 * Inicializar estadísticas de archivos
 */
void init_file_stats(void);

/**
 * Actualizar estadísticas después de un upload
 * @param success 1 si fue exitoso, 0 si falló
 * @param bytes_processed Bytes procesados
 * @param filename Nombre del archivo procesado
 */
void update_file_stats(int success, size_t bytes_processed, const char* filename);

/**
 * Obtener estadísticas actuales
 * @return Puntero a estructura de estadísticas
 */
const file_stats_t* get_file_stats(void);

/**
 * Mostrar estadísticas en logs
 */
void log_file_stats(void);

// =============================================================================
// FUNCIONES DE LIMPIEZA Y MANTENIMIENTO
// =============================================================================

/**
 * Limpiar archivos temporales antiguos
 * @param max_age_hours Edad máxima en horas antes de eliminar
 * @return Número de archivos eliminados
 */
int cleanup_old_temp_files(int max_age_hours);

/**
 * Verificar espacio disponible en disco
 * @param path Ruta a verificar
 * @return Espacio libre en bytes, -1 en error
 */
long long get_available_disk_space(const char* path);

// =============================================================================
// MACROS DE UTILIDAD
// =============================================================================

// Macro para verificar si un puntero es válido
#define IS_VALID_PTR(ptr) ((ptr) != NULL)

// Macro para obtener la extensión de un archivo
#define GET_FILE_EXTENSION(filename) (strrchr((filename), '.'))

// Macro para verificar si una cadena está vacía
#define IS_EMPTY_STRING(str) (!(str) || (str)[0] == '\0')

// Macro para logging específico de file handler
#define LOG_FILE_ERROR(fmt, ...) LOG_ERROR("[FILE_HANDLER] " fmt, ##__VA_ARGS__)
#define LOG_FILE_INFO(fmt, ...) LOG_INFO("[FILE_HANDLER] " fmt, ##__VA_ARGS__)
#define LOG_FILE_DEBUG(fmt, ...) LOG_DEBUG("[FILE_HANDLER] " fmt, ##__VA_ARGS__)

#endif // FILE_HANDLER_H
