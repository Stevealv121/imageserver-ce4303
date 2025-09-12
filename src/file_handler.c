#include "file_handler.h"

// Función auxiliar para obtener el tamaño de archivo
long get_file_size(FILE* file) {
    long size;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    return size;
}

// Verificar si el formato de archivo es soportado
int is_supported_format(const char* filename) {
    if (!filename) return 0;
    
    char* ext = strrchr(filename, '.');
    if (!ext) return 0;
    
    // Convertir a minúsculas para comparación
    char ext_lower[16];
    strncpy(ext_lower, ext + 1, sizeof(ext_lower) - 1);
    ext_lower[sizeof(ext_lower) - 1] = '\0';
    
    for (int i = 0; ext_lower[i]; i++) {
        ext_lower[i] = tolower(ext_lower[i]);
    }
    
    // Verificar formatos soportados
    return (strcmp(ext_lower, "jpg") == 0 || 
            strcmp(ext_lower, "jpeg") == 0 ||
            strcmp(ext_lower, "png") == 0 ||
            strcmp(ext_lower, "gif") == 0);
}

// Generar nombre único para archivo temporal
void generate_temp_filename(char* temp_filename, size_t size, const char* original_filename) {
    time_t now = time(NULL);
    pid_t pid = getpid();
    
    char* ext = strrchr(original_filename, '.');
    if (ext) {
        snprintf(temp_filename, size, "%s/temp_%ld_%d%s", 
                 server_config.temp_dir, now, pid, ext);
    } else {
        snprintf(temp_filename, size, "%s/temp_%ld_%d.tmp", 
                 server_config.temp_dir, now, pid);
    }
}

// Buscar boundary en Content-Type header
int extract_boundary(const char* content_type, char* boundary, size_t boundary_size) {
    const char* boundary_start = strstr(content_type, "boundary=");
    if (!boundary_start) {
        return 0;
    }
    
    boundary_start += strlen("boundary=");
    
    // Copiar boundary hasta el final o hasta encontrar ';'
    size_t i = 0;
    while (boundary_start[i] && boundary_start[i] != ';' && 
           boundary_start[i] != '\r' && boundary_start[i] != '\n' && 
           i < boundary_size - 1) {
        boundary[i] = boundary_start[i];
        i++;
    }
    boundary[i] = '\0';
    
    return (i > 0) ? 1 : 0;
}

// Extraer filename del Content-Disposition header
int extract_filename_from_disposition(const char* disposition, char* filename, size_t filename_size) {
    const char* filename_start = strstr(disposition, "filename=\"");
    if (!filename_start) {
        filename_start = strstr(disposition, "filename=");
        if (!filename_start) return 0;
        filename_start += strlen("filename=");
    } else {
        filename_start += strlen("filename=\"");
    }
    
    size_t i = 0;
    while (filename_start[i] && filename_start[i] != '"' && 
           filename_start[i] != '\r' && filename_start[i] != '\n' &&
           i < filename_size - 1) {
        filename[i] = filename_start[i];
        i++;
    }
    filename[i] = '\0';
    
    return (i > 0) ? 1 : 0;
}

