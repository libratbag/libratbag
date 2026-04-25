# REQUISITOS PARA CREAR UN DRIVER EN LIBRATBAG V0.18 (API v2)

## 1. ESTRUCTURA BÁSICA DEL ARCHIVO DEL DRIVER

### Cabeceras Requeridas
```c
#include "config.h"
#include <assert.h>
#include <errno.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "libratbag-data.h"  // Si requiere datos específicos del dispositivo
```

### Definición de Constantes del Dispositivo
```c
#define NOMBRE_NUM_PROFILES    X          // Número de perfiles soportados
#define NOMBRE_NUM_DPI         X          // Número de niveles DPI
#define NOMBRE_NUM_BUTTONS     X          // Número de botones
#define NOMBRE_NUM_LEDS        X          // Número de LEDs
#define NOMBRE_REPORT_ID       0xXX       // Report ID si aplica
#define NOMBRE_REPORT_SIZE     XX         // Tamaño del reporte HID
```

---

## 2. FUNCIONES ESENCIALES DEL DRIVER

### 2.1 Función `probe` (Detección del Dispositivo)
**Propósito:** Inicializar el dispositivo cuando es detectado.

**Requisitos:**
- Verificar que el dispositivo es compatible (VID/PID)
- Abrir la interfaz HID correcta (`ratbag_open_hidraw_index`)
- Inicializar perfiles con `ratbag_device_init_profiles`
- Configurar capacidades de cada perfil
- Establecer valores por defecto (DPI, botones, LEDs, report rate)
- Habilitar tipos de acciones soportadas para botones
- Habilitar modos de LED soportados
- Leer configuración actual del hardware (si es posible)
- Retornar `0` en éxito o `-errno` en error

**Ejemplo:**
```c
static int
nombre_probe(struct ratbag_device *device)
{
    struct ratbag_profile *profile;
    struct ratbag_resolution *resolution;
    struct ratbag_button *button;
    struct ratbag_led *led;
    int rc;

    // Verificar dispositivo HID
    rc = ratbag_find_hidraw(device, nombre_test_hidraw);
    if (rc)
        return rc;

    // Abrir endpoint HID
    rc = ratbag_open_hidraw_index(device, ENDPOINT, INDEX);
    if (rc)
        return rc;

    // Inicializar perfiles
    ratbag_device_init_profiles(device,
                                NOMBRE_NUM_PROFILES,
                                NOMBRE_NUM_DPI,
                                NOMBRE_NUM_BUTTONS,
                                NOMBRE_NUM_LEDS);

    // Configurar cada perfil
    ratbag_device_for_each_profile(device, profile) {
        profile->is_active = true;
        
        // Configurar capacidades
        ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);
        
        // Configurar report rates soportados
        static const unsigned int report_rates[] = { 125, 250, 500, 1000 };
        ratbag_profile_set_report_rate_list(profile, report_rates,
                                            ARRAY_LENGTH(report_rates));
        profile->hz = 1000;

        // Configurar resoluciones (DPI)
        ratbag_profile_for_each_resolution(profile, resolution) {
            if (resolution->index == 0) {
                resolution->is_active = true;
                resolution->is_default = true;
            }
            
            // Establecer lista de DPI soportados
            ratbag_resolution_set_dpi_list(resolution, dpi_values, n_dpis);
            
            // Valores por defecto
            resolution->dpi_x = 800 * (resolution->index + 1);
            resolution->dpi_y = resolution->dpi_x;
        }

        // Configurar botones
        ratbag_profile_for_each_button(profile, button) {
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
            
            // Asignar acción por defecto
            button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
            button->action.action.button = button->index + 1;
        }

        // Configurar LEDs
        ratbag_profile_for_each_led(profile, led) {
            led->mode = RATBAG_LED_ON;
            led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
            led->color.red = 0;
            led->color.green = 0;
            led->color.blue = 255;
            
            ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
            ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
            ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
            ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
        }
    }

    return 0;
}
```

