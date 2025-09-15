#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP   "158.23.56.208"  
#define SERVER_PORT 1717
#define BUFFER_SIZE 4096

void send_file(const char *filename) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    FILE *fp;
    size_t bytes;

    // Abrir archivo
    fp = fopen(filename, "rb");
    if (!fp) {
        perror("No se puede abrir el archivo");
        return;
    }

    // Crear socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error al crear socket");
        fclose(fp);
        return;
    }

    // Configurar dirección del servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Dirección IP inválida");
        fclose(fp);
        close(sock);
        return;
    }

    // Conectar
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Conexión fallida");
        fclose(fp);
        close(sock);
        return;
    }
    printf("Conectado al servidor %s:%d\n", SERVER_IP, SERVER_PORT);

    // Obtener tamaño del archivo
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Leer archivo a memoria
    char *filedata = malloc(filesize);
    if (!filedata) {
        perror("Error al reservar memoria");
        fclose(fp);
        close(sock);
        return;
    }
    fread(filedata, 1, filesize, fp);
    fclose(fp);

    // Definir boundary
    const char *boundary = "----BOUNDARY123";

    // Armar la parte inicial del multipart
    char preamble[BUFFER_SIZE];
    int preamble_len = snprintf(preamble, sizeof(preamble),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"image\"; filename=\"%s\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n",
        boundary, filename);

    // Armar la parte final del multipart
    char ending[BUFFER_SIZE];
    int ending_len = snprintf(ending, sizeof(ending),
        "\r\n--%s--\r\n", boundary);

    // Calcular Content-Length exacto
    long content_length = preamble_len + filesize + ending_len;

    // Armar el header HTTP
    char header[BUFFER_SIZE];
    int header_len = snprintf(header, sizeof(header),
        "POST /upload HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: multipart/form-data; boundary=%s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        SERVER_IP, SERVER_PORT, boundary, content_length);

    // Enviar cabecera HTTP
    send(sock, header, header_len, 0);

    // Enviar parte inicial
    send(sock, preamble, preamble_len, 0);

    // Enviar archivo
    send(sock, filedata, filesize, 0);
    free(filedata);

    // Enviar parte final
    send(sock, ending, ending_len, 0);

    printf("Imagen '%s' enviada (%ld bytes)\n", filename, filesize);

    // Leer respuesta del servidor
    while ((bytes = recv(sock, buffer, sizeof(buffer)-1, 0)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }
    printf("\n");

    close(sock);
}

int main() {
    char filename[256];
    while (1) {
        printf("\nIngrese el nombre de la imagen (o Exit para salir): ");
        scanf("%s", filename);
        if (strcmp(filename, "Exit") == 0)
            break;
        send_file(filename);
    }
    return 0;
}