// Parsear multipart/form-data y extraer archivo
int parse_multipart_data(const char* data, size_t data_len, const char* boundary, 
                        file_upload_info_t* upload_info) {
    (void)data_len; // Silenciar warning de parámetro no usado
    
    if (!data || !boundary || !upload_info) {
        LOG_ERROR("Parámetros inválidos en parse_multipart_data");
        return -1;
    }
    
    // Crear boundary delimitador completo
    char full_boundary[256];
    snprintf(full_boundary, sizeof(full_boundary), "--%s", boundary);
    
    LOG_DEBUG("Buscando boundary: %s", full_boundary);
    
    // Buscar inicio del boundary
    const char* boundary_start = strstr(data, full_boundary);
    if (!boundary_start) {
        LOG_ERROR("No se encontró boundary en los datos");
        return -1;
    }
    
    // Avanzar después del boundary inicial
    const char* headers_start = boundary_start + strlen(full_boundary);
    
    // Saltar CRLF después del boundary
    if (headers_start[0] == '\r' && headers_start[1] == '\n') {
        headers_start += 2;
    } else if (headers_start[0] == '\n') {
        headers_start += 1;
    }
    
    // Buscar el final de los headers (línea vacía)
    const char* headers_end = strstr(headers_start, "\r\n\r\n");
    if (!headers_end) {
        headers_end = strstr(headers_start, "\n\n");
        if (!headers_end) {
            LOG_ERROR("No se encontró final de headers");
            return -1;
        }
        headers_end += 2;
    } else {
        headers_end += 4;
    }
    
    // Extraer headers
    size_t headers_len = headers_end - headers_start;
    char headers[2048];
    if (headers_len >= sizeof(headers)) {
        LOG_ERROR("Headers demasiado largos");
        return -1;
    }
    
    strncpy(headers, headers_start, headers_len);
    headers[headers_len] = '\0';
    
    LOG_DEBUG("Headers extraídos: %s", headers);
    
    // Buscar Content-Disposition para obtener filename
    char* disposition_line = strstr(headers, "Content-Disposition:");
    if (disposition_line) {
        if (extract_filename_from_disposition(disposition_line, upload_info->original_filename, 
                                            sizeof(upload_info->original_filename))) {
            LOG_INFO("Archivo detectado: %s", upload_info->original_filename);
        }
    }
    
    // El contenido del archivo empieza después de los headers
    const char* file_data_start = headers_end;
    
    // Buscar el boundary final
    char end_boundary[256];
    snprintf(end_boundary, sizeof(end_boundary), "\r\n--%s", boundary);
    
    const char* file_data_end = strstr(file_data_start, end_boundary);
    if (!file_data_end) {
        // Intentar sin \r\n
        snprintf(end_boundary, sizeof(end_boundary), "\n--%s", boundary);
        file_data_end = strstr(file_data_start, end_boundary);
        if (!file_data_end) {
            LOG_ERROR("No se encontró boundary final");
            return -1;
        }
    }
    
    // Calcular tamaño del archivo
    upload_info->file_size = file_data_end - file_data_start;
    
    LOG_INFO("Tamaño de archivo extraído: %zu bytes", upload_info->file_size);
    
    // Verificar tamaño máximo
    size_t max_size = server_config.max_image_size_mb * 1024 * 1024;
    if (upload_info->file_size > max_size) {
        LOG_ERROR("Archivo demasiado grande: %zu bytes (máximo: %zu MB)", 
                  upload_info->file_size, server_config.max_image_size_mb);
        return -1;
    }
    
    // Guardar puntero a los datos del archivo
    upload_info->file_data = file_data_start;
    
    return 0;
}

// Guardar archivo en disco
int save_uploaded_file(const file_upload_info_t* upload_info, char* saved_filepath, size_t filepath_size) {
    if (!upload_info || !upload_info->file_data || upload_info->file_size == 0) {
        LOG_ERROR("Información de archivo inválida");
        return -1;
    }
    
    // Verificar formato soportado
    if (!is_supported_format(upload_info->original_filename)) {
        LOG_ERROR("Formato de archivo no soportado: %s", upload_info->original_filename);
        return -1;
    }
    
    // Generar nombre de archivo temporal
    char temp_filename[512];
    generate_temp_filename(temp_filename, sizeof(temp_filename), upload_info->original_filename);
    
    // Abrir archivo para escritura
    FILE* file = fopen(temp_filename, "wb");
    if (!file) {
        LOG_ERROR("No se pudo crear archivo temporal: %s (%s)", temp_filename, strerror(errno));
        return -1;
    }
    
    // Escribir datos del archivo
    size_t written = fwrite(upload_info->file_data, 1, upload_info->file_size, file);
    fclose(file);
    
    if (written != upload_info->file_size) {
        LOG_ERROR("Error escribiendo archivo: escrito %zu de %zu bytes", written, upload_info->file_size);
        unlink(temp_filename);
        return -1;
    }
    
    // Verificar que el archivo se puede cargar con STB
    int width, height, channels;
    unsigned char* img_data = stbi_load(temp_filename, &width, &height, &channels, 0);
    if (!img_data) {
        LOG_ERROR("Archivo no es una imagen válida: %s", stbi_failure_reason());
        unlink(temp_filename);
        return -1;
    }
    
    stbi_image_free(img_data);
    
    LOG_INFO("Archivo guardado exitosamente: %s (%dx%d, %d canales)", 
             temp_filename, width, height, channels);
    
    // Copiar path del archivo guardado
    strncpy(saved_filepath, temp_filename, filepath_size - 1);
    saved_filepath[filepath_size - 1] = '\0';
    
    return 0;
}

