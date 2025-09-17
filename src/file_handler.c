#include <dirent.h>
#include <sys/stat.h>
#include "config.h"
#include "logger.h"
#include "file_handler.h"
#include "image_processor.h"
#include "priority_queue.h"

static int temp_file_counter = 0;

// Función auxiliar para obtener el tamaño de archivo
long get_file_size(FILE *file)
{
    long size;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    return size;
}

// Verificar si el formato de archivo es soportado
int is_supported_format(const char *filename)
{
    if (!filename)
        return 0;

    char *ext = strrchr(filename, '.');
    if (!ext)
        return 0;

    // Convertir a minúsculas para comparación
    char ext_lower[16];
    strncpy(ext_lower, ext + 1, sizeof(ext_lower) - 1);
    ext_lower[sizeof(ext_lower) - 1] = '\0';

    for (int i = 0; ext_lower[i]; i++)
    {
        ext_lower[i] = tolower(ext_lower[i]);
    }

    // Verificar formatos soportados
    return (strcmp(ext_lower, "jpg") == 0 ||
            strcmp(ext_lower, "jpeg") == 0 ||
            strcmp(ext_lower, "png") == 0 ||
            strcmp(ext_lower, "gif") == 0);
}

// Generar nombre único para archivo temporal
void generate_temp_filename(char *temp_filename, size_t size, const char *original_filename)
{
    time_t now = time(NULL);
    pid_t pid = getpid();

    // Usar contador atómico para evitar duplicados
    static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&counter_mutex);
    temp_file_counter++;
    int counter = temp_file_counter;
    pthread_mutex_unlock(&counter_mutex);

    char *ext = strrchr(original_filename, '.');
    if (ext)
    {
        snprintf(temp_filename, size, "%s/temp_%ld_%d_%d%s",
                 server_config.temp_path, now, pid, counter, ext);
    }
    else
    {
        snprintf(temp_filename, size, "%s/temp_%ld_%d_%d.tmp",
                 server_config.temp_path, now, pid, counter);
    }

    LOG_DEBUG("Generando archivo temporal: %s", temp_filename);
}

// Buscar boundary en Content-Type header
int extract_boundary(const char *content_type, char *boundary, size_t boundary_size)
{
    const char *boundary_start = strstr(content_type, "boundary=");
    if (!boundary_start)
    {
        return 0;
    }

    boundary_start += strlen("boundary=");

    // Copiar boundary hasta el final o hasta encontrar ';'
    size_t i = 0;
    while (boundary_start[i] && boundary_start[i] != ';' &&
           boundary_start[i] != '\r' && boundary_start[i] != '\n' &&
           i < boundary_size - 1)
    {
        boundary[i] = boundary_start[i];
        i++;
    }
    boundary[i] = '\0';

    return (i > 0) ? 1 : 0;
}

// Extraer filename del Content-Disposition header
int extract_filename_from_disposition(const char *disposition, char *filename, size_t filename_size)
{
    const char *filename_start = strstr(disposition, "filename=\"");
    if (!filename_start)
    {
        filename_start = strstr(disposition, "filename=");
        if (!filename_start)
            return 0;
        filename_start += strlen("filename=");
    }
    else
    {
        filename_start += strlen("filename=\"");
    }

    size_t i = 0;
    while (filename_start[i] && filename_start[i] != '"' &&
           filename_start[i] != '\r' && filename_start[i] != '\n' &&
           i < filename_size - 1)
    {
        filename[i] = filename_start[i];
        i++;
    }
    filename[i] = '\0';

    return (i > 0) ? 1 : 0;
}

