#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "pico/time.h"
#include "ff.h"
#include "hw_config.h"

// ==========================================
// 1. DEFINICIONES Y CONFIGURACIÓN DE PINES
// ==========================================
#define SPI_TFT_PORT spi0
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_RST  20
#define PIN_DC   21

#define ANCHO 320
#define ALTO  240

#define NEGRO   0x0000
#define VERDE   0x07E0
#define ROJO    0xF800

// ==========================================
// 2. VARIABLES GLOBALES (INTERRUPCIONES Y FSM)
// ==========================================
volatile bool flag_datos_listos = false;
volatile uint16_t lectura_adc_isr = 0;
volatile uint32_t tiempo_isr_ms = 0;

typedef enum {
    INIT_SYS,
    IDLE_MODE,
    ACQUISITION,
    ERROR_SYS
} EstadoSistema;

EstadoSistema estado_actual = INIT_SYS;

// ==========================================
// 3. SUBSISTEMA DE PROCESAMIENTO DIGITAL (DSP)
// ==========================================
#define VENTANA_FILTRO 10
uint16_t buffer_crudo[VENTANA_FILTRO] = {0};
uint8_t indice_filtro = 0;

void init_filtro() {
    for(int i=0; i<VENTANA_FILTRO; i++) buffer_crudo[i] = 2047;
}

uint16_t aplicar_filtro_media(uint16_t nuevo_dato) {
    buffer_crudo[indice_filtro] = nuevo_dato;
    indice_filtro = (indice_filtro + 1) % VENTANA_FILTRO;
    uint32_t suma = 0;
    for(int i=0; i<VENTANA_FILTRO; i++) suma += buffer_crudo[i];
    return (uint16_t)(suma / VENTANA_FILTRO);
}

// ==========================================
// 4. SUBSISTEMA DE INTERFAZ GRÁFICA (TFT)
// ==========================================
void tft_enviar_comando(uint8_t cmd) {
    gpio_put(PIN_CS, 0); gpio_put(PIN_DC, 0);
    spi_write_blocking(SPI_TFT_PORT, &cmd, 1);
    gpio_put(PIN_CS, 1);
}

void tft_enviar_dato(uint8_t data) {
    gpio_put(PIN_CS, 0); gpio_put(PIN_DC, 1);
    spi_write_blocking(SPI_TFT_PORT, &data, 1);
    gpio_put(PIN_CS, 1);
}

void tft_init() {
    gpio_put(PIN_RST, 1); sleep_ms(50);
    gpio_put(PIN_RST, 0); sleep_ms(50);
    gpio_put(PIN_RST, 1); sleep_ms(150);
    tft_enviar_comando(0x01); sleep_ms(150);
    tft_enviar_comando(0x11); sleep_ms(250);
    tft_enviar_comando(0x36); tft_enviar_dato(0x28); // Landscape
    tft_enviar_comando(0x3A); tft_enviar_dato(0x55); // 16-bit
    tft_enviar_comando(0x29); sleep_ms(150);
}

void tft_dibujar_linea_v(uint16_t x, uint16_t y1, uint16_t y2, uint16_t color) {
    if(x >= ANCHO) return;
    if(y1 > y2) { uint16_t temp=y1; y1=y2; y2=temp; }
    if(y2 >= ALTO) y2 = ALTO - 1;

    tft_enviar_comando(0x2A);
    tft_enviar_dato(x >> 8); tft_enviar_dato(x & 0xFF);
    tft_enviar_dato(x >> 8); tft_enviar_dato(x & 0xFF);

    tft_enviar_comando(0x2B);
    tft_enviar_dato(y1 >> 8); tft_enviar_dato(y1 & 0xFF);
    tft_enviar_dato(y2 >> 8); tft_enviar_dato(y2 & 0xFF);

    tft_enviar_comando(0x2C);
    uint8_t alto = color >> 8; uint8_t bajo = color & 0xFF;
    
    gpio_put(PIN_CS, 0); gpio_put(PIN_DC, 1);
    uint32_t pixeles = (y2 - y1) + 1;
    for(uint32_t i=0; i<pixeles; i++) {
        spi_write_blocking(SPI_TFT_PORT, &alto, 1);
        spi_write_blocking(SPI_TFT_PORT, &bajo, 1);
    }
    gpio_put(PIN_CS, 1);
}

void tft_limpiar_pantalla(uint16_t color) {
    for(uint16_t x=0; x<ANCHO; x++) tft_dibujar_linea_v(x, 0, ALTO-1, color);
}

uint16_t map_adc_to_screen(uint16_t valor_adc) {
    return (uint16_t)( (ALTO-1) - (valor_adc * (ALTO-1) / 4095) );
}

// ==========================================
// 5. RUTINA DE INTERRUPCIÓN DE HARDWARE (ISR)
// ==========================================
// Esta función se ejecuta automáticamente en "background" por hardware
bool timer_adquisicion_callback(struct repeating_timer *t) {
    lectura_adc_isr = adc_read();
    tiempo_isr_ms = to_ms_since_boot(get_absolute_time());
    flag_datos_listos = true;
    return true; // Continuar repitiendo
}

