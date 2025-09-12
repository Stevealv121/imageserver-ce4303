#!/bin/bash

echo "=== Reorganizando estructura del proyecto ImageServer ==="

# Crear directorios necesarios
echo "Creando directorios..."
mkdir -p src
mkdir -p include
mkdir -p bin
mkdir -p obj
mkdir -p include/stb

# Mover archivos .c a src/
echo "Moviendo archivos fuente (.c) a src/..."
if [ -f "config.c" ]; then mv config.c src/; fi
if [ -f "daemon.c" ]; then mv daemon.c src/; fi
if [ -f "logger.c" ]; then mv logger.c src/; fi
if [ -f "main.c" ]; then mv main.c src/; fi

# Mover archivos .h a include/
echo "Moviendo archivos header (.h) a include/..."
if [ -f "config.h" ]; then mv config.h include/; fi
if [ -f "daemon.h" ]; then mv daemon.h include/; fi
if [ -f "logger.h" ]; then mv logger.h include/; fi

# Verificar estructura
echo ""
echo "=== Nueva estructura del proyecto ==="
echo "Directorios creados:"
ls -la | grep "^d"

echo ""
echo "Archivos en src/:"
ls -la src/ 2>/dev/null || echo "  (vacío o no existe)"

echo ""
echo "Archivos en include/:"
ls -la include/ 2>/dev/null || echo "  (vacío o no existe)"

echo ""
echo "=== Próximos pasos ==="
echo "1. Ejecuta: make download-stb"
echo "2. Ejecuta: make test"
echo "3. Ejecuta: make"
echo ""
echo "¡Reorganización completada!"