int parse_multipart_data(const char *data, size_t data_len, const char *boundary,
                         file_upload_info_t *upload_info)
{

    if (!data || !boundary || !upload_info || data_len == 0)
    {
        LOG_FILE_ERROR("Parámetros inválidos para parsing multipart");
        return FILE_UPLOAD_ERROR_INVALID_PARAMS;
    }

    // Limpiar estructura
    memset(upload_info, 0, sizeof(file_upload_info_t));

    // Construir boundary completo
    char full_boundary[MAX_BOUNDARY_SIZE + 4];
    snprintf(full_boundary, sizeof(full_boundary), "--%s", boundary);

    // Construir boundary de cierre
    char closing_boundary[MAX_BOUNDARY_SIZE + 6];
    snprintf(closing_boundary, sizeof(closing_boundary), "--%s--", boundary);

    LOG_FILE_DEBUG("Buscando boundary: %s", full_boundary);
    LOG_FILE_DEBUG("Boundary de cierre: %s", closing_boundary);

    // Buscar el primer boundary
    const char *boundary_start = strstr(data, full_boundary);
    if (!boundary_start)
    {
        LOG_FILE_ERROR("No se encontró boundary inicial");
        return FILE_UPLOAD_ERROR_NO_BOUNDARY;
    }

    // Buscar el boundary de cierre
    const char *closing_boundary_start = strstr(data, closing_boundary);
    if (!closing_boundary_start)
    {
        // Si no hay boundary de cierre, buscar el siguiente boundary
        const char *next_boundary = strstr(boundary_start + strlen(full_boundary), full_boundary);
        if (next_boundary)
        {
            closing_boundary_start = next_boundary;
            LOG_FILE_DEBUG("Usando siguiente boundary como cierre");
        }
        else
        {
            // Usar final de datos menos margen de seguridad
            closing_boundary_start = data + data_len - 10;
            LOG_FILE_DEBUG("Usando final de datos como boundary de cierre");
        }
    }

    // Posición después del primer boundary + CRLF
    const char *headers_start = boundary_start + strlen(full_boundary);
    if (*headers_start == '\r')
        headers_start++;
    if (*headers_start == '\n')
        headers_start++;

    // Buscar fin de headers (doble CRLF)
    const char *headers_end = strstr(headers_start, "\r\n\r\n");
    if (!headers_end)
    {
        headers_end = strstr(headers_start, "\n\n");
        if (!headers_end)
        {
            LOG_FILE_ERROR("No se encontró fin de headers");
            return FILE_UPLOAD_ERROR_PARSE_FAILED;
        }
        headers_end += 2; // Skip \n\n
    }
    else
    {
        headers_end += 4; // Skip \r\n\r\n
    }

    // Extraer y procesar headers
    size_t headers_len = headers_end - headers_start;
    char *headers = malloc(headers_len + 1);
    if (!headers)
    {
        LOG_FILE_ERROR("Error de memoria allocando headers");
        return FILE_UPLOAD_ERROR_PARSE_FAILED;
    }

    strncpy(headers, headers_start, headers_len);
    headers[headers_len] = '\0';

    LOG_FILE_DEBUG("Headers extraídos (%zu bytes): %s", headers_len, headers);

    // Parsear Content-Disposition para obtener filename
    if (!extract_filename_from_disposition(headers,
                                           upload_info->original_filename,
                                           sizeof(upload_info->original_filename)))
    {
        LOG_FILE_ERROR("No se pudo extraer filename");
        free(headers);
        return FILE_UPLOAD_ERROR_PARSE_FAILED;
    }

    // Extraer Content-Type
    const char *content_type_line = strstr(headers, "Content-Type:");
    if (content_type_line)
    {
        const char *type_start = content_type_line + strlen("Content-Type:");
        while (*type_start == ' ' || *type_start == '\t')
            type_start++;

        const char *type_end = strchr(type_start, '\r');
        if (!type_end)
            type_end = strchr(type_start, '\n');
        if (!type_end)
            type_end = type_start + strlen(type_start);

        size_t type_len = type_end - type_start;
        if (type_len < sizeof(upload_info->content_type))
        {
            strncpy(upload_info->content_type, type_start, type_len);
            upload_info->content_type[type_len] = '\0';
        }
    }

    free(headers);

    // Calcular posición y tamaño de datos del archivo
    upload_info->file_data = headers_end;

    // Calcular fin de datos del archivo
    const char *file_data_end = closing_boundary_start;

    // Retroceder para eliminar CRLF antes del boundary de cierre
    while (file_data_end > upload_info->file_data &&
           (*(file_data_end - 1) == '\r' || *(file_data_end - 1) == '\n'))
    {
        file_data_end--;
    }

    upload_info->file_size = file_data_end - upload_info->file_data;
    upload_info->upload_time = time(NULL);

    LOG_FILE_INFO("Archivo detectado: %s", upload_info->original_filename);
    LOG_FILE_INFO("Content-Type: %s", upload_info->content_type);
    LOG_FILE_INFO("Tamaño de archivo: %zu bytes", upload_info->file_size);
    LOG_FILE_DEBUG("Datos del archivo desde posición %ld hasta %ld",
                   upload_info->file_data - data, file_data_end - data);

    // Validaciones básicas
    if (upload_info->file_size == 0)
    {
        LOG_FILE_ERROR("Archivo vacío detectado");
        return FILE_UPLOAD_ERROR_INVALID_IMAGE;
    }

    if (upload_info->file_size > MAX_UPLOAD_SIZE)
    {
        LOG_FILE_ERROR("Archivo demasiado grande: %zu bytes (máximo: %d)",
                       upload_info->file_size, MAX_UPLOAD_SIZE);
        return FILE_UPLOAD_ERROR_TOO_LARGE;
    }

    return FILE_UPLOAD_SUCCESS;
}

