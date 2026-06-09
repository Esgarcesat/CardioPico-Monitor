#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/adc.h"

// Configuración SPI0 para Pantalla TFT
#define SPI_TFT_PORT spi0
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_RST  20
#define PIN_DC   21

#define ANCHO 320
#define ALTO  240

#define NEGRO    0x0000
#define AMARILLO 0xFFE0 
#define ROJO     0xF800 // Rojo puro para el ruido de fondo

// ==========================================
// SUBSISTEMA DE FILTRADO DIGITAL (PCG)
// ==========================================
// Reducimos la ventana a 3 para no aplanar la onda acústica
#define VENTANA_MIC 3  
uint16_t buffer_mic[VENTANA_MIC] = {0};
uint8_t indice_mic = 0;

void init_filtro_mic() {
    for(int i=0; i<VENTANA_MIC; i++) buffer_mic[i] = 2047;
}

uint16_t aplicar_filtro_mic(uint16_t nuevo_dato) {
    buffer_mic[indice_mic] = nuevo_dato;
    indice_mic = (indice_mic + 1) % VENTANA_MIC;
    uint32_t suma = 0;
    for(int i=0; i<VENTANA_MIC; i++) suma += buffer_mic[i];
    return (uint16_t)(suma / VENTANA_MIC);
}

// ==========================================
// FUNCIONES DE PANTALLA
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
    tft_enviar_comando(0x01); sleep_ms(150); // Soft reset
    tft_enviar_comando(0x11); sleep_ms(250); // Sleep out
    
    // --- SOLUCIÓN DE COLORES ---
    // Este comando corrige los colores invertidos del panel
    tft_enviar_comando(0x21); 
    
    tft_enviar_comando(0x36); tft_enviar_dato(0x28); 
    tft_enviar_comando(0x3A); tft_enviar_dato(0x55); 
    tft_enviar_comando(0x29); sleep_ms(150); // Display on
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

uint16_t map_adc_to_screen(uint16_t valor_adc) {
    return (uint16_t)( (ALTO-1) - (valor_adc * (ALTO-1) / 4095) );
}

int main() {
    stdio_init_all();

    spi_init(SPI_TFT_PORT, 31000 * 1000);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);
    gpio_init(PIN_DC); gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
    
    tft_init();
    
    for(uint16_t i=0; i<ANCHO; i++) tft_dibujar_linea_v(i, 0, ALTO-1, NEGRO);

    adc_init();
    adc_gpio_init(27);
    adc_select_input(1); 
    init_filtro_mic();

    uint16_t x = 0;
    uint16_t prev_y_filtrada = ALTO / 2;

    while (1) {
        uint16_t lectura_mic_cruda = adc_read();
        uint16_t lectura_mic_filtrada = aplicar_filtro_mic(lectura_mic_cruda);

        uint16_t y_cruda_scr = map_adc_to_screen(lectura_mic_cruda);
        uint16_t y_filtrada_scr = map_adc_to_screen(lectura_mic_filtrada);

        tft_dibujar_linea_v(x, 0, ALTO-1, NEGRO);
        if(x + 1 < ANCHO) tft_dibujar_linea_v(x+1, 0, ALTO-1, NEGRO);
        if(x + 2 < ANCHO) tft_dibujar_linea_v(x+2, 0, ALTO-1, NEGRO);

        // Señal cruda en rojo al fondo, señal filtrada en amarillo brillante al frente
        tft_dibujar_linea_v(x, y_cruda_scr, y_cruda_scr, ROJO);
        
        if(x > 0) {
            tft_dibujar_linea_v(x-1, prev_y_filtrada, y_filtrada_scr, AMARILLO);
        }

        prev_y_filtrada = y_filtrada_scr;
        x++;
        if (x >= ANCHO) x = 0;

        sleep_ms(2);
    }
    return 0;
}