---

### 2.2 Función `read_profile` (Lectura de Configuración)
**Propósito:** Leer la configuración actual desde el hardware.

**Nota:** Algunos dispositivos no soportan lectura. En ese caso, usar `RATBAG_PROFILE_CAP_WRITE_ONLY`.

**Requisitos:**
- Enviar comando HID de lectura usando `ratbag_hidraw_output_report` o `ratbag_hidraw_raw_request`
- Recibir respuesta con `ratbag_hidraw_read_input_report_index`
- Parsear los bytes recibidos
- Actualizar estructuras de libratbag (`profile`, `resolution`, `button`, `led`)
- Retornar `0` o `-errno`

**Comandos HID Comunes:**
```c
// Enviar comando
msleep(10);  // Esperar si el dispositivo lo requiere
ret = ratbag_hidraw_output_report(device, buffer, buffer_size);

// O usar raw_request para FEATURE_REPORTS
ret = ratbag_hidraw_raw_request(device, report_id, buffer, size,
                                HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

// Leer respuesta
ret = ratbag_hidraw_read_input_report_index(device, buffer, size, index, NULL);
```

---

### 2.3 Función `write_profile` (Escritura de Configuración)
**Propósito:** Escribir cambios en el hardware.

**Requisitos:**
- Iterar sobre resoluciones, botones y LEDs marcados como `dirty`
- Construir paquetes HID con los nuevos valores
- Enviar comandos al dispositivo
- Manejar errores apropiadamente
- Retornar `0` o `-errno`

**Patrón Típico:**
```c
static int
nombre_write_profile(struct ratbag_profile *profile)
{
    struct ratbag_resolution *resolution;
    struct ratbag_button *button;
    struct ratbag_led *led;
    struct ratbag_device *device = profile->device;
    int rc;
    bool something_dirty = false;

    // Verificar y escribir DPI
    ratbag_profile_for_each_resolution(profile, resolution) {
        if (!resolution->dirty)
            continue;

        rc = nombre_write_dpi(resolution);
        if (rc != 0) {
            log_error(device->ratbag, "Failed to write DPI: %s (%d)\n",
                      strerror(-rc), rc);
            return rc;
        }
    }

    // Verificar y escribir botones
    ratbag_profile_for_each_button(profile, button) {
        if (button->dirty)
            something_dirty = true;
    }

    if (something_dirty) {
        rc = nombre_write_buttons(profile);
        if (rc != 0)
            return rc;
    }

    // Verificar y escribir LEDs
    ratbag_profile_for_each_led(profile, led) {
        if (!led->dirty)
            continue;

        rc = nombre_write_led(led);
        if (rc != 0)
            return rc;
    }

    return 0;
}
```

---

### 2.4 Función `commit` (Persistir Cambios)
**Propósito:** Aplicar todos los cambios pendientes y guardar en memoria no volátil.

**Requisitos:**
- Iterar sobre perfiles marcados como `dirty`
- Llamar a `write_profile` para cada perfil
- Enviar comando de guardado si el dispositivo lo requiere
- Retornar `0` o `-errno`

**Ejemplo:**
```c
static int
nombre_commit(struct ratbag_device *device)
{
    struct ratbag_profile *profile;
    int rc = 0;

    list_for_each(profile, &device->profiles, link) {
        if (!profile->dirty)
            continue;

        log_debug(device->ratbag, "Profile %d changed, rewriting\n", profile->index);

        rc = nombre_write_profile(profile);
        if (rc) {
            log_error(device->ratbag, "Failed to write profile: %s (%d)\n",
                      strerror(-rc), rc);
            return rc;
        }

        // Guardar en memoria no volátil
        rc = nombre_write_save(device);
        if (rc) {
            log_error(device->ratbag, "Failed to save profile: %s (%d)\n",
                      strerror(-rc), rc);
            return rc;
        }
    }

    return 0;
}
```

---

