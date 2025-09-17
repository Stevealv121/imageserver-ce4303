#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SERVER_IP "158.23.56.208"
#define SERVER_PORT 1717
#define BUFFER_SIZE 4096

typedef struct {
    char filename[256];
} thread_arg_t;

void *send_file_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    const char *filename = targ->filename;

    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    FILE *fp;
    size_t bytes;

    // Abrir archivo
    fp = fopen(filename, "rb");
    if (!fp) {
        perror("No se puede abrir el archivo");
        free(targ);
        return NULL;
    }

    // Crear socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error al crear socket");
        fclose(fp);
        free(targ);
        return NULL;
    }

    // Configurar dirección del servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Dirección IP inválida");
        fclose(fp);
        close(sock);
        free(targ);
        return NULL;
    }

    // Conectar
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Conexión fallida");
        fclose(fp);
        close(sock);
        free(targ);
        return NULL;
    }
    printf("Conectado al servidor para '%s'\n", filename);

    // Leer archivo a memoria
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *filedata = malloc(filesize);
    if (!filedata) {
        perror("Error al reservar memoria");
        fclose(fp);
        close(sock);
        free(targ);
        return NULL;
    }
    fread(filedata, 1, filesize, fp);
    fclose(fp);

    // Preparar multipart/form-data
    const char *boundary = "----BOUNDARY123";
    char preamble[BUFFER_SIZE];
    int preamble_len = snprintf(preamble, sizeof(preamble),
                                "--%s\r\n"
                                "Content-Disposition: form-data; name=\"image\"; filename=\"%s\"\r\n"
                                "Content-Type: application/octet-stream\r\n\r\n",
                                boundary, filename);

    char ending[BUFFER_SIZE];
    int ending_len = snprintf(ending, sizeof(ending), "\r\n--%s--\r\n", boundary);

    long content_length = preamble_len + filesize + ending_len;

    char header[BUFFER_SIZE];
    int header_len = snprintf(header, sizeof(header),
                              "POST /upload HTTP/1.1\r\n"
                              "Host: %s:%d\r\n"
                              "Content-Type: multipart/form-data; boundary=%s\r\n"
                              "Content-Length: %ld\r\n"
                              "Connection: close\r\n\r\n",
                              SERVER_IP, SERVER_PORT, boundary, content_length);

    // Enviar todo
    send(sock, header, header_len, 0);
    send(sock, preamble, preamble_len, 0);
    send(sock, filedata, filesize, 0);
    send(sock, ending, ending_len, 0);
    free(filedata);

    printf("Imagen '%s' enviada (%ld bytes)\n", filename, filesize);

    // Leer respuesta del servidor
    while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        printf("[%s] %s", filename, buffer);
    }
    printf("\n");

    close(sock);
    free(targ);
    return NULL;
}

int main() {
    char input[1024];

    while (1) {
        printf("\nIngrese nombres de imágenes separados por espacio (o Exit para salir):\n> ");
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        // Quitar salto de línea
        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "Exit") == 0) {
            printf("Saliendo...\n");
            break;
        }

        // Contar y separar nombres de archivos
        char *token;
        char *filenames[100];
        int count = 0;
        token = strtok(input, " ");
        while (token && count < 100) {
            filenames[count++] = token;
            token = strtok(NULL, " ");
        }

        pthread_t threads[100];
        for (int i = 0; i < count; i++) {
            thread_arg_t *targ = malloc(sizeof(thread_arg_t));
            strcpy(targ->filename, filenames[i]);
            pthread_create(&threads[i], NULL, send_file_thread, targ);
        }

        // Esperar que todos terminen antes de pedir nuevas imágenes
        for (int i = 0; i < count; i++)
            pthread_join(threads[i], NULL);

        printf("Todas las imágenes fueron enviadas.\n");
    }

    return 0;
}