// Guardar archivo en disco
int save_uploaded_file(const file_upload_info_t *upload_info,
                       char *saved_filepath, size_t filepath_size)
{

    if (!upload_info || !saved_filepath)
    {
        return FILE_UPLOAD_ERROR_INVALID_PARAMS;
    }

    // Verificar formato soportado
    if (!is_supported_format(upload_info->original_filename))
    {
        LOG_FILE_ERROR("Formato no soportado: %s", upload_info->original_filename);
        return FILE_UPLOAD_ERROR_UNSUPPORTED_FORMAT;
    }

    // Validar datos de imagen antes de guardar
    if (!validate_image_data((const unsigned char *)upload_info->file_data, upload_info->file_size))
    {
        LOG_FILE_ERROR("Datos de imagen inválidos para: %s", upload_info->original_filename);
        return FILE_UPLOAD_ERROR_INVALID_IMAGE;
    }

    // Generar nombre temporal único
    char temp_filename[MAX_FILENAME_SIZE];
    generate_temp_filename(temp_filename, sizeof(temp_filename), upload_info->original_filename);

    // Construir ruta completa
    snprintf(saved_filepath, filepath_size, "%s/%s",
             server_config.temp_path, temp_filename);

    LOG_FILE_DEBUG("Guardando archivo en: %s", saved_filepath);

    // Abrir archivo para escritura binaria
    FILE *file = fopen(saved_filepath, "wb");
    if (!file)
    {
        LOG_FILE_ERROR("No se pudo crear archivo: %s - %s", saved_filepath, strerror(errno));
        return FILE_UPLOAD_ERROR_SAVE_FAILED;
    }

    // Escribir datos
    size_t bytes_written = fwrite(upload_info->file_data, 1, upload_info->file_size, file);
    fclose(file);

    if (bytes_written != upload_info->file_size)
    {
        LOG_FILE_ERROR("Error escribiendo archivo: escritos %zu de %zu bytes",
                       bytes_written, upload_info->file_size);
        unlink(saved_filepath); // Eliminar archivo parcial
        return FILE_UPLOAD_ERROR_SAVE_FAILED;
    }

    LOG_FILE_INFO("Archivo guardado exitosamente: %s (%zu bytes)",
                  saved_filepath, upload_info->file_size);

    return FILE_UPLOAD_SUCCESS;
}

