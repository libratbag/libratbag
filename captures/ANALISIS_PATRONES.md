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

### 1.1 Especificaciones Técnicas Oficiales
**Sensor:** Avago ADNS-9800 (DNA S9800) - Sensor láser de alta precisión  
**DPI Máximo:** 8200 DPI  
**Niveles DPI:** 4 niveles preset: 400/1600/3200/8200 CPI  
**Polling Rate:** 500-1000 Hz (seleccionable por software)  
**Aceleración:** 30G  
**Track Speed:** 150 pulgadas/segundo  
**Frame Rate:** 11750 FPS  
**Tecnología:** Laser-Stream con IAS + DSP  

**Iluminación:**  
- 5 colores base + 26 tonos para indicador DPI  
- Matriz LED 3x3 (9 zonas RGB)  
- Indicador de perfil activo  

**Perfiles:** 5 perfiles independientes almacenados en memoria onboard  
**Botones:** 11 físicos (1 izquierdo + 10 configurables), 9 programables vía software  
**Peso:** 170g neto, 220g bruto (con pesos ajustables)  
**Dimensiones:** 126 x 84 x 42mm  
**Cable:** 1.8m trenzado de alta resistencia, USB Full-Speed (12 Mbps)  
**Pies:** Cerámica con coeficiente de fricción bajo, punto de fusión 2715°C  

---

## 1.2 Especificaciones USB/HID Oficiales

**Interface:** USB 2.0 Full-Speed (12 Mbps)  
**Conector:** USB Tipo-A chapado en oro  
**Cable:** 1.8 metros trenzado de alta resistencia  
**Protocolo:** HID (Human Interface Device)  
**Controller:** Desconocido (posiblemente Genius/A..... G3)  

**Estructura HID Report Esperada:**
- Report ID 0x01: Movimiento del mouse (X, Y, Wheel, Buttons)
- Report ID 0x02: Reportes de características (DPI, perfil, LED)
- Report ID 0x03: Configuración de macros/botones programables

**Configuración de Botones (HID):**
| Botón | Función | Programable |
|-------|---------|-------------|
| Click izquierdo | Principal | ❌ |
| Click derecho | Secundario | ❌ |
| Rueda (click) | Medio | ✅ |
| Rueda (scroll) | Eje Z | ❌ |
| Botón DPI | Cambio de sensibilidad | ✅ (modo) |
| Botón lateral 1 | Atrás (Back) | ✅ |
| Botón lateral 2 | Adelante (Forward) | ✅ |
| Botón modo | Cambio de perfil/LED | ✅ |
| Botón lift | Ajuste de altura | ✅ |
| Extra 1-3 | Personalizables | ✅ |

**Iluminación y Perfiles:**
- 5 perfiles de usuario independientes, seleccionables por LED de color
- 26 colores + 1 modo de iluminación LED
- Los perfiles se almacenan en memoria interna del mouse (on-board memory)
- Indicador LED muestra nivel DPI actual en 26 tonos diferentes

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

**Formato real observado:** `01 85 00 BB CC DD EE 12`

Donde:
- `01 85`: comando de configuración de botón
- Byte 2 = `00` (constante)
- Byte 3 = `BB` Índice de botón en el dispositivo (01..09)
- Byte 4-5 = Código de función o acción
- Byte 6 = Parámetro/relleno (`ff` en todas las capturas)
- Byte 7 = `12` sufijo fijo

**Patrones encontrados por botón:**

#### Botón A2 (Button 2):
- **LeftClick:** `01 85 00 01 80 1e ff 12` → índice 0x01, función 0x801e (click izquierdo)
- **RightClick:** `01 85 00 01 00 ff ff 12` → índice 0x01, función 0x00ff (click derecho)

#### Botón A3 (Button 3 - Wheel):
- **LeftClick:** `01 85 00 02 80 1e ff 12` → índice 0x02, función 0x801e
- **WheelClick:** `01 85 00 02 00 ff ff 12` → índice 0x02, función 0x00ff

#### Botón A4 (Button 4):
- **Button4:** `01 85 00 04 00 ff ff 12` → índice 0x04, función 0x00ff (back)
- **LeftClick:** `01 85 00 04 80 1e ff 12` → índice 0x04, función 0x801e

