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

#define NEGRO   0x0000
#define AMARILLO 0xFFE0 // Color distintivo para la señal acústica
#define GRIS    0x7BEF

// Funciones básicas de TFT
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
    tft_enviar_comando(0x36); tft_enviar_dato(0x28); 
    tft_enviar_comando(0x3A); tft_enviar_dato(0x55); 
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

uint16_t map_adc_to_screen(uint16_t valor_adc) {
    return (uint16_t)( (ALTO-1) - (valor_adc * (ALTO-1) / 4095) );
}

int main() {
    stdio_init_all();

    // Iniciar SPI y Pantalla
    spi_init(SPI_TFT_PORT, 31000 * 1000);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);
    gpio_init(PIN_DC); gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
    
    tft_init();
    
    // Pintar fondo negro
    for(uint16_t i=0; i<ANCHO; i++) tft_dibujar_linea_v(i, 0, ALTO-1, NEGRO);

    // Iniciar ADC para el Micrófono en GPIO 27 (Canal 1)
    adc_init();
    adc_gpio_init(27);
    adc_select_input(1); 

    uint16_t x = 0;
    uint16_t prev_y = ALTO / 2;

    while (1) {
        // Leer el micrófono (el sonido viaja como variaciones de voltaje)
        uint16_t lectura_mic = adc_read();
        uint16_t y_scr = map_adc_to_screen(lectura_mic);

        // Borrado dinámico de la columna actual
        tft_dibujar_linea_v(x, 0, ALTO-1, NEGRO);
        if(x + 1 < ANCHO) tft_dibujar_linea_v(x+1, 0, ALTO-1, NEGRO);
        if(x + 2 < ANCHO) tft_dibujar_linea_v(x+2, 0, ALTO-1, NEGRO);

        // Dibujar la señal acústica
        if(x > 0) {
            tft_dibujar_linea_v(x-1, prev_y, y_scr, AMARILLO);
        }

        prev_y = y_scr;
        x++;
        if (x >= ANCHO) x = 0;

        sleep_ms(2); // Muestreo de audio rápido
    }
    return 0;
}