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

// Colores en formato RGB565
#define NEGRO    0x0000
#define VERDE    0x07E0 // Para el ECG
#define AMARILLO 0xFFE0 // Para el PCG (Sonido)
#define ROJO     0xF800 // Para errores

// ==========================================
// 2. VARIABLES GLOBALES (INTERRUPCIONES Y FSM)
// ==========================================
volatile bool flag_datos_listos = false;
volatile uint16_t lectura_ecg_isr = 0;
volatile uint16_t lectura_pcg_isr = 0;
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
// Filtro para ECG (Señal lenta: ventana de 10)
#define VENTANA_ECG 10
uint16_t buffer_ecg[VENTANA_ECG] = {0};
uint8_t indice_ecg = 0;

// Filtro para PCG (Señal acústica rápida: ventana de 3)
#define VENTANA_PCG 3
uint16_t buffer_pcg[VENTANA_PCG] = {0};
uint8_t indice_pcg = 0;

void init_filtros() {
    for(int i=0; i<VENTANA_ECG; i++) buffer_ecg[i] = 2047;
    for(int i=0; i<VENTANA_PCG; i++) buffer_pcg[i] = 2047;
}

uint16_t aplicar_filtro_ecg(uint16_t nuevo_dato) {
    buffer_ecg[indice_ecg] = nuevo_dato;
    indice_ecg = (indice_ecg + 1) % VENTANA_ECG;
    uint32_t suma = 0;
    for(int i=0; i<VENTANA_ECG; i++) suma += buffer_ecg[i];
    return (uint16_t)(suma / VENTANA_ECG);
}

