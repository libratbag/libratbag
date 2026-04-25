# Mouse Sentey Revolution Pro GS-3910 - Descripción Detallada

## **Tecnología y Rendimiento**

El **Revolution Pro GS-3910** es un mouse gaming de la serie profesional que incorpora tecnología de punta para jugadores exigentes:

### **Sensor y Precisión**
- **Chipset Avago 9800** (ADNS 9800): Sensor láser de alta precisión
- **Resolución hasta 8200 DPI**: Máxima sensibilidad configurable
- **Tecnología Laser-Stream**: Sistema avanzado que combina un sistema de adquisición de imágenes (IAS) con procesador de señal digital (DSP) utilizando un puerto serial cuádruple. Este sistema captura imágenes microscópicas de la superficie a través de una lente y sistema de iluminación, mientras el DSP calcula la dirección y distancia del movimiento optimizado
- **Frame Rate de 11750 FPS**: Captura ultra rápida de movimientos
- **Polling Rate de 1000Hz**: Respuesta inmediata
- **Track Speed de 150 pulgadas/segundo**: Velocidad de seguimiento excepcional
- **Aceleración de 30G**: Soporte para movimientos rápidos y precisos

### **Configuración de DPI**
- **4 niveles preset de DPI**: 400/1600/3200/8200 CPI
- **Botón selector de DPI**: Ajuste hardware instantáneo incluso durante el juego
- **Indicador LED**: Muestra el nivel de sensibilidad actual en 26 tonos diferentes según el DPI seleccionado
- **Configuración por software**: Control total desde la aplicación

## **Diseño y Construcción**

### **Acabados**
- **Superior en negro mate**: Acabado de alta calidad en todo el producto
- **Conector USB bañado en oro**: Mejor conductividad y durabilidad
- **Cable trenzado de alta resistencia**: 1.8 metros de longitud, diseñado para soportar uso intensivo

### **Sistema de Peso Personalizable**
- **6 pesos ajustables**: Cada uno de 4.9 gramos (29.4 gramos en total)
- **Control total**: Permite ajustar el peso según preferencias personales para mejorar resistencia y habilidad en el juego

### **Pies de Cerámica**
- **Bajo coeficiente de fricción**: Movimiento suave y continuo en diversas superficies
- **Alta densidad y tenacidad**: Extremadamente resistentes
- **Punto de fusión de 2715°C**: Prácticamente indestructibles
- **Material "acero cerámico":** Durabilidad excepcional

## **Botones y Programación**

### **Configuración de Botones**
- **9 botones + selector de DPI**: Control total
- **10 botones configurables vía software**: Personalización completa
- **7 botones macro**: Programación de comandos complejos
- **Ejes X e Y independientes y programables**: Control preciso

### **Perfiles**
- **5 perfiles configurables**: Guarda diferentes configuraciones para distintos juegos o usuarios

## **Iluminación**

- **LED con 5 colores diferentes**: Personalización estética
- **Indicador de DPI**: Retroiluminación que cambia según el nivel de sensibilidad seleccionado

## **Especificaciones Físicas**

- **Dimensiones**: 126 x 84 x 42mm
- **Peso neto**: 170 gramos (sin pesos adicionales)
- **Peso bruto**: 220 gramos
- **Interface**: USB 2.0
- **Scrolling**: 4D 4 Way Scrolling (desplazamiento en 4 direcciones)

## **Software y Accesorios**

### **Incluye**
- Software/Driver para configuración avanzada
- 6 pesos de ajuste (4.9g cada uno)
- Manual y guía de usuario
- Conector USB 2.0

### **Garantía**
- **1 año de garantía**

---

Basado en el análisis del datasheet y la información técnica disponible, aquí tienes las especificaciones del **Sentey Revolution Pro** relacionadas con su funcionamiento USB/HID:

## 🖱️ Sentey Revolution Pro - Especificaciones Técnicas USB/HID

### **Chipset y Sensor**
- **Chipset**: Avago ADNS-9800 (DNA S9800) [[18]]
- **DPI máximo**: 8200 DPI ajustables en 4 niveles: 400/1600/3200/8200 [[23]]
- **Tasa de sondeo**: 500-1000 Hz seleccionable por software [[26]]
- **Aceleración**: 30G [[25]]

### **Configuración de Botones (HID)**
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

**Total**: 11 botones físicos, 9 programables vía software [[12]]

### **Iluminación y Perfiles**
- **5 perfiles de usuario** independientes, seleccionables por LED de color [[12]]
- **26 colores + 1 modo** de iluminación LED [[12]]
- Los perfiles se almacenan en memoria interna del mouse (on-board memory)

### **Comunicación USB**
```
Interface: USB 2.0 Full-Speed (12 Mbps)
Conector: USB Tipo-A chapado en oro
Cable: 1.8 metros trenzado de alta resistencia
Protocolo: HID (Human Interface Device)
```

### **Estructura HID Report Esperada**
Basado en los paquetes USB que analizaste anteriormente, este mouse probablemente utiliza:

```
Report ID 0x01: Movimiento del mouse (X, Y, Wheel, Buttons)
Report ID 0x02: Reportes de características (DPI, perfil, LED)
Report ID 0x03: Configuración de macros/botones programables
```

Los paquetes `SET_REPORT` con `wValue: 0x0300` que observaste en tus capturas corresponden a reportes de tipo **Feature (3)** usados para:
- Cambiar perfil activo
- Ajustar configuración de DPI
- Controlar iluminación RGB
- Programar macros en botones

### **Notas para análisis de tráfico USB**
1. Los cambios de perfil/LED generan tráfico `SET_REPORT` hacia el endpoint de control (0x00)
2. Los movimientos del mouse usan reportes de entrada por endpoint de interrupción (típicamente 0x81)
3. Los valores de DPI se codifican en los bytes de datos del Feature Report (ej: `018600010000ff00` donde el byte 4 indica el índice de DPI)

### **informacion de chip genius**

Descripcion Dispositovo compuesto USB(A..... G3)
Deice Type: Human Interface Device

Protocal Version: USB 1.10
Current Speed: FULL SPEED
MAX CUrrent: 100mA

USB DEVICE ID: VID = E0FF PID = 0002

DEVICE VENDOR: A......
DEVICE NAME: G3
DEVICE REVISION: 0001

CONTROLLER PART-NUMBER: UNKNOWN