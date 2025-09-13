#include <dirent.h>
#include <sys/stat.h>
#include "config.h"
#include "logger.h"
#include "file_handler.h"
#include "image_processor.h"

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

    char *ext = strrchr(original_filename, '.');
    if (ext)
    {
        snprintf(temp_filename, size, "%s/temp_%ld_%d%s",
                 server_config.temp_path, now, pid, ext);
    }
    else
    {
        snprintf(temp_filename, size, "%s/temp_%ld_%d.tmp",
                 server_config.temp_path, now, pid);
    }
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
    LOG_DEBUG("Datos recibidos (primeros 100 bytes): %.100s", data);
    LOG_DEBUG("Datos recibidos (últimos 100 bytes): %.100s", data + data_len - 100);

    if (!data || !boundary || !upload_info)
    {
        LOG_ERROR("Parámetros inválidos en parse_multipart_data");
        return -1;
    }

    // Crear boundary delimitador completo
    char full_boundary[256];
    snprintf(full_boundary, sizeof(full_boundary), "--%s", boundary);

    LOG_DEBUG("Buscando boundary: %s", full_boundary);
    LOG_DEBUG("Total received data: %zu", data_len);

    // Buscar inicio del boundary
    const char *boundary_start = strstr(data, full_boundary);
    if (!boundary_start)
    {
        LOG_ERROR("No se encontró boundary en los datos");
        return -1;
    }

    // Avanzar después del boundary inicial
    const char *headers_start = boundary_start + strlen(full_boundary);

    // Saltar CRLF después del boundary
    if (headers_start[0] == '\r' && headers_start[1] == '\n')
    {
        headers_start += 2;
    }
    else if (headers_start[0] == '\n')
    {
        headers_start += 1;
    }

    // Verificar que no nos salimos de los datos
    if (headers_start >= data + data_len)
    {
        LOG_ERROR("Headers start fuera de los datos recibidos");
        return -1;
    }

    // Buscar el final de los headers (línea vacía)
    const char *headers_end = strstr(headers_start, "\r\n\r\n");
    if (!headers_end)
    {
        headers_end = strstr(headers_start, "\n\n");
        if (!headers_end)
        {
            LOG_ERROR("No se encontró final de headers");
            return -1;
        }
        headers_end += 2;
    }
    else
    {
        headers_end += 4;
    }

    // Verificar que headers_end está dentro de los datos
    if (headers_end >= data + data_len)
    {
        LOG_ERROR("Headers end fuera de los datos recibidos");
        return -1;
    }

    // Debug: posiciones importantes
    LOG_DEBUG("Headers start position: %zu", headers_start - data);
    LOG_DEBUG("Headers end position: %zu", headers_end - data);

    // Extraer headers
    size_t headers_len = headers_end - headers_start;
    char headers[2048];
    if (headers_len >= sizeof(headers))
    {
        LOG_ERROR("Headers demasiado largos");
        return -1;
    }

    strncpy(headers, headers_start, headers_len);
    headers[headers_len] = '\0';

    LOG_DEBUG("Headers extraídos: %s", headers);

    // Buscar Content-Disposition para obtener filename
    char *disposition_line = strstr(headers, "Content-Disposition:");
    if (disposition_line)
    {
        if (extract_filename_from_disposition(disposition_line, upload_info->original_filename,
                                              sizeof(upload_info->original_filename)))
        {
            LOG_INFO("Archivo detectado: %s", upload_info->original_filename);
        }
    }

    // El contenido del archivo empieza después de los headers
    const char *file_data_start = headers_end;

    // Buscar el boundary final
    char end_boundary[256];
    const char *file_data_end = NULL;

    // Intentar diferentes formatos de boundary
    const char *boundary_patterns[] = {
        "\r\n--%s--", // CRLF + --boundary--
        "\n--%s--",   // LF + --boundary--
        "\r\n--%s",   // CRLF + --boundary (siguiente parte)
        "\n--%s",     // LF + --boundary (siguiente parte)
        NULL};

    for (int i = 0; boundary_patterns[i] != NULL; i++)
    {
        snprintf(end_boundary, sizeof(end_boundary), boundary_patterns[i], boundary);
        file_data_end = strstr(file_data_start, end_boundary);

        if (file_data_end)
        {
            LOG_DEBUG("Boundary encontrado con patrón %d: %s", i, end_boundary);
            break;
        }
    }

    if (!file_data_end)
    {
        // Si no encontramos boundary, usar el final de los datos
        LOG_WARNING("No se encontró boundary final, usando final de datos");
        file_data_end = data + data_len;

        // Retroceder para quitar posibles \r\n finales
        while (file_data_end > file_data_start &&
               (*(file_data_end - 1) == '\r' || *(file_data_end - 1) == '\n'))
        {
            file_data_end--;
        }
    }

    // Debug: posiciones del archivo
    LOG_DEBUG("File data start position: %zu", file_data_start - data);
    LOG_DEBUG("File data end position: %zu", file_data_end - data);

    // Calcular tamaño del archivo
    upload_info->file_size = file_data_end - file_data_start;

    LOG_INFO("Tamaño de archivo extraído: %zu bytes", upload_info->file_size);

    // Verificar que el tamaño es válido
    if (file_data_end < file_data_start)
    {
        LOG_ERROR("Posiciones inválidas: end < start");
        return -1;
    }

    // Verificar tamaño máximo
    size_t max_size = server_config.max_image_size_mb * 1024 * 1024;
    if (upload_info->file_size > max_size)
    {
        LOG_ERROR("Archivo demasiado grande: %zu bytes (máximo: %zu MB)",
                  upload_info->file_size, server_config.max_image_size_mb);
        return -1;
    }

    // Guardar puntero a los datos del archivo
    upload_info->file_data = file_data_start;

    return 0;
}

