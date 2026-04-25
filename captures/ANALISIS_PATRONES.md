# Análisis de Patrones de Comunicación - Mouse Sentey Revolution Pro GS-3910
## Vendor ID: e0ff, Product ID: 0002 (A..... G3)

---

## 1. INFORMACIÓN GENERAL DEL DISPOSITIVO

**Dispositivo:** Mouse gamer Sentey Revolution Pro GS-3910  
**Vendor ID:** 0xe0ff  
**Product ID:** 0x0002  
**Nombre USB:** "A..... G3"  
**Bus:** HID (Human Interface Device)  
**Interfaces HID:** 2 interfaces (hidraw3, hidraw4)  
**Report IDs:** 0x01, 0x02, 0x03  

**Problema actual:** Libratbag v0.18 lo asigna incorrectamente al driver `steelseries`  
**Driver correcto necesario:** Driver personalizado para protocolo Sentey/Sinowealth

---

## 2. PATRONES DE COMUNICACIÓN HID

### 2.1 Estructura General de Comandos

Todos los comandos siguen el patrón SET_REPORT/GET_REPORT con:
- **bmRequestType:** 0x21 (SET_REPORT), 0xa1 (GET_REPORT)
- **bRequest:** 0x09 (SET_REPORT), 0x01 (GET_REPORT)
- **wValue:** 0x0300 (Report ID 3)
- **wIndex:** 0x0000
- **wLength:** 8 bytes (payload de datos)

### 2.2 Formato del Payload de Datos (8 bytes)

```
Byte 0-1: Código de comando (01XX)
Byte 2-3: Parámetro 1 (índice, dirección, etc.)
Byte 4-5: Parámetro 2 (valor, configuración, etc.)
Byte 6-7: Sufijo fijo o checksum (ff00, ff12, etc.)
```

---

## 3. PATRONES ESPECÍFICOS POR FUNCIÓN

### 3.1 COMANDO DE PERFIL (0x0188)

**Propósito:** Cambiar entre los 5 perfiles del mouse

**Formato:** `01 88 XX 00 00 00 00 12`

Donde:
- `0188`: Código de comando de perfil
- `XX`: Índice del perfil (00-04)
- `00 00 00 00`: Relleno
- `12`: Sufijo fijo

**Patrones encontrados:**

| Perfil | Comando Hexadecimal | Acción |
|--------|---------------------|--------|
| Perfil 1 | `01 88 00 00 00 00 00 12` | Activar perfil 1 |
| Perfil 2 | `01 88 01 00 00 00 00 12` | Activar perfil 2 |
| Perfil 3 | `01 88 02 00 00 00 00 12` | Activar perfil 3 |
| Perfil 4 | `01 88 03 00 00 00 00 12` | Activar perfil 4 |
| Perfil 5 | `01 88 04 00 00 00 00 12` | Activar perfil 5 |

**Secuencia típica:**
```
HOST -> DEVICE: SET_REPORT wValue=0x0300 Data=0188000000000012 (Perfil 1)
DEVICE -> HOST: SET_REPORT Response (confirmación)
```

---

### 3.2 COMANDO DE ASIGNACIÓN DE BOTONES (0x0185)

**Propósito:** Configurar la función de cada botón del mouse

**Formato:** `01 85 XX YY ZZ WW VV 12`

Donde:
- `0185`: Código de comando de botón
- `XX`: Índice del botón (00-09)
- `YY ZZ`: Código de función del botón
- `WW VV`: Parámetros adicionales (depende de la función)
- `12`: Sufijo fijo

**Códigos de función de botones identificados:**

| Función | Código Hex | Bytes 2-3 | Bytes 4-5 | Observaciones |
|---------|-----------|-----------|-----------|---------------|
| LeftClick (predeterminado) | 0x801e | `80 1e` | `ff ff` | Click izquierdo estándar |
| RightClick | 0x00ff | `00 ff` | `ff ff` | Click derecho |
| WheelClick | 0x00ff | `00 ff` | `ff ff` | Click de rueda |
| Button4 (ATRAS) | 0x00ff | `00 ff` | `ff ff` | Botón lateral 1 |
| Button5 (ADELANTE) | 0x00ff | `00 ff` | `ff ff` | Botón lateral 2 |
| ScrollUp | 0x00ff | `00 ff` | `ff ff` | Scroll arriba |
| ScrollDown | 0x00ff | `00 ff` | `ff ff` | Scroll abajo |

**Patrones encontrados por botón:**

#### Botón A2 (Button 2):
- **LeftClick:** `01 85 00 01 80 1e ff 12` → Botón 1, función 0x801e (click izquierdo)
- **RightClick:** `01 85 00 01 00 ff ff 12` → Botón 1, función 0x00ff (click derecho)

