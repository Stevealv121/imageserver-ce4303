# Makefile para ImageServer
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -D_GNU_SOURCE -O2
INCLUDES = -I./include
LIBS = -lpthread -lm

# Directorios
SRC_DIR = src
BIN_DIR = bin
OBJ_DIR = obj

# Archivos fuente
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
TARGET = $(BIN_DIR)/imageserver

# Directorio de instalación
INSTALL_DIR = /opt/imageserver
SERVICE_DIR = /etc/systemd/system

.PHONY: all clean install uninstall setup test download-stb

# Regla principal
all: setup $(TARGET)

# Crear directorios necesarios
setup:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p include/stb

# Descargar STB si no existe
download-stb:
	@if [ ! -f include/stb/stb_image.h ]; then \
		echo "Descargando STB Image..."; \
		wget -q -O include/stb/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h; \
	fi
	@if [ ! -f include/stb/stb_image_write.h ]; then \
		echo "Descargando STB Image Write..."; \
		wget -q -O include/stb/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h; \
	fi
	@echo "STB libraries ready"

# Compilar el ejecutable
$(TARGET): $(OBJECTS)
	@echo "Enlazando $(TARGET)..."
	$(CC) $(OBJECTS) -o $@ $(LIBS)
	@echo "Compilación completada."

# Compilar archivos objeto
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compilando $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Instalar el servicio
install: all
	@echo "Instalando ImageServer..."
	sudo cp $(TARGET) $(INSTALL_DIR)/bin/
	sudo chmod +x $(INSTALL_DIR)/bin/imageserver
	@echo "Instalación completada."

# Desinstalar
uninstall:
	@echo "Desinstalando ImageServer..."
	sudo systemctl stop imageserver 2>/dev/null || true
	sudo systemctl disable imageserver 2>/dev/null || true
	sudo rm -f $(SERVICE_DIR)/imageserver.service
	sudo rm -f $(INSTALL_DIR)/bin/imageserver
	sudo systemctl daemon-reload
	@echo "Desinstalación completada."

# Limpiar archivos compilados
clean:
	@echo "Limpiando archivos temporales..."
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Limpieza completada."

# Limpiar todo incluyendo STB
clean-all: clean
	@echo "Limpiando STB libraries..."
	rm -rf include/stb
	@echo "Limpieza completa terminada."

# Test básico
test:
	@echo "Verificando configuración..."
	@echo "GCC: $$(gcc --version | head -n1)"
	@echo "STB Image: $$(test -f include/stb/stb_image.h && echo 'OK' || echo 'Falta')"
	@echo "STB Write: $$(test -f include/stb/stb_image_write.h && echo 'OK' || echo 'Falta')"
	@echo "Configuración: $$(test -f /etc/server/config.conf && echo 'OK' || echo 'Falta config.conf')"
	@echo "Directorios: $$(test -d /var/imageserver && echo 'OK' || echo 'Faltan directorios')"

# Mostrar ayuda
help:
	@echo "Comandos disponibles:"
	@echo "  make             - Compilar el proyecto"
	@echo "  make download-stb- Descargar librerías STB"
	@echo "  make install     - Instalar el servicio"
	@echo "  make uninstall   - Desinstalar el servicio"
	@echo "  make clean       - Limpiar archivos compilados"
	@echo "  make clean-all   - Limpiar todo incluyendo STB"
	@echo "  make test        - Verificar configuración"
	@echo "  make help        - Mostrar esta ayuda"