// Procesar upload HTTP POST completo
int handle_file_upload_request(int client_socket, const char* request_data, size_t request_len, 
                              const char* client_ip) {
    LOG_INFO("Procesando upload de archivo desde %s", client_ip);
    
    // Buscar Content-Type header
    const char* content_type_start = strstr(request_data, "Content-Type:");
    if (!content_type_start) {
        LOG_ERROR("No se encontró Content-Type header");
        send_error_response(client_socket, 400, "Missing Content-Type header");
        return -1;
    }
    
    // Extraer línea completa de Content-Type
    const char* content_type_end = strstr(content_type_start, "\r\n");
    if (!content_type_end) {
        content_type_end = strstr(content_type_start, "\n");
    }
    
    if (!content_type_end) {
        LOG_ERROR("Content-Type header malformado");
        send_error_response(client_socket, 400, "Malformed Content-Type header");
        return -1;
    }
    
    size_t content_type_len = content_type_end - content_type_start;
    char content_type[256];
    if (content_type_len >= sizeof(content_type)) {
        LOG_ERROR("Content-Type header demasiado largo");
        send_error_response(client_socket, 400, "Content-Type header too long");
        return -1;
    }
    
    strncpy(content_type, content_type_start, content_type_len);
    content_type[content_type_len] = '\0';
    
    LOG_DEBUG("Content-Type: %s", content_type);
    
    // Verificar que es multipart/form-data
    if (!strstr(content_type, "multipart/form-data")) {
        LOG_ERROR("Content-Type no es multipart/form-data");
        send_error_response(client_socket, 400, "Expected multipart/form-data");
        return -1;
    }
    
    // Extraer boundary
    char boundary[128];
    if (!extract_boundary(content_type, boundary, sizeof(boundary))) {
        LOG_ERROR("No se pudo extraer boundary del Content-Type");
        send_error_response(client_socket, 400, "Invalid boundary in Content-Type");
        return -1;
    }
    
    LOG_DEBUG("Boundary extraído: %s", boundary);
    
    // Buscar inicio del cuerpo del mensaje (después de headers HTTP)
    const char* body_start = strstr(request_data, "\r\n\r\n");
    if (!body_start) {
        body_start = strstr(request_data, "\n\n");
        if (!body_start) {
            LOG_ERROR("No se encontró separador de headers HTTP");
            send_error_response(client_socket, 400, "Malformed HTTP request");
            return -1;
        }
        body_start += 2;
    } else {
        body_start += 4;
    }
    
    size_t body_len = request_len - (body_start - request_data);
    
    // Parsear datos multipart
    file_upload_info_t upload_info;
    memset(&upload_info, 0, sizeof(upload_info));
    
    if (parse_multipart_data(body_start, body_len, boundary, &upload_info) != 0) {
        LOG_ERROR("Error parseando datos multipart");
        send_error_response(client_socket, 400, "Failed to parse multipart data");
        return -1;
    }
    
    // Guardar archivo
    char saved_filepath[512];
    if (save_uploaded_file(&upload_info, saved_filepath, sizeof(saved_filepath)) != 0) {
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