### 2.5 Función `remove` (Limpieza)
**Propósito:** Liberar recursos cuando el dispositivo es desconectado.

**Ejemplo:**
```c
static void
nombre_remove(struct ratbag_device *device)
{
    ratbag_close_hidraw_index(device, 0);
    ratbag_close_hidraw_index(device, 1);  // Si hay múltiples endpoints
}
```

---

## 3. COMUNICACIÓN HID

### 3.1 Funciones de Envío/Recepción

| Función | Descripción | Uso |
|---------|-------------|-----|
| `ratbag_hidraw_output_report(device, buf, len)` | Envía un OUTPUT_REPORT | Comandos de escritura |
| `ratbag_hidraw_raw_request(device, id, buf, size, type, req)` | Envía FEATURE_REPORT | Lectura/escritura directa |
| `ratbag_hidraw_read_input_report_index(device, buf, size, index, timeout)` | Lee INPUT_REPORT | Respuestas del dispositivo |

### 3.2 Tipos de Reportes HID
- **HID_OUTPUT_REPORT**: Para enviar comandos al dispositivo
- **HID_INPUT_REPORT**: Para recibir datos del dispositivo
- **HID_FEATURE_REPORT**: Para configuración directa (get/set)

### 3.3 Constantes de Request
- `HID_REQ_SET_REPORT`: Para escribir configuración
- `HID_REQ_GET_REPORT`: Para leer configuración

---

## 4. MANEJO DE PERFILES, DPI, BOTONES Y LEDS

### 4.1 Perfiles
```c
// Iterar sobre perfiles
ratbag_device_for_each_profile(device, profile) {
    profile->is_active = true;
    profile->hz = 1000;  // Report rate
    
    // Marcar como write-only si no se puede leer
    ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);
}
```

### 4.2 DPI/Resoluciones
```c
// Establecer lista de DPI soportados
unsigned int dpi_values[] = { 400, 800, 1600, 3200, 6400 };
ratbag_resolution_set_dpi_list(resolution, dpi_values, 5);

// Marcar resolución activa
resolution->is_active = true;
resolution->is_default = true;

// Leer estado dirty
if (resolution->dirty) {
    // El usuario cambió este DPI
    int new_dpi = resolution->dpi_x;
}
```

### 4.3 Botones
```c
// Habilitar tipos de acción
ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);

// Tipos de acción especiales
BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP)
BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP)
BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN)
BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP)

// Acciones de botón
BUTTON_ACTION_BUTTON(1)  // Click izquierdo
BUTTON_ACTION_BUTTON(2)  // Click derecho
BUTTON_ACTION_BUTTON(3)  // Rueda click

// Verificar si está dirty
if (button->dirty) {
    struct ratbag_button_action *action = &button->action;
    // Procesar nueva acción
}
```

### 4.4 LEDs
```c
// Configurar profundidad de color
led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
led->color.red = 255;
led->color.green = 0;
led->color.blue = 0;

// Habilitar modos
ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);

// Modos disponibles
RATBAG_LED_OFF        // Apagado
RATBAG_LED_ON         // Color fijo
RATBAG_LED_BREATHING  // Respiración (led->ms para duración)
RATBAG_LED_CYCLE      // Ciclo de colores (led->ms para duración)
```

---

## 5. GESTIÓN DE ERRORES Y LOGGING

### 5.1 Códigos de Error
- Retornar `0` en éxito
- Retornar `-errno` en error (ej. `-EINVAL`, `-ENOTSUP`, `-EIO`)

### 5.2 Logging
```c
log_debug(device->ratbag, "Mensaje de debug: %d\n", valor);
log_info(device->ratbag, "Mensaje informativo\n");
log_error(device->ratbag, "Error: %s (%d)\n", strerror(-rc), rc);
log_bug(device->ratbag, "Bug detectado\n");  // Para condiciones imposibles
```

---

## 6. INTEGRACIÓN CON MESON.BUILD

