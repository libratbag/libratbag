#!/bin/bash
echo "=== DIAGNÓSTICO COMPLETO DEL DRIVER SENTEY ==="
echo ""

echo "1. Verificando archivos .device..."
echo "Archivos encontrados:"
ls -la /workspaces/libratbag/data/devices/ | grep sentey
echo ""

echo "2. Verificando contenido del archivo correcto..."
if [ -f "/workspaces/libratbag/data/devices/sentey-revolution-pro-gs-3910.device" ]; then
    echo "Contenido de sentey-revolution-pro-gs-3910.device:"
    cat /workspaces/libratbag/data/devices/sentey-revolution-pro-gs-3910.device
else
    echo "❌ Archivo sentey-revolution-pro-gs-3910.device NO encontrado"
fi
echo ""

echo "3. Verificando si existe archivo duplicado..."
if [ -f "/workspaces/libratbag/data/devices/sentey-gs-3910.device" ]; then
    echo "❌ Archivo duplicado aún existe:"
    cat /workspaces/libratbag/data/devices/sentey-gs-3910.device
    echo "Eliminando archivo duplicado..."
    rm /workspaces/libratbag/data/devices/sentey-gs-3910.device
    echo "✅ Archivo duplicado eliminado"
else
    echo "✅ No hay archivo duplicado"
fi
echo ""

echo "4. Verificando driver compilado..."
echo "Buscando libratbag.so..."
find /usr/local -name "*libratbag*" -type f 2>/dev/null | head -5
echo ""

echo "5. Verificando archivos de datos instalados..."
echo "Directorio de datos de libratbag:"
ls -la /usr/local/share/libratbag/devices/ 2>/dev/null || echo "Directorio no encontrado"
echo ""

echo "6. Verificando driver registrado..."
echo "Buscando referencias a sentey en libratbag:"
grep -r "sentey" /usr/local/lib/ 2>/dev/null | head -3 || echo "No encontrado en libratbag"
echo ""

echo "7. Probando compilación del driver..."
cd /workspaces/libratbag
echo "Compilando solo el driver sentey..."
gcc -c -I. -I./src -I/usr/include/libevdev-1.0 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include src/driver-sentey.c -o /tmp/driver-sentey.o 2>&1
if [ $? -eq 0 ]; then
    echo "✅ Driver compila correctamente"
    rm /tmp/driver-sentey.o
else
    echo "❌ Error de compilación en driver"
fi
echo ""

echo "8. Verificando libratbag-data.c..."
echo "Buscando SENTEY en libratbag-data.c:"
grep -n "SENTEY" /workspaces/libratbag/src/libratbag-data.c || echo "SENTEY no encontrado"
echo ""

echo "=== FIN DEL DIAGNÓSTICO ==="
echo ""
echo "Para más información, ejecuta:"
echo "sudo /usr/local/sbin/ratbagd --verbose=raw 2>&1 | grep -i sentey"