// Guardar archivo en disco
int save_uploaded_file(const file_upload_info_t *upload_info, char *saved_filepath, size_t filepath_size)
{
    if (!upload_info || !upload_info->file_data || upload_info->file_size == 0)
    {
        LOG_ERROR("Información de archivo inválida");
        return -1;
    }

    if (!is_supported_format(upload_info->original_filename))
    {
        LOG_ERROR("Formato de archivo no soportado: %s", upload_info->original_filename);
        return -1;
    }

    // Generar nombre de archivo temporal
    char temp_filename[512];
    generate_temp_filename(temp_filename, sizeof(temp_filename), upload_info->original_filename);

    // Guardar archivo temporal
    FILE *file = fopen(temp_filename, "wb");
    if (!file)
    {
        LOG_ERROR("No se pudo crear archivo temporal: %s (%s)", temp_filename, strerror(errno));
        return -1;
    }

    size_t written = fwrite(upload_info->file_data, 1, upload_info->file_size, file);
    fclose(file);

    if (written != upload_info->file_size)
    {
        LOG_ERROR("Error escribiendo archivo: escrito %zu de %zu bytes", written, upload_info->file_size);
        unlink(temp_filename);
        return -1;
    }

    // Verificar que es imagen válida
    int width, height, channels;
    unsigned char *img_data = stbi_load(temp_filename, &width, &height, &channels, 0);
    if (!img_data)
    {
        LOG_ERROR("Archivo no es una imagen válida: %s", stbi_failure_reason());
        unlink(temp_filename);
        return -1;
    }
    stbi_image_free(img_data);

    // Procesar imagen completa
    processed_image_info_t result;
    memset(&result, 0, sizeof(result));
    // Establecer nombre original antes del procesamiento
    strncpy(result.original_filename, upload_info->original_filename, sizeof(result.original_filename) - 1);
    result.original_filename[sizeof(result.original_filename) - 1] = '\0';
    if (process_image_complete(temp_filename, &result) == 0)
    {
        LOG_INFO("Imagen procesada exitosamente: %s", upload_info->original_filename);
        strncpy(saved_filepath, result.equalized_path, filepath_size - 1);
        saved_filepath[filepath_size - 1] = '\0';
    }
    else
    {
        LOG_ERROR("Error procesando imagen: %s", upload_info->original_filename);
        strncpy(saved_filepath, temp_filename, filepath_size - 1);
        saved_filepath[filepath_size - 1] = '\0';
    }

    // Limpiar archivo temporal
    cleanup_temp_image(temp_filename);

    return 0;
}

// Procesar upload HTTP POST completo
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

    // Guardar archivo
    char saved_filepath[512];
    if (save_uploaded_file(&upload_info, saved_filepath, sizeof(saved_filepath)) != 0)
    {
        LOG_ERROR("Error guardando archivo");
        send_error_response(client_socket, 500, "Failed to save uploaded file");
        return -1;
    }

    // Enviar respuesta de éxito
    char response_body[512];
    snprintf(response_body, sizeof(response_body),
             "{\n"
             "  \"status\": \"success\",\n"
             "  \"message\": \"File uploaded successfully\",\n"
             "  \"filename\": \"%s\",\n"
             "  \"size\": %zu,\n"
             "  \"saved_path\": \"%s\"\n"
             "}",
             upload_info.original_filename, upload_info.file_size, saved_filepath);

    send_success_response(client_socket, "application/json", response_body);

    // Log de actividad del cliente
    log_client_activity(client_ip, upload_info.original_filename, "upload", "success");

    LOG_INFO("Upload completado: %s (%zu bytes) guardado como %s",
             upload_info.original_filename, upload_info.file_size, saved_filepath);

    return 0;
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