#### Botón A3 (Button 3 - Wheel):
- **LeftClick:** `01 85 00 02 80 1e ff 12` → Botón 2, función 0x801e
- **WheelClick:** `01 85 00 02 00 ff ff 12` → Botón 2, función 0x00ff

#### Botón A4 (Button 4):
- **Button4:** `01 85 00 04 00 ff ff 12` → Botón 4, función 0x00ff (back)
- **LeftClick:** `01 85 00 04 80 1e ff 12` → Botón 4, función 0x801e

#### Botón A5 (Button 5):
- **Button5:** `01 85 00 03 00 ff ff 12` → Botón 3, función 0x00ff (forward)
- **LeftClick:** `01 85 00 03 80 1e ff 12` → Botón 3, función 0x801e

#### Botón A6 (Button 6):
- **LeftClick:** `01 85 00 05 80 1e ff 12` → Botón 5, función 0x801e

#### Botón A7 (Button 7):
- **LeftClick:** `01 85 00 06 80 1e ff 12` → Botón 6, función 0x801e

#### Botón A8 (Button 8):
- **LeftClick:** `01 85 00 07 80 1e ff 12` → Botón 7, función 0x801e

#### Botón A9 (Button 9):
- **ScrollUp:** `01 85 00 08 00 ff ff 12` → Botón 8, función 0x00ff (scroll up)

#### Botón A10 (Button 10):
- **ScrollDown:** `01 85 00 09 00 ff ff 12` → Botón 9, función 0x00ff (scroll down)

**Mapeo de índices de botones:**
```
Índice 0x01 → Botón físico 2 (Click izquierdo/derecho principal)
Índice 0x02 → Botón físico 3 (Rueda click)
Índice 0x03 → Botón físico 5 (Lateral 2 / Button5)
Índice 0x04 → Botón físico 4 (Lateral 1 / Button4)
Índice 0x05 → Botón físico 6
Índice 0x06 → Botón físico 7
Índice 0x07 → Botón físico 8
Índice 0x08 → Botón físico 9 (Scroll Up)
Índice 0x09 → Botón físico 10 (Scroll Down)
```

---

### 3.3 COMANDO DE ILUMINACIÓN RGB (0x0186 y 0x0102)

**Propósito:** Configurar colores LED del mouse

Se identificaron DOS tipos de comandos que se alternan:

#### Tipo A: Comando de establecimiento de color (0x0186)

**Formato:** `01 86 AA BB CC DD EE FF`

Donde:
- `0186`: Código de comando RGB
- `AA BB`: Coordenadas/índices de zona de color
- `CC DD`: Posiblemente brillo o canal adicional
- `EE FF`: Valor de color o sufijo (frecuentemente `ff 00`)

**Patrones observados:**
```
01 86 00 00 00 00 ff 00  → Zona 0,0, color ff00
01 86 00 01 00 00 ff 00  → Zona 0,1, color ff00
01 86 00 02 00 00 ff 00  → Zona 0,2, color ff00
01 86 00 00 01 00 ff 00  → Zona 1,0, color ff00
01 86 00 01 01 00 ff 00  → Zona 1,1, color ff00
01 86 00 02 01 00 ff 00  → Zona 1,2, color ff00
01 86 00 00 02 00 ff 00  → Zona 2,0, color ff00
...
```

**Interpretación:**
- Bytes 2-3 (`00 00`, `00 01`, `00 02`): Índice X de la zona LED (0-2)
- Bytes 4-5 (`00 00`, `01 00`, `02 00`): Índice Y de la zona LED (0-2)
- Bytes 6-7 (`ff 00`): Valor de color (posiblemente blanco en formato especial)

Esto sugiere una matriz de LEDs de 3x3 = 9 zonas configurables.

#### Tipo B: Comando de confirmación/lectura (0x0102)

**Formato:** `01 02 00 XX XX XX XX 12`

**Patrones observados:**
```
01 02 00 6a 03 00 00 12  → Respuesta tipo A
01 02 00 c0 3b ab 77 12 → Respuesta tipo B
```

**Interpretación:**
- `0102`: Código de respuesta/confirmación
- `00`: Byte fijo
- `6a 03 00 00` o `c0 3b ab 77`: Datos de estado/confirmación (varían)
- `12`: Sufijo fijo

Los valores `6a030000` y `c03bab77` parecen ser:
- Posiblemente hashes o checksums de la configuración
- O identificadores de paleta de colores predefinida
- Se alternan según el color/zona configurada

