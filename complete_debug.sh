#!/bin/bash
echo "=== DEBUG DETALLADO: ¿POR QUÉ NO CARGA EL DRIVER SENTEY? ==="
echo ""

# Función para verificar y mostrar resultados
check_result() {
    if [ $? -eq 0 ]; then
        echo "✅ $1"
    else
        echo "❌ $1"
    fi
}

echo "🔍 1. VERIFICANDO ARCHIVOS .DEVICE"
echo "Archivos sentey en directorio fuente:"
ls -la /workspaces/libratbag/data/devices/ | grep sentey
echo ""

echo "Verificando archivo correcto existe:"
if [ -f "/workspaces/libratbag/data/devices/sentey-revolution-pro-gs-3910.device" ]; then
    echo "✅ Archivo .device existe"
    echo "Contenido:"
    cat /workspaces/libratbag/data/devices/sentey-revolution-pro-gs-3910.device
else
    echo "❌ Archivo .device NO existe"
fi
echo ""

echo "🔍 2. VERIFICANDO INSTALACIÓN DE ARCHIVOS DE DATOS"
echo "Directorio de datos instalado:"
ls -la /usr/local/share/libratbag/devices/ 2>/dev/null || echo "❌ Directorio de datos no encontrado"
echo ""

echo "Buscando archivos sentey instalados:"
find /usr/local/share/libratbag/ -name "*sentey*" 2>/dev/null || echo "❌ No se encontraron archivos sentey instalados"
echo ""

echo "🔍 3. VERIFICANDO DRIVER COMPILADO"
echo "Buscando libratbag compilado:"
find /usr/local/lib/ -name "*libratbag*" 2>/dev/null | head -3 || echo "❌ Libratbag no encontrado en /usr/local/lib/"
echo ""

echo "Verificando símbolos del driver en libratbag:"
if command -v nm >/dev/null 2>&1; then
    LIBRATBAG_SO=$(find /usr/local/lib/ -name "*libratbag*.so*" 2>/dev/null | head -1)
    if [ -n "$LIBRATBAG_SO" ]; then
        echo "Analizando símbolos en $LIBRATBAG_SO:"
        nm -D "$LIBRATBAG_SO" 2>/dev/null | grep -i sentey | head -3 || echo "No se encontraron símbolos sentey"
    else
        echo "❌ No se encontró libratbag.so"
    fi
else
    echo "❌ nm no disponible"
fi
echo ""

echo "🔍 4. VERIFICANDO RATBAGD"
echo "Ubicación de ratbagd:"
which ratbagd 2>/dev/null || echo "❌ ratbagd no encontrado en PATH"
ls -la /usr/local/sbin/ratbagd 2>/dev/null || echo "❌ ratbagd no encontrado en /usr/local/sbin/"
echo ""

echo "Versión de ratbagd:"
/usr/local/sbin/ratbagd --version 2>/dev/null || echo "❌ No se puede ejecutar ratbagd"
echo ""

echo "🔍 5. PRUEBA DE CONECTIVIDAD CON MOUSE"
echo "Dispositivos HID conectados:"
lsusb | grep -i "e0ff:0002" || echo "❌ Mouse Sentey no detectado por lsusb"
echo ""

echo "Dispositivos de entrada:"
cat /proc/bus/input/devices | grep -A 5 -B 5 -i "sentey\|e0ff\|044e:120a" || echo "❌ Mouse no encontrado en /proc/bus/input/devices"
echo ""

echo "🔍 6. PRUEBA DE CARGA MANUAL"
echo "Intentando cargar libratbag manualmente:"
if command -v python3 >/dev/null 2>&1; then
    python3 -c "
import sys
sys.path.insert(0, '/usr/local/lib/python3.*/site-packages')
try:
    import libratbag
    print('✅ libratbag se puede importar')
    ratbag = libratbag.Ratbag()
    print('✅ Ratbag creado correctamente')
    print('Drivers disponibles:', [d.name for d in ratbag.drivers])
    sentey_found = any('sentey' in d.id for d in ratbag.drivers)
    if sentey_found:
        print('✅ Driver sentey encontrado')
    else:
        print('❌ Driver sentey NO encontrado')
except Exception as e:
    print('❌ Error importando libratbag:', e)
"
else
    echo "❌ Python3 no disponible"
fi
echo ""

echo "=== DIAGNÓSTICO COMPLETO ==="
echo ""
echo "POSIBLES CAUSAS DEL ERROR:"
echo "1. ❌ Archivo .device duplicado existe"
echo "2. ❌ Archivos de datos no instalados en /usr/local/share/libratbag/"
echo "3. ❌ Driver no compilado en libratbag.so"
echo "4. ❌ ratbagd no reiniciado después de instalación"
echo "5. ❌ Variables de entorno incorrectas"
echo ""
echo "SOLUCIONES RECOMENDADAS:"
echo "1. rm /workspaces/libratbag/data/devices/sentey-gs-3910.device"
echo "2. cd /workspaces/libratbag && rm -rf build"
echo "3. meson setup build --prefix=/usr/local -Dsystemd=true"
echo "4. ninja -C build && sudo ninja -C build install"
echo "5. sudo systemctl restart ratbagd"
echo "6. sudo /usr/local/sbin/ratbagd --verbose=raw"