#!/bin/bash
echo "=== CORRECCIÓN AUTOMÁTICA DEL DRIVER SENTEY ==="
echo ""

echo "🔧 PASO 1: Eliminando archivo duplicado..."
if [ -f "/workspaces/libratbag/data/devices/sentey-gs-3910.device" ]; then
    rm /workspaces/libratbag/data/devices/sentey-gs-3910.device
    echo "✅ Archivo duplicado eliminado"
else
    echo "✅ No hay archivo duplicado"
fi
echo ""

echo "🔧 PASO 2: Limpiando build anterior..."
cd /workspaces/libratbag
rm -rf build
echo "✅ Build limpiado"
echo ""

echo "🔧 PASO 3: Configurando meson..."
meson setup build --prefix=/usr/local -Dsystemd=true
if [ $? -eq 0 ]; then
    echo "✅ Meson configurado correctamente"
else
    echo "❌ Error en configuración de meson"
    exit 1
fi
echo ""

echo "🔧 PASO 4: Compilando..."
ninja -C build
if [ $? -eq 0 ]; then
    echo "✅ Compilación exitosa"
else
    echo "❌ Error en compilación"
    echo "Mostrando últimos errores:"
    ninja -C build 2>&1 | tail -20
    exit 1
fi
echo ""

echo "🔧 PASO 5: Instalando..."
sudo ninja -C build install
if [ $? -eq 0 ]; then
    echo "✅ Instalación exitosa"
else
    echo "❌ Error en instalación"
    exit 1
fi
echo ""

echo "🔧 PASO 6: Verificando instalación..."
echo "Archivos .device instalados:"
ls -la /usr/local/share/libratbag/devices/ | grep sentey || echo "❌ Archivos .device no instalados"
echo ""

echo "Driver en libratbag:"
find /usr/local/lib/ -name "*libratbag*.so*" -exec nm -D {} \; 2>/dev/null | grep -i sentey | head -3 || echo "❌ Driver no encontrado en libratbag"
echo ""

echo "🔧 PASO 7: Reiniciando servicios..."
sudo systemctl restart ratbagd 2>/dev/null || echo "⚠️  No se pudo reiniciar ratbagd (posiblemente no está como servicio)"
echo ""

echo "🎯 PRUEBA FINAL:"
echo "Ejecutando: sudo /usr/local/sbin/ratbagd --verbose=raw"
echo "Si ves errores, ejecuta: ./complete_debug.sh"
echo ""
echo "=== CORRECCIÓN COMPLETADA ==="