### 6.1 Agregar el Driver al Build
Editar `/workspace/src/meson.build`:

```python
src_libratbag = files(
    'driver-asus.c',
    'driver-etekcity.c',
    'driver-sentey.c',  # <-- Agregar tu driver aquí
    ...
)
```

### 6.2 Registrar el Driver
Al final del archivo del driver:

```c
struct ratbag_driver sentey_driver = {
    .name = "Sentey",
    .id = "sentey",
    .probe = sentey_probe,
    .remove = sentey_remove,
    .commit = sentey_commit,
};
```

---

## 7. PATRONES COMUNES DE COMANDOS HID

### 7.1 Estructura de Mensaje Típica
```c
union device_message {
    struct {
        uint8_t report_id;
        uint8_t command;
        uint8_t data[SIZE];
    } __attribute__((packed)) msg;
    uint8_t data[TOTAL_SIZE];
};

_Static_assert(sizeof(union device_message) == TOTAL_SIZE,
               "Size of union is wrong");
```

### 7.2 Secuencia de Escritura Típica
```c
// 1. Preparar buffer
union device_message msg = {
    .msg.report_id = REPORT_ID,
    .msg.command = COMMAND_CODE,
    .msg.data = { /* datos */ },
};

// 2. Esperar si es necesario
msleep(10);

// 3. Enviar comando
ret = ratbag_hidraw_output_report(device, msg.data, msg_size);
if (ret < 0)
    return ret;

// 4. Esperar respuesta si es necesario
msleep(20);

// 5. Leer respuesta (si aplica)
uint8_t response[SIZE];
ret = ratbag_hidraw_read_input_report_index(device, response, SIZE, INDEX, NULL);
if (ret < 0)
    return ret;
```

---

## 8. CHECKLIST PARA IMPLEMENTAR DRIVER SENTEY GS-3910

### Información del Dispositivo
- [ ] Vendor ID: `0xe0ff`
- [ ] Product ID: `0x0002`
- [ ] Nombre: "Sentey Revolution Pro GS-3910"

### Capacidades a Implementar
- [ ] 5 perfiles
- [ ] 4 niveles de DPI (2000, 4200, 6200, 8200)
- [ ] 9 botones configurables (A2-A10)
- [ ] LEDs RGB configurables
- [ ] Report rates: 125, 250, 500, 1000 Hz

### Comandos HID Identificados (según capturas)
- [ ] `0x0188` - Selección de perfil
- [ ] `0x0185` - Configuración de botones
- [ ] `0x0186` / `0x0102` - Configuración RGB
- [ ] Comandos DPI (por determinar exactamente)
- [ ] Comando de guardado (por determinar)

---

## 9. INFORMACIÓN ESPECÍFICA DEL DISPOSITIVO SENTEY GS-3910

### 9.1 Documentación de Capturas (`captures/`)
- **guia.txt**: Mapeo completo de nombres de archivos a acciones realizadas
  - A2-A10: Botones físicos del mouse
  - Perfil 1-5: Selección de perfiles
  - DPI1-4: Configuración de niveles DPI (2000, 4200, 6200, 8200)
  - colores posibles perfil 1.txt: Configuración RGB del perfil 1
  - capturaLRB.txt: Comunicación actual con libratbag (muestra error de driver steelseries)

- **ANALISIS_PATRONES.md**: Análisis detallado del protocolo HID
  - Estructura: SET_REPORT con wValue=0x0300, 8 bytes payload
  - Comando perfil: `01 88 XX 00 00 00 00 12` (XX=00-04 para perfiles 1-5)
  - Comando botones: `01 85 00 BB CC DD EE 12` (BB=índice botón, CC DD=código función)
  - Comando RGB: `01 86 AA BB CC DD EE FF` + respuesta `01 02 00 XX XX XX XX 12`
  - DPI: Por determinar (capturas no incluyen Data Fragment completo)