#### Botón A5 (Button 5):
- **Button5:** `01 85 00 03 00 ff ff 12` → índice 0x03, función 0x00ff (forward)
- **LeftClick:** `01 85 00 03 80 1e ff 12` → índice 0x03, función 0x801e

#### Botón A6 (Button 6):
- **LeftClick:** `01 85 00 05 80 1e ff 12` → índice 0x05, función 0x801e

#### Botón A7 (Button 7):
- **LeftClick:** `01 85 00 06 80 1e ff 12` → índice 0x06, función 0x801e

#### Botón A8 (Button 8):
- **LeftClick:** `01 85 00 07 80 1e ff 12` → índice 0x07, función 0x801e

#### Botón A9 (Button 9):
- **ScrollUp:** `01 85 00 08 00 ff ff 12` → índice 0x08, función 0x00ff (scroll up)

#### Botón A10 (Button 10):
- **ScrollDown:** `01 85 00 09 00 ff ff 12` → índice 0x09, función 0x00ff (scroll down)

**Observación importante:**
- El valor `0x00ff` aparece para varias acciones de botones laterales y rueda. Esto sugiere que la acción completa puede depender del índice del botón, no solo del código de función.
- En todas las capturas de botón, el byte 2 se mantiene en `00` y el byte 7 en `12`.

**Mapeo de índices de botones capturados:**
```
Botón físico 1: Click izquierdo (no programable)
Botón físico 2 (A2): Índice 0x01 → Click izquierdo/derecho
Botón físico 3 (A3): Índice 0x02 → Click izquierdo/rueda
Botón físico 4 (A4): Índice 0x04 → Click izquierdo/botón 4
Botón físico 5 (A5): Índice 0x03 → Click izquierdo/botón 5
Botón físico 6 (A6): Índice 0x05 → Click izquierdo
Botón físico 7 (A7): Índice 0x06 → Click izquierdo
Botón físico 8 (A8): Índice 0x07 → Click izquierdo
Botón físico 9 (A9): Índice 0x08 → Scroll up
Botón físico 10 (A10): Índice 0x09 → Scroll down
Botón físico 11: Selector DPI (modo especial)

Total: 11 botones físicos, 9 programables vía software
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

**Nota de captura:** Las grabaciones `DPI1.txt` a `DPI4.txt` muestran secuencias HID `GET_REPORT` y `SET_REPORT` alrededor del cambio de DPI, pero el texto exportado no incluye la línea `Data Fragment:` con el contenido de los 8 bytes de payload. Por tanto, el comando DPI exacto no puede confirmarse directamente con estas exports.

**Lo que se puede afirmar con seguridad:**
- El dispositivo sigue usando el mismo transporte HID `SET_REPORT` para escribir la configuración.
- Las solicitudes de `SET_REPORT` tienen 44 bytes en el bus, lo que coincide con un bloque de 8 bytes de payload más encabezados USB.
- El flujo observado es:
  1. `GET_REPORT` (lectura de estado/estado actual)
  2. `SET_REPORT` (escritura de nueva configuración)
  3. `GET_REPORT` (lectura de confirmación)

**Hipótesis reforzada:**
El comando DPI probablemente también es un bloque de 8 bytes empezando con `01`, posiblemente `0187` o `0189`, con:
```
01 XX YY YY ZZ ZZ 12
```
Donde `XX` podría seleccionar el nivel (0..3) y `YY YY` / `ZZ ZZ` codifican los valores X/Y de DPI.

**Niveles documentados del dispositivo:**
```
Nivel 0: 400 x 400 DPI   (anteriormente asumido 2000)
Nivel 1: 1600 x 1600 DPI (anteriormente asumido 4200)
Nivel 2: 3200 x 3200 DPI (anteriormente asumido 6200)
Nivel 3: 8200 x 8200 DPI (anteriormente asumido 8200 - correcto)
```

**Recomendación:**
- Reexportar o capturar el tráfico con un modo que conserve el contenido de los informes de características.
- Buscar explícitamente los datos del `SET_REPORT` en el flujo DPI, ya que las capturas actuales solo muestran la presencia de la transacción, no su payload.

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