// Procesar upload HTTP POST completo con cola de prioridad
int handle_file_upload_request(int client_socket, const char *request_data, size_t request_len,
                               const char *client_ip)
{
    LOG_INFO("Procesando upload de archivo desde %s", client_ip);

    // Buscar Content-Type header
    const char *content_type_start = strstr(request_data, "Content-Type:");
    if (!content_type_start)
    {
        LOG_ERROR("No se encontró Content-Type header");
        send_error_response(client_socket, 400, "Missing Content-Type header");
        return -1;
    }

    // Extraer línea completa de Content-Type
    const char *content_type_end = strstr(content_type_start, "\r\n");
    if (!content_type_end)
    {
        content_type_end = strstr(content_type_start, "\n");
    }

    if (!content_type_end)
    {
        LOG_ERROR("Content-Type header malformado");
        send_error_response(client_socket, 400, "Malformed Content-Type header");
        return -1;
    }

    size_t content_type_len = content_type_end - content_type_start;
    char content_type[256];
    if (content_type_len >= sizeof(content_type))
    {
        LOG_ERROR("Content-Type header demasiado largo");
        send_error_response(client_socket, 400, "Content-Type header too long");
        return -1;
    }

    strncpy(content_type, content_type_start, content_type_len);
    content_type[content_type_len] = '\0';

    LOG_DEBUG("Content-Type: %s", content_type);

    // Mover puntero al valor después de "Content-Type:"
    const char *value_start = content_type + strlen("Content-Type:");
    while (*value_start == ' ')
        value_start++;

    // Verificar que es multipart/form-data
    if (!strstr(content_type, "multipart/form-data"))
    {
        LOG_ERROR("Content-Type no es multipart/form-data");
        send_error_response(client_socket, 400, "Expected multipart/form-data");
        return -1;
    }

    // Extraer boundary
    char boundary[128];
    if (!extract_boundary(content_type, boundary, sizeof(boundary)))
    {
        LOG_ERROR("No se pudo extraer boundary del Content-Type");
        send_error_response(client_socket, 400, "Invalid boundary in Content-Type");
        return -1;
    }

    LOG_DEBUG("Boundary extraído: %s", boundary);

    // Buscar inicio del cuerpo del mensaje (después de headers HTTP)
    const char *body_start = strstr(request_data, "\r\n\r\n");
    if (!body_start)
    {
        body_start = strstr(request_data, "\n\n");
        if (!body_start)
        {
            LOG_ERROR("No se encontró separador de headers HTTP");
            send_error_response(client_socket, 400, "Malformed HTTP request");
            return -1;
        }
        body_start += 2;
    }
    else
    {
        body_start += 4;
    }

    size_t body_len = request_len - (body_start - request_data);

    // Parsear datos multipart
    file_upload_info_t upload_info;
    memset(&upload_info, 0, sizeof(upload_info));

    if (parse_multipart_data(body_start, body_len, boundary, &upload_info) != 0)
    {
        LOG_ERROR("Error parseando datos multipart");
        send_error_response(client_socket, 400, "Failed to parse multipart data");
        return -1;
    }

    // Verificar formato soportado
    if (!is_supported_format(upload_info.original_filename))
    {
        LOG_ERROR("Formato de archivo no soportado: %s", upload_info.original_filename);
        send_error_response(client_socket, 400, "Unsupported file format");
        return -1;
    }

    // Verificar tamaño máximo
    size_t max_size = server_config.max_image_size_mb * 1024 * 1024;
    if (upload_info.file_size > max_size)
    {
        LOG_ERROR("Archivo demasiado grande: %zu bytes (máximo: %zu MB)",
                  upload_info.file_size, server_config.max_image_size_mb);
        send_error_response(client_socket, 413, "File too large");
        return -1;
    }

    // Generar nombre de archivo temporal
    char temp_filename[512];
    generate_temp_filename(temp_filename, sizeof(temp_filename), upload_info.original_filename);

    // Guardar archivo temporal
    FILE *file = fopen(temp_filename, "wb");
    if (!file)
    {
        LOG_ERROR("No se pudo crear archivo temporal: %s (%s)", temp_filename, strerror(errno));
        send_error_response(client_socket, 500, "Failed to create temporary file");
        return -1;
    }

    size_t written = fwrite(upload_info.file_data, 1, upload_info.file_size, file);
    fclose(file);

    if (written != upload_info.file_size)
    {
        LOG_ERROR("Error escribiendo archivo: escrito %zu de %zu bytes", written, upload_info.file_size);
        unlink(temp_filename);
        send_error_response(client_socket, 500, "Failed to write temporary file");
        return -1;
    }

    // Verificar que es imagen válida
    int width, height, channels;
    unsigned char *img_data = stbi_load(temp_filename, &width, &height, &channels, 0);
    if (!img_data)
    {
        LOG_ERROR("Archivo no es una imagen válida: %s", stbi_failure_reason());
        unlink(temp_filename);
        send_error_response(client_socket, 400, "Invalid image file");
        return -1;
    }
    stbi_image_free(img_data);

    // Encolar archivo para procesamiento en lugar de procesarlo directamente
    if (enqueue_file_for_processing(&upload_info, temp_filename, client_ip, client_socket) != 0)
    {
        LOG_ERROR("Error encolando archivo para procesamiento");
        unlink(temp_filename);
        send_error_response(client_socket, 500, "Failed to queue file for processing");
        return -1;
    }

    log_client_activity(client_ip, upload_info.original_filename, "upload", "queued");

    LOG_INFO("Upload encolado: %s (%zu bytes) desde %s - Posición en cola: %d",
             upload_info.original_filename, upload_info.file_size, client_ip, get_queue_size());

    return 0;
}

