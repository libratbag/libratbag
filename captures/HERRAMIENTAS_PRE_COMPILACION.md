# HERRAMIENTAS DE VALIDACIÓN PRE-COMPILACIÓN
## Directorios `test/`, `tools/` y `doc/` - libratbag

Este documento describe las herramientas disponibles en libratbag que pueden utilizarse **antes de compilar** el driver para validar y mejorar el código.

## 🧪 Directorio `test/` - Validación y Testing

### `data-parse-test.py` - ✅ MUY ÚTIL
**Ubicación:** `test/data-parse-test.py`

**Función:** Valida la sintaxis y formato de archivos `.device`

**Uso:**
```bash
python3 test/data-parse-test.py data/devices/sentey-revolution-pro-gs-3910.device
```

**Qué valida:**
- ✅ DeviceMatch (IDs USB/Bluetooth)
- ✅ DeviceType (mouse, keyboard, other)
- ✅ DPI ranges (`min:max@steps`)
- ✅ DPI lists (`dpi1;dpi2;dpi3`)
- ✅ Sintaxis general del archivo

**Por qué útil para Sentey:**
- Confirma que el archivo `.device` está correctamente formateado
- Detecta errores de sintaxis antes de compilar
- Valida rangos DPI y configuraciones

### `duplicate-check.py` - ✅ ÚTIL
**Ubicación:** `test/duplicate-check.py`

**Función:** Detecta dispositivos duplicados en archivos `.device`

**Uso:**
```bash
python3 test/duplicate-check.py data/devices/
```

**Qué valida:**
- IDs USB duplicados entre dispositivos
- Conflictos de DeviceMatch

**Por qué útil para Sentey:**
- Evita conflictos con otros dispositivos
- Asegura que el ID USB `e0ff:0002` no esté duplicado

### Scripts de Calidad de Código

#### `python-black-check.sh`
**Función:** Verifica formato del código Python usando `black`

**Requisitos:** `black` instalado (`pip install black`)

**Uso:** Se ejecuta automáticamente durante el build si está disponible

#### `python-ruff-check.sh`
**Función:** Verifica calidad del código Python usando `ruff`

**Requisitos:** `ruff` instalado (`pip install ruff`)

**Uso:** Se ejecuta automáticamente durante el build si está disponible

## 🔧 Directorio `tools/` - Desarrollo y Debugging

### `ratbagc.py.in` - ✅ MUY ÚTIL (Cliente Python)
**Ubicación:** `tools/ratbagc.py.in`

**Función:** Cliente de línea de comandos para libratbag (se convierte en `ratbagctl`)

**Comandos útiles después de compilar:**
```bash
ratbagctl list                    # Listar dispositivos detectados
ratbagctl info <device>           # Información detallada del dispositivo
ratbagctl <device> profile active get    # Ver perfil activo
ratbagctl <device> profile active set 0  # Cambiar a perfil 0
ratbagctl <device> button 0 get          # Ver configuración del botón 0
ratbagctl <device> button 0 action set button 1  # Configurar botón
ratbagctl <device> led 0 get             # Ver configuración LED
ratbagctl <device> led 0 set mode on     # Configurar LED
```

**Por qué útil para Sentey:**
- Probar que el driver detecta correctamente el mouse
- Verificar configuración de botones y LEDs
- Debuggear problemas de funcionamiento

### `hidpp10-dump-page.c` / `hidpp20-dump-page.c` - 🔍 PARA ANÁLISIS
**Ubicación:** `tools/hidpp10-dump-page.c`, `tools/hidpp20-dump-page.c`

**Función:** Herramientas para dumpear páginas HID de dispositivos Logitech

**Limitación:** Específicas para protocolo HID++ de Logitech, no aplican a Sentey

### `toolbox.py` - 📚 UTILIDADES
**Ubicación:** `tools/toolbox.py`

**Función:** Utilidades para desarrollo y testing de libratbag

**Útil para:** Crear scripts personalizados de testing

### `check_scan_build.py` - 🔍 ANÁLISIS ESTÁTICO
**Ubicación:** `tools/check_scan_build.py`

**Función:** Analiza reportes de scan-build (herramienta de análisis estático)

**Uso:** Procesar reportes HTML generados por `scan-build make`

## 📚 Directorio `doc/` - Documentación

### `index.rst` y `dbus.rst`
**Ubicación:** `doc/index.rst`, `doc/dbus.rst`

**Contenido:**
- Documentación completa de la API DBus de libratbag
- Interfaces públicas y protocolos
- Guías de desarrollo

**Formato:** RST (reStructuredText) - puede convertirse a HTML con Sphinx

**Por qué útil:**
- Entender la interfaz DBus para desarrollo avanzado
- Referencia de protocolos y APIs
- Documentación técnica del proyecto

## 🎯 WORKFLOW RECOMENDADO PRE-COMPILACIÓN

### Paso 1: Validar Archivo .device
```bash
cd /workspaces/libratbag
python3 test/data-parse-test.py data/devices/sentey-revolution-pro-gs-3910.device
```

### Paso 2: Verificar Duplicados
```bash
python3 test/duplicate-check.py data/devices/
```

### Paso 3: Revisar Documentación (si necesario)
- Leer `doc/dbus.rst` para entender APIs complejas

### Paso 4: Compilar
```bash
meson setup build
ninja -C build
```

### Paso 5: Probar con ratbagctl
```bash
./build/tools/ratbagctl list
./build/tools/ratbagctl info "Sentey Revolution Pro GS-3910"
```

## ⚠️ LIMITACIONES

- **ratbagctl no disponible** hasta después de compilar
- **Scripts HID específicos** para Logitech (no aplican a Sentey)
- **Algunos tests requieren** herramientas externas (`black`, `ruff`, `scan-build`)

## 📋 CHECKLIST DE VALIDACIÓN PRE-COMPILACIÓN

- [ ] `data-parse-test.py` ejecutado sin errores
- [ ] `duplicate-check.py` ejecutado sin conflictos
- [ ] Archivo `.device` validado
- [ ] Código del driver revisado
- [ ] Documentación consultada si necesario

## 🔗 REFERENCIAS

- **Proyecto libratbag:** https://github.com/libratbag/libratbag
- **Documentación DBus:** `doc/dbus.rst`
- **API libratbag:** Headers en `src/libratbag-private.h`

---
**Fecha de creación:** Abril 2026
**Driver:** Sentey GS-3910
**Versión libratbag:** 0.18 (API v2)</content>
<parameter name="filePath">/workspaces/libratbag/captures/HERRAMIENTAS_PRE_COMPILACION.md