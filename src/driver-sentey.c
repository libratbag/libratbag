/*
 * Copyright © 2024 Libratbag Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Driver for Sentey Revolution Pro GS-3910 Gaming Mouse
 *
 * Especificaciones oficiales del dispositivo:
 * - Sensor: Avago ADNS-9800 (DNA S9800)
 * - DPI: 400/1600/3200/8200 CPI
 * - Polling Rate: 500/1000 Hz
 * - Aceleración: 30G
 * - Track Speed: 150 ips
 * - Frame Rate: 11750 FPS
 * - Botones: 11 físicos (9 programables)
 * - LEDs: Matriz 3x3 RGB (9 zonas)
 * - Perfiles: 5 (almacenados onboard)
 * - Peso: 170g neto, 220g bruto
 * - Dimensiones: 126 x 84 x 42mm
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
#define SENTEY_NUM_BUTTONS     11  /* 11 botones físicos totales */
#define SENTEY_NUM_LEDS        9   /* Matriz 3x3 RGB */

#define SENTEY_REPORT_SIZE     8

/* Comandos HID identificados del análisis de capturas */
#define SENTEY_CMD_PROFILE     0x0188
#define SENTEY_CMD_BUTTON      0x0185
#define SENTEY_CMD_LED_SET     0x0186
#define SENTEY_CMD_LED_GET     0x0102

/* Funciones de botones identificadas */
#define SENTEY_BUTTON_LEFT_CLICK   0x801e
#define SENTEY_BUTTON_RIGHT_CLICK  0x00ff
#define SENTEY_BUTTON_WHEEL_CLICK  0x00ff
#define SENTEY_BUTTON_BUTTON4      0x00ff
#define SENTEY_BUTTON_BUTTON5      0x00ff
#define SENTEY_BUTTON_SCROLL_UP    0x00ff
#define SENTEY_BUTTON_SCROLL_DOWN  0x00ff

/* Mapeo de botones físicos a índices del dispositivo */
static const uint8_t sentey_button_mapping[SENTEY_NUM_BUTTONS] = {
    0x01,  /* Botón físico 2 (A2) → índice 1 */
    0x02,  /* Botón físico 3 (A3) → índice 2 */
    0x04,  /* Botón físico 4 (A4) → índice 4 */
    0x03,  /* Botón físico 5 (A5) → índice 3 */
    0x05,  /* Botón físico 6 (A6) → índice 5 */
    0x06,  /* Botón físico 7 (A7) → índice 6 */
    0x07,  /* Botón físico 8 (A8) → índice 7 */
    0x08,  /* Botón físico 9 (A9) → índice 8 */
    0x09,  /* Botón físico 10 (A10) → índice 9 */
    0x00,  /* Botón físico 1 (click izquierdo) - no programable */
    0x0A,  /* Botón físico 11 (selector DPI) - modo especial */
};

/* Lista de DPI soportados */
static const unsigned int sentey_dpi_values[SENTEY_NUM_DPI] = {
    400, 1600, 3200, 8200
};

/* Lista de report rates soportados */
static const unsigned int sentey_report_rates[] = {
    500, 1000
};

struct sentey_data {
    uint8_t current_profile;
};

static int
sentey_test_hidraw(struct ratbag_device *device)
{
    return ratbag_hidraw_has_report(device, 0x0300);
}