### 9.2 Mapeo de Botones
Según `guia.txt` y análisis:
- Botón físico 2 (A2) → Índice dispositivo 0x01
- Botón físico 3 (A3) → Índice dispositivo 0x02
- Botón físico 4 (A4) → Índice dispositivo 0x04
- Botón físico 5 (A5) → Índice dispositivo 0x03
- Botón físico 6 (A6) → Índice dispositivo 0x05
- Botón físico 7 (A7) → Índice dispositivo 0x06
- Botón físico 8 (A8) → Índice dispositivo 0x07
- Botón físico 9 (A9) → Índice dispositivo 0x08
- Botón físico 10 (A10) → Índice dispositivo 0x09

**Nota importante**: El mapeo comienza desde el botón 2 (índice 1), el botón 1 es el click izquierdo principal.

### 9.3 Funciones de Botones Identificadas
- `0x801e`: LeftClick
- `0x00ff`: RightClick, WheelClick, Button4, Button5, ScrollUp, ScrollDown

---

## 10. EJEMPLOS DE DRIVERS EXISTENTES

### 10.1 Driver Logitech G600 (`driver-logitech-g600.c`)
**Características:**
- 3 perfiles, 41 botones (20 + G-shift), 4 DPI, 1 LED
- Estructuras complejas: `logitech_g600_profile_report` (154 bytes)
- Report IDs específicos: 0xF0, 0xF3-F5
- Manejo de macros y efectos LED complejos
- Comunicación: HID reports con estructuras empaquetadas

### 10.2 Driver Roccat Kone EMP (`driver-roccat-kone-emp.c`)
**Características:**
- 5 perfiles, 22 botones (Easy Shift), 5 DPI, 4 LEDs
- Report IDs: 4-8 para diferentes configuraciones
- Manejo de macros (hasta 480 eventos)
- Efectos LED: sólido, breathing, cycle
- Comunicación: HID con retry logic y magic numbers

### 10.3 Driver Sinowealth (`driver-sinowealth.c`)
**Características:**
- Múltiples dispositivos con configuración por device ID
- Report IDs: 0x4 (config), 0x5 (cmd), 0x6 (config long)
- Comandos enumerados: firmware, profile, config, buttons, macro
- DPI: 100-2000+ con step 100
- Comunicación: HID feature reports con estructuras de comando

### 10.4 Driver SteelSeries (`driver-steelseries.c`)
**Características:**
- Múltiples versiones de protocolo (1-4)
- Estructuras de datos complejas por dispositivo
- Manejo de LEDs con ciclos y efectos
- Comunicación: HID con diferentes tamaños de reporte
- Configuración por device data (quirks, DPI lists, etc.)

**Lecciones para Sentey:**
- Usar estructuras empaquetadas para payloads HID
- Implementar probe con verificación de HID interfaces
- Manejar diferentes versiones/protocolos si es necesario
- Usar logging apropiado para debugging

---

## 11. FORMATOS DE ARCHIVOS .device

### 11.1 Archivo Actual Sentey (`sentey-gs-3910.device`)
```ini
[Device]
Name=Sentey GS-3910
DeviceMatch=usb:e0ff:0002
DeviceType=mouse
Driver=steelseries  # ← Temporal, cambiar a sentey

[Driver/steelseries]
DeviceVersion=2
Buttons=10
Leds=1
DpiList=2000;4200;6200;8200
```

### 11.2 Ejemplo Sinowealth (`sinowealth-0027.device`)
```ini
[Device]
DeviceMatch=usb:258a:0027
DeviceType=mouse
Driver=sinowealth
Name=SinoWealth Generic Mouse (0027)

[Driver/sinowealth/devices/3106]
Buttons=8
DeviceName=DreamMachines DM5 Blink
LedType=RGB
SensorType=PMW3389
```