**Secuencia típica de configuración RGB:**
```
1. HOST -> DEVICE: SET_REPORT 018600000000ff00 (Configurar zona 0,0)
2. DEVICE -> HOST: SET_REPORT 0102006a03000012 (Confirmación tipo A)
3. HOST -> DEVICE: SET_REPORT 018600010000ff00 (Configurar zona 0,1)
4. DEVICE -> HOST: SET_REPORT 0102006a03000012 (Confirmación tipo A)
5. HOST -> DEVICE: SET_REPORT 018600020000ff00 (Configurar zona 0,2)
6. DEVICE -> HOST: SET_REPORT 0102006a03000012 (Confirmación tipo A)
7. HOST -> DEVICE: SET_REPORT 018600000100ff00 (Configurar zona 1,0)
8. DEVICE -> HOST: SET_REPORT 010200c03bab7712 (Confirmación tipo B - cambió)
...
```

---

### 3.4 COMANDO DE DPI (0x01??)

**Nota:** No se encontraron fragmentos de datos explícitos en las capturas de DPI1.txt - DPI4.txt con el formato "Data Fragment". 

Sin embargo, basándose en:
- La estructura similar a otros comandos
- Los 4 niveles de DPI documentados (2000, 4200, 6200, 8200)
- El patrón de otros dispositivos Sinowealth

**Hipótesis de formato DPI:**

Es probable que use un comando similar a:
```
01 XX AA BB CC DD EE FF
```

Donde:
- `01XX`: Código de comando DPI (posiblemente `0187` o similar)
- `AA`: Nivel de DPI (0-3 para los 4 niveles)
- `BB CC`: Valor X del DPI (little-endian)
- `DD EE`: Valor Y del DPI (little-endian, usualmente igual a X)
- `FF`: Sufijo/checksum

**Valores DPI documentados:**
```
Nivel 0: 2000 x 2000 DPI
Nivel 1: 4200 x 4200 DPI
Nivel 2: 6200 x 6200 DPI
Nivel 3: 8200 x 8200 DPI
```

**Comandos hipotéticos (por verificar con más capturas):**
```
Nivel 0: 01 87 00 D0 07 D0 07 12  (2000 = 0x07D0)
Nivel 1: 01 87 01 A4 10 A4 10 12  (4200 = 0x1068)
Nivel 2: 01 87 02 68 18 68 18 12  (6200 = 0x1838)
Nivel 3: 01 87 03 38 20 38 20 12  (8200 = 0x2008)
```

**RECOMENDACIÓN:** Capturar tráfico específico al cambiar DPI para confirmar el formato exacto.

---

## 4. SECUENCIA DE INICIALIZACIÓN (Apertura del Software)

Durante la apertura del software original, se observa la siguiente secuencia:

### 4.1 Enumeración USB inicial
```
1. GET DESCRIPTOR Request DEVICE (host -> device)
2. GET DESCRIPTOR Response DEVICE (device -> host)
   - bDeviceClass: 0x00 (definido en interface)
   - idVendor: 0xe0ff
   - idProduct: 0x0002
3. GET DESCRIPTOR Request CONFIGURATION
4. GET DESCRIPTOR Response CONFIGURATION
   - 2 interfaces HID reportadas
5. SET CONFIGURATION Request
6. SET CONFIGURATION Response
```

### 4.2 Handshake HID inicial
Patrón repetitivo observado múltiples veces:
```
[GET_REPORT Request]  (wValue=0x0300, wLength=8)
[GET_REPORT Response] (8 bytes de datos)
[SET_REPORT Request]  (wValue=0x0300, 16 bytes totales, 8 bytes payload)
[SET_REPORT Response]
```

Este patrón se repite ~20-30 veces durante la inicialización, posiblemente:
- Leyendo configuración actual del dispositivo
- Sincronizando estado entre software y hardware
- Verificando capacidades del dispositivo

---

## 5. ANÁLISIS DE CAPTURA LIBRATBAG (capturaLRB.txt)

### 5.1 Problemas identificados

```
ratbag debug: device assigned driver steelseries
ratbag raw: output report: 00 90 00 00 00 00 00 00 ... (comando SteelSeries)
ratbag raw: output report: 00 92 00 00 00 00 00 00 ... (comando SteelSeries)
ratbag error: Failed to read device settings
ratbag error: A..... G3: error opening hidraw node (Connection timed out)
```

**Diagnóstico:**
- Libratbag asigna incorrectamente el driver `steelseries`
- Envía comandos `00 90` y `00 92` (protocolo SteelSeries)
- El dispositivo no responde (timeout) porque espera comandos Sentey (`01 8X`)
- Necesita driver personalizado con protocolo Sentey

### 5.2 Reportes HID detectados
```
- HID report ID 01
- HID report ID 02
- HID report ID 03 ← Usado para configuración (wValue=0x0300)
```

---

## 6. RESUMEN DE CÓDIGOS DE COMANDO IDENTIFICADOS