uint16_t aplicar_filtro_pcg(uint16_t nuevo_dato) {
    buffer_pcg[indice_pcg] = nuevo_dato;
    indice_pcg = (indice_pcg + 1) % VENTANA_PCG;
    uint32_t suma = 0;
    for(int i=0; i<VENTANA_PCG; i++) suma += buffer_pcg[i];
    return (uint16_t)(suma / VENTANA_PCG);
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

// Inicialización EXACTA de tu código funcional (sin 0x21)
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

// Mapeo matemático para Pantalla Dividida (Split-Screen)
uint16_t map_ecg_to_screen(uint16_t valor_adc) {
    // Mitad superior: Límites de Y de 0 a 115
    return (uint16_t)( 115 - (valor_adc * 115 / 4095) );
}

uint16_t map_pcg_to_screen(uint16_t valor_adc) {
    // Mitad inferior: Límites de Y de 125 a 239
    return (uint16_t)( 239 - (valor_adc * 114 / 4095) );
}

// ==========================================
// 5. RUTINA DE INTERRUPCIÓN DE HARDWARE (ISR)
// ==========================================
// Multiplexación secuencial a 200Hz
bool timer_adquisicion_callback(struct repeating_timer *t) {
    adc_select_input(0); // Canal ECG (GPIO 26)
    lectura_ecg_isr = adc_read();
    
    adc_select_input(1); // Canal MIC (GPIO 27)
    lectura_pcg_isr = adc_read();
    
    tiempo_isr_ms = to_ms_since_boot(get_absolute_time());
    flag_datos_listos = true;
    return true; 
}

// ==========================================
// 6. PROGRAMA PRINCIPAL (MAIN)
// ==========================================
int main() {
    stdio_init_all();
    
    sd_card_t *pSD = NULL;
    FIL archivo_master;
    uint32_t contador_sync = 0;

    uint16_t x = 0;
    
    // Buffers para continuidad de trazo gráfico (estilo grueso de tu código original)
    uint16_t prev_y_ecg = 60;
    uint16_t prev_y_pcg = 180;
    uint16_t buffer_y_ecg[ANCHO];
    uint16_t buffer_y_pcg[ANCHO];
    
    for(int i=0; i<ANCHO; i++) {
        buffer_y_ecg[i] = 60;
        buffer_y_pcg[i] = 180;
    }

    struct repeating_timer timer;

    while (1) {
        switch (estado_actual) {

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
                init_filtros();

                // 2. Iniciar ADC (ECG y MIC)
                adc_init();
                adc_gpio_init(26);
                adc_gpio_init(27);

                // 3. Iniciar MicroSD y FatFS (SPI1)
                pSD = sd_get_by_num(0);
                FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
                if (fr != FR_OK) {
                    printf("[ERROR] Fallo al montar la MicroSD. Deteniendo sistema.\n");
                    estado_actual = ERROR_SYS;
                    break;
                }
                
                fr = f_open(&archivo_master, "0:registro_paciente.csv", FA_WRITE | FA_CREATE_ALWAYS);
                if (fr == FR_OK) {
                    f_printf(&archivo_master, "Tiempo_ms;ECG_Crudo;ECG_Filtrado;PCG_Filtrado\n");
                    printf("[INFO] Archivo CSV creado exitosamente.\n");
                    estado_actual = IDLE_MODE;
                } else {
                    printf("[ERROR] No se pudo crear el archivo CSV.\n");
                    estado_actual = ERROR_SYS;
                }
                break;

            case IDLE_MODE:
                printf("[FSM] Estado: IDLE_MODE -> Iniciando interrupción Timer a 200Hz\n");
                add_repeating_timer_ms(-5, timer_adquisicion_callback, NULL, &timer);
                estado_actual = ACQUISITION;
                break;

            case ACQUISITION:
                if (flag_datos_listos) {
                    flag_datos_listos = false; 

                    uint16_t ecg_crudo_local = lectura_ecg_isr;
                    uint16_t pcg_crudo_local = lectura_pcg_isr;
                    uint32_t tiempo_local = tiempo_isr_ms;

                    // 1. Procesamiento DSP
                    uint16_t ecg_filtrado = aplicar_filtro_ecg(ecg_crudo_local);
                    uint16_t pcg_filtrado = aplicar_filtro_pcg(pcg_crudo_local);

                    // 2. Mapeo a Pantalla Dividida
                    uint16_t y_ecg_scr = map_ecg_to_screen(ecg_filtrado);
                    uint16_t y_pcg_scr = map_pcg_to_screen(pcg_filtrado);

                    // 3. Borrado Dinámico
                    tft_dibujar_linea_v(x, 0, ALTO-1, NEGRO);
                    if(x + 1 < ANCHO) tft_dibujar_linea_v(x+1, 0, ALTO-1, NEGRO);
                    if(x + 2 < ANCHO) tft_dibujar_linea_v(x+2, 0, ALTO-1, NEGRO);

                    // 4. Dibujo de Señales (Estilo grueso de tu código original)
                    if(x > 0) {
                        // Trazo ECG (Verde Superior)
                        tft_dibujar_linea_v(x-1, buffer_y_ecg[x-1], prev_y_ecg, VERDE);
                        tft_dibujar_linea_v(x-2, buffer_y_ecg[x-2], buffer_y_ecg[x-1], VERDE);
                        
                        // Trazo PCG (Amarillo Inferior)
                        tft_dibujar_linea_v(x-1, buffer_y_pcg[x-1], prev_y_pcg, AMARILLO);
                        tft_dibujar_linea_v(x-2, buffer_y_pcg[x-2], buffer_y_pcg[x-1], AMARILLO);
                    }
                    
                    // Puntos actuales
                    tft_dibujar_linea_v(x, y_ecg_scr, y_ecg_scr, VERDE);
                    tft_dibujar_linea_v(x, y_pcg_scr, y_pcg_scr, AMARILLO);

                    // Actualizar buffers
                    buffer_y_ecg[x] = y_ecg_scr;
                    prev_y_ecg = y_ecg_scr;
                    
                    buffer_y_pcg[x] = y_pcg_scr;
                    prev_y_pcg = y_pcg_scr;
                    
                    x++;
                    if (x >= ANCHO) x = 0;

                    // 5. Almacenamiento en Memoria (SPI1)
                    f_printf(&archivo_master, "%lu;%u;%u;%u\n", 
                             tiempo_local, ecg_crudo_local, ecg_filtrado, pcg_filtrado);
                    
                    contador_sync++;
                    if (contador_sync >= 100) {
                        f_sync(&archivo_master);
                        contador_sync = 0;
                    }
                }
                break;

            case ERROR_SYS:
                tft_limpiar_pantalla(ROJO);
                while(1) {
                    sleep_ms(1000); 
                }
                break;
        }
    }
    return 0;
}