**Formato recomendado para Sentey:**
```ini
[Device]
Name=Sentey Revolution Pro GS-3910
DeviceMatch=usb:e0ff:0002
DeviceType=mouse
Driver=sentey

[Driver/sentey]
Profiles=5
Buttons=10
Leds=9  # Matriz 3x3 RGB
DpiList=2000;4200;6200;8200
ReportRates=125;250;500;1000
```

---

## 12. UTILIDAD DE CARPETAS PARA DESARROLLO

### 12.1 Carpeta `doc/` - Documentación
**Contenido:**
- `conf.py.in`, `index.rst`, `dbus.rst`: Documentación Sphinx
- `meson.build`: Build de documentación

**Utilidad para desarrollo:**
- Generar documentación API con Sphinx
- Documentar interfaces D-Bus
- Crear manuales de usuario
- **Recomendación:** Actualizar con documentación del driver Sentey

### 12.2 Carpeta `test/` - Pruebas
**Contenido:**
- `test-context.c`, `test-device.c`, `test-util.c`: Tests unitarios C
- `data-parse-test.py`: Test de parsing de datos
- `python-black-check.sh`, `python-ruff-check.sh`: Linters Python
- `valgrind.suppressions`: Supresiones Valgrind

**Utilidad para desarrollo:**
- Ejecutar tests unitarios: `meson test`
- Verificar parsing de archivos .device
- Detectar memory leaks con Valgrind
- **Recomendación:** Agregar tests específicos para driver Sentey

### 12.3 Carpeta `tools/` - Herramientas de Desarrollo
**Contenido:**
- `ratbagc.py.in`, `ratbagctl.*`: Herramientas de línea de comandos
- `hidpp10-dump-page.c`, `hidpp20-dump-page.c`: Debug HID++
- `lur-command.c`: Comando LUR
- `toolbox.py`: Utilidades Python
- `check_scan_build.py`: Análisis estático

**Utilidad para desarrollo:**
- `ratbagctl`: Probar configuración del mouse
- `hidpp*-dump-page.c`: Inspeccionar comunicación HID
- `toolbox.py`: Utilidades para desarrollo
- **Recomendación:** Usar `ratbagctl` para probar el driver Sentey, crear dumps HID para debugging

**Comandos útiles:**
```bash
# Probar driver
./tools/ratbagctl --help
./tools/ratbagctl list
./tools/ratbagctl <device> info

# Debugging HID
gcc -o hid_dump tools/hidpp10-dump-page.c
./hid_dump /dev/hidrawX

# Tests
meson test
```

---

## 13. PLAN DE IMPLEMENTACIÓN PARA DRIVER SENTEY

### 13.1 Fase 1: Estructura Básica
- [ ] Crear `src/driver-sentey.c` con probe básico
- [ ] Actualizar `data/devices/sentey-gs-3910.device` con Driver=sentey
- [ ] Agregar a `src/meson.build`
- [ ] Compilar y verificar detección

### 13.2 Fase 2: Funcionalidades Core
- [ ] Implementar `sentey_write_profile()` (comando 0x0188)
- [ ] Implementar `sentey_write_button()` (comando 0x0185)
- [ ] Implementar `sentey_write_led()` (comandos 0x0186/0x0102)
- [ ] Implementar `sentey_commit()` con guardado

### 13.3 Fase 3: DPI y Testing
- [ ] Investigar comando DPI exacto
- [ ] Implementar `sentey_write_resolution_dpi()`
- [ ] Testing con `ratbagctl`
- [ ] Debugging con tools/

### 13.4 Fase 4: Optimizaciones
- [ ] Agregar `read_profile()` si es posible
- [ ] Manejo de errores robusto
- [ ] Logging detallado
- [ ] Tests unitarios

### Archivos a Crear/Modificar
- [ ] `/workspace/src/driver-sentey.c` - Driver principal
- [ ] `/workspace/src/meson.build` - Agregar driver al build
- [ ] `/workspace/data/devices/sentey-revolution-pro-gs-3910.device` - Archivo de dispositivo