int validate_image_data(const unsigned char *data, size_t size)
{
    int width, height, channels;

    // Intentar cargar la imagen con stb_image
    unsigned char *image_data = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 0);

    if (!image_data)
    {
        const char *error = stbi_failure_reason();
        LOG_FILE_ERROR("STB no pudo cargar imagen: %s", error ? error : "Error desconocido");
        return 0;
    }

    // Validar dimensiones razonables
    if (width <= 0 || height <= 0 || width > 10000 || height > 10000)
    {
        LOG_FILE_ERROR("Dimensiones de imagen inválidas: %dx%d", width, height);
        stbi_image_free(image_data);
        return 0;
    }

    if (channels < 1 || channels > 4)
    {
        LOG_FILE_ERROR("Número de canales inválido: %d", channels);
        stbi_image_free(image_data);
        return 0;
    }

    LOG_FILE_DEBUG("Imagen validada: %dx%d, %d canales", width, height, channels);
    stbi_image_free(image_data);
    return 1;
}

// Limpiar archivos temporales antiguos
int cleanup_old_temp_files(int max_age_hours)
{
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char filepath[512];
    time_t current_time = time(NULL);
    time_t max_age_seconds = max_age_hours * 3600;
    int files_deleted = 0;

    LOG_DEBUG("Iniciando limpieza de archivos temporales (edad máxima: %d horas)", max_age_hours);

    // Abrir directorio temporal
    dir = opendir(server_config.temp_path);
    if (dir == NULL)
    {
        LOG_ERROR("No se pudo abrir directorio temporal: %s (%s)",
                  server_config.temp_path, strerror(errno));
        return -1;
    }

    // Leer archivos del directorio
    while ((entry = readdir(dir)) != NULL)
    {
        // Saltar "." y ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        // Solo procesar archivos que empiecen con "temp_"
        if (strncmp(entry->d_name, "temp_", 5) != 0)
        {
            continue;
        }

        // Construir path completo
        snprintf(filepath, sizeof(filepath), "%s/%s", server_config.temp_path, entry->d_name);

        // Obtener información del archivo
        if (stat(filepath, &file_stat) == -1)
        {
            LOG_WARNING("No se pudo obtener información del archivo: %s (%s)",
                        filepath, strerror(errno));
            continue;
        }

        // Verificar si es un archivo regular
        if (!S_ISREG(file_stat.st_mode))
        {
            continue;
        }

        // Verificar edad del archivo
        time_t file_age = current_time - file_stat.st_mtime;
        if (file_age > max_age_seconds)
        {
            // Eliminar archivo antiguo
            if (unlink(filepath) == 0)
            {
                files_deleted++;
                LOG_INFO("Archivo temporal eliminado: %s (edad: %.1f horas)",
                         entry->d_name, (double)file_age / 3600.0);
            }
            else
            {
                LOG_ERROR("Error eliminando archivo temporal: %s (%s)",
                          filepath, strerror(errno));
            }
        }
    }

    closedir(dir);

    if (files_deleted > 0)
    {
        LOG_INFO("Limpieza completada: %d archivos temporales eliminados", files_deleted);
    }
    else
    {
        LOG_DEBUG("Limpieza completada: no se encontraron archivos antiguos para eliminar");
    }

    return files_deleted;
}