| Código | Nombre | Función | Dirección |
|--------|--------|---------|-----------|
| `0x0188` | CMD_PROFILE | Cambiar perfil activo | Host -> Device |
| `0x0185` | CMD_BUTTON | Configurar asignación de botón | Host -> Device |
| `0x0186` | CMD_RGB_SET | Establecer color de zona LED | Host -> Device |
| `0x0102` | CMD_RGB_ACK | Confirmación/estado RGB | Device -> Host |
| `0x01??` | CMD_DPI | Configurar DPI (hipótesis) | Host -> Device |

---

## 7. RECOMENDACIONES PARA IMPLEMENTAR DRIVER EN LIBRATBAG

### 7.1 Estructura del driver

Crear archivo: `src/driver-sentey.c`

**Funciones principales necesarias:**
```c
// Probe: Detectar si el dispositivo es compatible
static int sentey_probe(struct libratbag_device *device);

// Lectura de perfil
static int sentey_read_profile(struct ratbag_profile *profile, unsigned int data);

// Escritura de perfil
static int sentey_write_profile(struct ratbag_profile *profile);

// Commit de cambios
static int sentey_commit(struct ratbag_device *device);

// Configuración de DPI
static int sentey_write_resolution_dpi(struct ratbag_resolution *resolution, int dpi);

// Configuración de botones
static int sentey_write_button(struct ratbag_button *button, 
                                const struct ratbag_button_action *action);

// Configuración de LEDs
static int sentey_write_led(struct ratbag_led *led, 
                            enum ratbag_led_mode mode,
                            struct ratbag_color color,
                            unsigned int ms);
```

### 7.2 Mapeo de operaciones a comandos HID

| Operación Libratbag | Comando Sentey | Formato |
|--------------------|----------------|---------|
| `write_profile()` | `0x0188` | `01 88 <idx> 00 00 00 00 12` |
| `write_button()` | `0x0185` | `01 85 <btn_idx> <func_hi> <func_lo> ff ff 12` |
| `write_led()` | `0x0186` + `0x0102` | Secuencia alterna por zona |
| `write_resolution_dpi()` | `0x01??` | Por determinar exactamente |

### 7.3 Mapeo de funciones de botones

```c
// Mapeo de acciones de libratbag a códigos Sentey
#define SENTAY_BTN_LEFT     0x801e
#define SENTAY_BTN_RIGHT    0x00ff
#define SENTAY_BTN_MIDDLE   0x00ff  // Wheel click
#define SENTAY_BTN_BACK     0x00ff  // Button4
#define SENTAY_BTN_FORWARD  0x00ff  // Button5
#define SENTAY_BTN_SCROLL_UP    0x00ff
#define SENTAY_BTN_SCROLL_DOWN  0x00ff

// Nota: Se necesita investigar más para diferenciar funciones con mismo código
// Posiblemente los bytes adicionales o el contexto determinan la función
```

### 7.4 Archivo .device necesario

Crear: `data/devices/sentey-revolution-pro-gs-3910.device`

```ini
[Device]
Name=Sentey Revolution Pro GS-3910
DeviceMatch=usb:e0ff:0002
Driver=sentey

[Features]
Profiles=5
Buttons=10
Leds=1  # o más, dependiendo de las zonas RGB
DpiRange=200:8200:50
```

---

## 8. PUNTOS CRÍTICOS A INVESTIGAR

1. **Comando exacto de DPI:** Se necesitan capturas específicas al cambiar DPI
2. **Diferenciación de funciones de botones:** Múltiples funciones comparten código `0x00ff`
3. **Número exacto de zonas LED:** La matriz parece ser 3x3 pero requiere confirmación
4. **Checksum/validación:** Determinar si hay checksum en los comandos o es byte fijo
5. **Persistencia:** Confirmar si los cambios se guardan inmediatamente o requieren comando de commit

---

## 9. HERRAMIENTAS ÚTILES PARA DEPURACIÓN

```bash
# Monitorear tráfico HID en tiempo real
sudo cat /dev/hidraw3 | hexdump -C

# Enviar comando HID manualmente (herramienta hidrd)
hidrd-convert -o spec /dev/hidraw3

# Probar driver con libratbag
sudo ratbagd --verbose=raw
ratbagctl list
ratbagctl <device> info
```

---

## 10. REFERENCIAS

- Protocolo similar a dispositivos Sinowealth (ej. PixArt PMW3360 basado)
- Documentación libratbag: https://github.com/libratbag/libratbag
- Especificación HID: https://www.usb.org/document-library/hid-111-class-definition-111

---

**Documento generado para asistencia en desarrollo de driver libratbag**
**Versión del análisis:** 1.0
**Basado en capturas de:** /workspace/captures/