static int
sentey_probe(struct ratbag_device *device)
{
    struct ratbag_profile *profile;
    struct ratbag_resolution *resolution;
    struct ratbag_button *button;
    struct ratbag_led *led;
    struct sentey_data *drv_data;
    int rc;

    log_debug(device->ratbag, "Probing Sentey GS-3910\n");

    /* Verificar que es un dispositivo HID compatible */
    rc = ratbag_find_hidraw(device, sentey_test_hidraw);
    if (rc) {
        log_error(device->ratbag, "Failed to find compatible HID interface\n");
        return rc;
    }

    /* Abrir el endpoint HID */
    rc = ratbag_open_hidraw_index(device, 0, 0);
    if (rc) {
        log_error(device->ratbag, "Failed to open HID interface: %s (%d)\n",
                  strerror(-rc), rc);
        return rc;
    }

    /* Inicializar perfiles */
    ratbag_device_init_profiles(device,
                                SENTEY_NUM_PROFILES,
                                SENTEY_NUM_DPI,
                                SENTEY_NUM_BUTTONS,
                                SENTEY_NUM_LEDS);

    /* Asignar datos específicos del driver */
    drv_data = zalloc(sizeof(*drv_data));
    drv_data->current_profile = 0;
    ratbag_set_drv_data(device, drv_data);

    /* Configurar cada perfil */
    ratbag_device_for_each_profile(device, profile) {
        profile->is_active = (profile->index == 0); /* Solo perfil 0 activo por defecto */

        /* Marcar como write-only ya que no podemos leer configuración actual */
        ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);

        /* Configurar report rates */
        ratbag_profile_set_report_rate_list(profile, sentey_report_rates,
                                            ARRAY_LENGTH(sentey_report_rates));
        profile->hz = 1000; /* Default 1000 Hz */

        /* Configurar resoluciones (DPI) */
        ratbag_profile_for_each_resolution(profile, resolution) {
            if (resolution->index == 0) {
                resolution->is_active = true;
                resolution->is_default = true;
            }

            /* Establecer lista de DPI soportados */
            ratbag_resolution_set_dpi_list(resolution, sentey_dpi_values,
                                           SENTEY_NUM_DPI);

            /* Valores por defecto */
            resolution->dpi_x = sentey_dpi_values[resolution->index];
            resolution->dpi_y = sentey_dpi_values[resolution->index];
        }

        /* Configurar botones */
        ratbag_profile_for_each_button(profile, button) {
            /* Habilitar tipos de acción soportados */
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);

            /* Configurar acciones por defecto según especificaciones oficiales */
            if (button->index == 0) {
                /* Botón 1: Click izquierdo (no programable) */
                button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                button->action.action.button = 1;
            } else if (button->index == 1) {
                /* Botón 2: Right click */
                button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                button->action.action.button = 2;
            } else if (button->index == 2) {
                /* Botón 3: Wheel click */
                button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                button->action.action.button = 3;
            } else if (button->index == 10) {
                /* Botón 11: DPI selector (modo especial) */
                button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
                button->action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP;
            } else {
                /* Otros botones programables: sin asignación por defecto */
                button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                button->action.action.button = button->index + 1;
            }
        }

        /* Configurar LEDs (matriz 3x3 RGB) */
        ratbag_profile_for_each_led(profile, led) {
            led->mode = RATBAG_LED_OFF;
            led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;

            /* Color por defecto: azul */
            led->color.red = 0;
            led->color.green = 0;
            led->color.blue = 255;

            /* Habilitar modos soportados */
            ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
            ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
            ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
            ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
        }
    }

    log_debug(device->ratbag, "Sentey GS-3910 probe successful\n");
    return 0;
}

