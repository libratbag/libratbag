#!/bin/bash
echo "=== ANÁLISIS DETALLADO DEL ERROR DEL DRIVER SENTEY ==="
echo ""

echo "🔍 PASO 1: Verificar archivos .device"
echo "Archivos sentey encontrados:"
ls -la /workspaces/libratbag/data/devices/ | grep sentey || echo "Ningún archivo sentey encontrado"
echo ""

echo "🔍 PASO 2: Verificar contenido del archivo válido"
if [ -f "/workspaces/libratbag/data/devices/sentey-revolution-pro-gs-3910.device" ]; then
    echo "✅ Archivo sentey-revolution-pro-gs-3910.device encontrado"
    echo "Contenido:"
    cat /workspaces/libratbag/data/devices/sentey-revolution-pro-gs-3910.device
    echo ""

    echo "Validando sintaxis..."
    python3 -c "
import configparser
config = configparser.ConfigParser(strict=True)
config.optionxform = lambda option: option
try:
    config.read('/workspaces/libratbag/data/devices/sentey-revolution-pro-gs-3910.device')
    print('✅ Sintaxis correcta')
    print('Secciones:', list(config.sections()))
    for section in config.sections():
        print(f'Sección [{section}]:', dict(config[section]))
except Exception as e:
    print('❌ Error de sintaxis:', e)
"
else
    echo "❌ Archivo sentey-revolution-pro-gs-3910.device NO encontrado"
fi
echo ""

echo "🔍 PASO 3: Verificar driver en código fuente"
echo "Buscando sentey_driver en archivos fuente..."
grep -n "sentey_driver" /workspaces/libratbag/src/*.c /workspaces/libratbag/src/*.h | head -5
echo ""

echo "🔍 PASO 4: Verificar compilación"
echo "Intentando compilar libratbag..."
cd /workspaces/libratbag
if [ -d "build" ]; then
    echo "Directorio build existe, verificando..."
    ls -la build/ | head -5
    echo "Intentando recompilar..."
    ninja -C build 2>&1 | grep -i sentey || echo "Sin mensajes específicos de sentey"
else
    echo "No hay directorio build, ejecuta: meson setup build"
fi
echo ""

echo "🔍 PASO 5: Verificar instalación"
echo "Buscando libratbag instalado..."
find /usr/local -name "*libratbag*" -type f 2>/dev/null | grep -v "\.a$" | head -3
echo ""

echo "🔍 PASO 6: Probar carga manual del driver"
echo "Verificando si el driver se puede cargar..."
if command -v ratbagctl >/dev/null 2>&1; then
    echo "ratbagctl encontrado, probando..."
    ratbagctl list 2>&1 | head -5
else
    echo "ratbagctl no encontrado en PATH"
fi
echo ""

echo "🔍 PASO 7: Verificar variables de entorno"
echo "LIBRATBAG_DATA_DIR: $LIBRATBAG_DATA_DIR"
echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
echo ""

echo "=== RECOMENDACIONES ==="
echo "1. Si hay archivo duplicado: rm /workspaces/libratbag/data/devices/sentey-gs-3910.device"
echo "2. Recompilar: cd /workspaces/libratbag && rm -rf build && meson setup build && ninja -C build"
echo "3. Reinstalar: sudo ninja -C build install"
echo "4. Reiniciar ratbagd: sudo systemctl restart ratbagd"
echo "5. Probar: sudo /usr/local/sbin/ratbagd --verbose=raw"