// ==========================================
// 6. PROGRAMA PRINCIPAL (MAIN)
// ==========================================
int main() {
    stdio_init_all();
    
    // Variables para el almacenamiento
    sd_card_t *pSD = NULL;
    FIL archivo_ecg;
    uint32_t contador_sync = 0;

    // Variables para la gráfica
    uint16_t x = 0;
    uint16_t prev_y_filtrada = ALTO / 2;
    uint16_t buffer_y_filtrada[ANCHO];
    for(int i=0; i<ANCHO; i++) buffer_y_filtrada[i] = ALTO/2;

    struct repeating_timer timer;

    while (1) {
        switch (estado_actual) {

            // ----------------------------------------------------
            case INIT_SYS:
                printf("\n[FSM] Estado: INIT_SYS -> Inicializando hardware...\n");
                
                // 1. Iniciar SPI0 (Pantalla)
                spi_init(SPI_TFT_PORT, 31000 * 1000);
                gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
                gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
                gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);
                gpio_init(PIN_DC); gpio_set_dir(PIN_DC, GPIO_OUT);
                gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
                
                tft_init();
                tft_limpiar_pantalla(NEGRO);
                init_filtro();

                // 2. Iniciar ADC (ECG)
                adc_init();
                adc_gpio_init(26);
                adc_select_input(0);

                // 3. Iniciar MicroSD y FatFS (SPI1)
                pSD = sd_get_by_num(0);
                FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
                if (fr != FR_OK) {
                    printf("[ERROR] Fallo al montar la MicroSD. Deteniendo sistema.\n");
                    estado_actual = ERROR_SYS;
                    break;
                }
                
                fr = f_open(&archivo_ecg, "0:registro_paciente.csv", FA_WRITE | FA_CREATE_ALWAYS);
                if (fr == FR_OK) {
                    // Usamos PUNTO Y COMA (;) para evitar conflictos con Excel en español
                    f_printf(&archivo_ecg, "Tiempo_ms;ECG_Crudo;ECG_Filtrado\n");
                    printf("[INFO] Archivo CSV creado exitosamente.\n");
                    estado_actual = IDLE_MODE;
                } else {
                    printf("[ERROR] No se pudo crear el archivo CSV.\n");
                    estado_actual = ERROR_SYS;
                }
                break;

            // ----------------------------------------------------
            case IDLE_MODE:
                printf("[FSM] Estado: IDLE_MODE -> Iniciando interrupción Timer a 200Hz\n");
                // Configurar el Timer por hardware para dispararse cada 5ms (200 muestras por segundo)
                add_repeating_timer_ms(-5, timer_adquisicion_callback, NULL, &timer);
                estado_actual = ACQUISITION;
                break;

            // ----------------------------------------------------
            case ACQUISITION:
                // POLLING: El procesador espera pacientemente aquí hasta que la ISR levanta la bandera
                if (flag_datos_listos) {
                    flag_datos_listos = false; // Bajar la bandera inmediatamente

                    // Extraer datos de la interrupción de forma segura
                    uint16_t crudo_local = lectura_adc_isr;
                    uint32_t tiempo_local = tiempo_isr_ms;

                    // 1. Procesamiento DSP
                    uint16_t filtrado_local = aplicar_filtro_media(crudo_local);
                    uint16_t y_cruda_scr = map_adc_to_screen(crudo_local);
                    uint16_t y_filtrada_scr = map_adc_to_screen(filtrado_local);

                    // 2. Actualización de Interfaz Gráfica (SPI0)
                    tft_dibujar_linea_v(x, 0, ALTO-1, NEGRO);
                    if(x + 1 < ANCHO) tft_dibujar_linea_v(x+1, 0, ALTO-1, NEGRO);
                    if(x + 2 < ANCHO) tft_dibujar_linea_v(x+2, 0, ALTO-1, NEGRO);

                    if(x > 0) {
                        tft_dibujar_linea_v(x-1, buffer_y_filtrada[x-1], prev_y_filtrada, VERDE);
                        tft_dibujar_linea_v(x-2, buffer_y_filtrada[x-2], buffer_y_filtrada[x-1], VERDE);
                    }
                    tft_dibujar_linea_v(x, y_cruda_scr, y_cruda_scr, ROJO);
                    tft_dibujar_linea_v(x, y_filtrada_scr, y_filtrada_scr, VERDE);

                    buffer_y_filtrada[x] = y_filtrada_scr;
                    prev_y_filtrada = y_filtrada_scr;
                    x++;
                    if (x >= ANCHO) x = 0;

                    // 3. Almacenamiento en Memoria (SPI1)
                    f_printf(&archivo_ecg, "%lu;%u;%u\n", tiempo_local, crudo_local, filtrado_local);
                    
                    // Obligar a la SD a guardar físicamente los datos cada 100 muestras (cada 0.5 seg)
                    contador_sync++;
                    if (contador_sync >= 100) {
                        f_sync(&archivo_ecg);
                        contador_sync = 0;
                    }
                }
                break;

            // ----------------------------------------------------
            case ERROR_SYS:
                // Estado seguro por si ocurre un fallo crítico de hardware
                tft_limpiar_pantalla(ROJO);
                while(1) {
                    sleep_ms(1000); // Congelar el sistema de forma segura
                }
                break;
        }
    }
    return 0;
}