### Pruebas
- [ ] Compilar sin errores
- [ ] Cargar driver con `ratbagd`
- [ ] Verificar detección con `ratbagctl list`
- [ ] Probar lectura/escritura de DPI
- [ ] Probar configuración de botones
- [ ] Probar configuración de LEDs
- [ ] Verificar persistencia de cambios

---

## 9. REFERENCIAS Y HERRAMIENTAS

### Herramientas de Depuración
```bash
# Listar dispositivos
ratbagctl list

# Información detallada
ratbagctl info <device>

# Monitorear eventos
ratbagctl watch

# Ver logs de ratbagd
journalctl -u ratbagd -f

# Ver comunicación HID raw
usbmon (requiere kernel module)
```

### Drivers de Referencia
- `driver-steelseries.c` - Bueno para dispositivos con protocolos simples
- `driver-etekcity.c` - Ejemplo con múltiples perfiles y macros
- `driver-logitech-g300.c` - Protocolo Logitech simple
- `driver-hidpp10.c` / `driver-hidpp20.c` - Protocolo HID++ de Logitech (complejo)

### Documentación
- Libratbag GitHub: https://github.com/libratbag/libratbag
- HID Specification: https://www.usb.org/hid
- Linux HIDRAW API: https://www.kernel.org/doc/html/latest/hid/hidraw.html

---

## 10. EJEMPLO DE ESTRUCTURA FINAL DEL DRIVER

```c
/*
 * Copyright © 2024 Tu Nombre.
 * SPDX-License-Identifier: MIT
 */

#include "config.h"
#include <assert.h>
#include <errno.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define SENTEY_NUM_PROFILES    5
#define SENTEY_NUM_DPI         4
#define SENTEY_NUM_BUTTONS     9
#define SENTEY_NUM_LEDS        1
#define SENTEY_REPORT_SIZE     64

// Estructuras específicas del dispositivo
union sentey_message {
    struct {
        uint8_t report_id;
        uint8_t command;
        uint8_t data[SENTEY_REPORT_SIZE - 2];
    } __attribute__((packed)) msg;
    uint8_t data[SENTEY_REPORT_SIZE];
};

// Funciones del driver
static int sentey_probe(struct ratbag_device *device);
static void sentey_remove(struct ratbag_device *device);
static int sentey_commit(struct ratbag_device *device);
static int sentey_write_dpi(struct ratbag_resolution *resolution);
static int sentey_write_buttons(struct ratbag_profile *profile);
static int sentey_write_led(struct ratbag_led *led);
static int sentey_write_profile(struct ratbag_profile *profile);

// Registro del driver
struct ratbag_driver sentey_driver = {
    .name = "Sentey",
    .id = "sentey",
    .probe = sentey_probe,
    .remove = sentey_remove,
    .commit = sentey_commit,
};
```

---

## NOTAS IMPORTANTES PARA EL DRIVER SENTEY

1. **Protocolo Propietario**: Basado en las capturas, el mouse usa un protocolo propietario con comandos específicos (`0x0188`, `0x0185`, etc.)

2. **Secuencia de Inicialización**: Según `Apertura del software.txt`, hay una secuencia inicial específica que debe replicarse

3. **Comandos RGB**: Los patrones `0x0186` y `0x0102` parecen estar relacionados con configuración de colores. Analizar `colores posibles perfil 1.txt` para entender el formato

4. **Botones**: Las asignaciones están documentadas en los archivos `A2-A10*.txt`. Mapear correctamente los códigos

5. **DPI**: Los 4 niveles (2000, 4200, 6200, 8200) deben configurarse según los patrones en `DPI*.txt`

6. **Write-Only**: Si el dispositivo no soporta lectura de configuración, marcar perfiles como `RATBAG_PROFILE_CAP_WRITE_ONLY`

7. **Tiempos de Espera**: Usar `msleep()` entre comandos si el dispositivo lo requiere (observar en capturas)

8. **Report ID**: Determinar si el dispositivo usa Report ID específico o `0x00`