static int
sentey_write_profile(struct ratbag_profile *profile)
{
    struct ratbag_device *device = profile->device;
    struct sentey_data *drv_data = ratbag_get_drv_data(device);
    uint8_t buf[SENTEY_REPORT_SIZE] = {0};
    int rc;

    log_debug(device->ratbag, "Writing profile %d\n", profile->index);

    /* Comando de selección de perfil: 01 88 XX 00 00 00 00 12 */
    buf[0] = 0x01;
    buf[1] = (SENTEY_CMD_PROFILE >> 8) & 0xFF;
    buf[2] = SENTEY_CMD_PROFILE & 0xFF;
    buf[3] = profile->index;  /* 0-4 para perfiles 1-5 */
    buf[4] = 0x00;
    buf[5] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0x12;

    rc = ratbag_hidraw_raw_request(device, 0x03, buf, sizeof(buf),
                                   HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
    if (rc < 0) {
        log_error(device->ratbag, "Failed to write profile: %s (%d)\n",
                  strerror(-rc), rc);
        return rc;
    }

    drv_data->current_profile = profile->index;
    return 0;
}

static int
sentey_write_button(struct ratbag_button *button)
{
    struct ratbag_device *device = button->profile->device;
    uint8_t buf[SENTEY_REPORT_SIZE] = {0};
    uint16_t button_function = 0;
    int rc;

    log_debug(device->ratbag, "Writing button %d\n", button->index);

    /* El botón 0 (índice 0) es el click izquierdo - no programable */
    if (button->index == 0)
        return 0;

    /* El botón 10 (índice 10) es el selector DPI - modo especial */
    if (button->index == 10) {
        log_debug(device->ratbag, "DPI selector button - special mode, skipping\n");
        return 0;
    }

    /* Solo botones configurables (índices 1-9, botones físicos 2-10) */
    if (button->index >= SENTEY_NUM_BUTTONS)
        return 0;

    /* Convertir acción a código de función del dispositivo */
    switch (button->action.type) {
    case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
        switch (button->action.action.button) {
        case 1: button_function = SENTEY_BUTTON_LEFT_CLICK; break;
        case 2: button_function = SENTEY_BUTTON_RIGHT_CLICK; break;
        case 3: button_function = SENTEY_BUTTON_WHEEL_CLICK; break;
        case 4: button_function = SENTEY_BUTTON_BUTTON4; break;
        case 5: button_function = SENTEY_BUTTON_BUTTON5; break;
        default:
            log_error(device->ratbag, "Unsupported button action: %d\n",
                      button->action.action.button);
            return -EINVAL;
        }
        break;
    case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
        switch (button->action.action.special) {
        case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:
            button_function = SENTEY_BUTTON_SCROLL_UP;
            break;
        case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:
            button_function = SENTEY_BUTTON_SCROLL_DOWN;
            break;
        default:
            log_error(device->ratbag, "Unsupported special action: %d\n",
                      button->action.action.special);
            return -EINVAL;
        }
        break;
    default:
        log_error(device->ratbag, "Unsupported action type: %d\n",
                  button->action.type);
        return -EINVAL;
    }

    /* Comando de configuración de botón: 01 85 00 BB CC DD EE 12 */
    buf[0] = 0x01;
    buf[1] = (SENTEY_CMD_BUTTON >> 8) & 0xFF;
    buf[2] = SENTEY_CMD_BUTTON & 0xFF;
    buf[3] = 0x00;
    buf[4] = sentey_button_mapping[button->index];  /* Índice del botón */
    buf[5] = (button_function >> 8) & 0xFF;         /* Código alto */
    buf[6] = button_function & 0xFF;                /* Código bajo */
    buf[7] = 0x12;

    rc = ratbag_hidraw_raw_request(device, 0x03, buf, sizeof(buf),
                                   HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
    if (rc < 0) {
        log_error(device->ratbag, "Failed to write button: %s (%d)\n",
                  strerror(-rc), rc);
        return rc;
    }

    return 0;
}

static int
sentey_write_led(struct ratbag_led *led)
{
    struct ratbag_device *device = led->profile->device;
    uint8_t buf[SENTEY_REPORT_SIZE] = {0};
    int rc;

    log_debug(device->ratbag, "Writing LED %d\n", led->index);

    /* Comando de configuración RGB: 01 86 AA BB CC DD EE FF */
    buf[0] = 0x01;
    buf[1] = (SENTEY_CMD_LED_SET >> 8) & 0xFF;
    buf[2] = SENTEY_CMD_LED_SET & 0xFF;
    buf[3] = led->index;                    /* Índice del LED (0-8) */
    buf[4] = led->color.red;                /* Componente rojo */
    buf[5] = led->color.green;              /* Componente verde */
    buf[6] = led->color.blue;               /* Componente azul */
    buf[7] = (led->mode == RATBAG_LED_OFF) ? 0x00 : 0xFF;  /* Modo */

    rc = ratbag_hidraw_raw_request(device, 0x03, buf, sizeof(buf),
                                   HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
    if (rc < 0) {
        log_error(device->ratbag, "Failed to write LED: %s (%d)\n",
                  strerror(-rc), rc);
        return rc;
    }

    return 0;
}

static int
sentey_write_resolution_dpi(struct ratbag_resolution *resolution)
{
    struct ratbag_device *device = resolution->profile->device;

    log_debug(device->ratbag, "Writing DPI %d for resolution %d\n",
              resolution->dpi_x, resolution->index);

    /* TODO: Implementar comando DPI cuando se identifique el patrón exacto */
    /* Por ahora, solo loggear que se cambió */
    log_info(device->ratbag, "DPI change requested: %d DPI (not yet implemented)\n",
             resolution->dpi_x);

    /* Comando DPI placeholder - necesita análisis adicional de capturas */
    /* Posible formato basado en otros drivers: algún comando con índice de DPI */

    return 0;  /* No error por ahora, pero no implementado */
}

static int
sentey_commit(struct ratbag_device *device)
{
    struct ratbag_profile *profile;
    struct ratbag_resolution *resolution;
    struct ratbag_button *button;
    struct ratbag_led *led;
    int rc = 0;

    log_debug(device->ratbag, "Committing changes to Sentey GS-3910\n");

    /* Procesar cada perfil */
    list_for_each(profile, &device->profiles, link) {
        if (!profile->dirty)
            continue;

        log_debug(device->ratbag, "Profile %d changed, writing\n", profile->index);

        /* Escribir perfil activo */
        if (profile->is_active) {
            rc = sentey_write_profile(profile);
            if (rc)
                return rc;
        }

        /* Escribir resoluciones (DPI) */
        ratbag_profile_for_each_resolution(profile, resolution) {
            if (!resolution->dirty)
                continue;

            rc = sentey_write_resolution_dpi(resolution);
            if (rc)
                return rc;
        }

        /* Escribir botones */
        ratbag_profile_for_each_button(profile, button) {
            if (!button->dirty)
                continue;

            rc = sentey_write_button(button);
            if (rc)
                return rc;
        }

        /* Escribir LEDs */
        ratbag_profile_for_each_led(profile, led) {
            if (!led->dirty)
                continue;

            rc = sentey_write_led(led);
            if (rc)
                return rc;
        }
    }

    /* TODO: Implementar comando de guardado si es necesario */
    log_debug(device->ratbag, "Sentey GS-3910 commit successful\n");
    return 0;
}

static void
sentey_remove(struct ratbag_device *device)
{
    struct sentey_data *drv_data = ratbag_get_drv_data(device);

    log_debug(device->ratbag, "Removing Sentey GS-3910\n");

    ratbag_close_hidraw_index(device, 0);

    if (drv_data)
        free(drv_data);

    ratbag_set_drv_data(device, NULL);
}

struct ratbag_driver sentey_driver = {
    .name = "Sentey",
    .id = "sentey",
    .probe = sentey_probe,
    .remove = sentey_remove,
    .commit = sentey_commit,
};
