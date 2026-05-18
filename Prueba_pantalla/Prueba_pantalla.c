#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// Definición de pines para SPI0
#define SPI_PORT spi0
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_RST  20
#define PIN_DC   21

// Funciones para enviar comandos y datos por SPI
void tft_enviar_comando(uint8_t cmd) {
    gpio_put(PIN_CS, 0); // Seleccionar pantalla
    gpio_put(PIN_DC, 0); // Modo comando
    spi_write_blocking(SPI_PORT, &cmd, 1);
    gpio_put(PIN_CS, 1); // Deseleccionar
}

void tft_enviar_dato(uint8_t data) {
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 1); // Modo dato
    spi_write_blocking(SPI_PORT, &data, 1);
    gpio_put(PIN_CS, 1);
}

// Función para inicializar la pantalla ILI9341
void tft_init() {
    // Hard Reset
    gpio_put(PIN_RST, 1); sleep_ms(50);
    gpio_put(PIN_RST, 0); sleep_ms(50);
    gpio_put(PIN_RST, 1); sleep_ms(150);

    // Secuencia básica de inicio ILI9341
    tft_enviar_comando(0x01); // Software Reset
    sleep_ms(150);
    
    tft_enviar_comando(0x11); // Sleep Out
    sleep_ms(250);
    
    tft_enviar_comando(0x3A); // Formato de color
    tft_enviar_dato(0x55);    // 16 bits por pixel (RGB565)
    
    tft_enviar_comando(0x29); // Display ON
    sleep_ms(150);
}

// Función para llenar la pantalla de un color
void tft_llenar_color(uint8_t color_alto, uint8_t color_bajo) {
    // Definir el área a dibujar (Toda la pantalla 240x320)
    tft_enviar_comando(0x2A); // Column Address Set
    tft_enviar_dato(0x00); tft_enviar_dato(0x00); // Inicio: 0
    tft_enviar_dato(0x00); tft_enviar_dato(0xEF); // Fin: 239

    tft_enviar_comando(0x2B); // Page Address Set
    tft_enviar_dato(0x00); tft_enviar_dato(0x00); // Inicio: 0
    tft_enviar_dato(0x01); tft_enviar_dato(0x3F); // Fin: 319

    tft_enviar_comando(0x2C); // Memory Write (Prepararse para recibir los pixeles)

    // Enviar los 76,800 pixeles (240 * 320)
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 1); // Modo dato para enviar todos los pixeles rápido
    
    for(uint32_t i = 0; i < 76800; i++) {
        spi_write_blocking(SPI_PORT, &color_alto, 1);
        spi_write_blocking(SPI_PORT, &color_bajo, 1);
    }
    gpio_put(PIN_CS, 1);
}

int main() {
    stdio_init_all();

    // 1. Inicializar hardware SPI a 10 MHz
    spi_init(SPI_PORT, 10000 * 1000);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // 2. Inicializar pines de control GPIO
    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_init(PIN_DC); gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);

    gpio_put(PIN_CS, 1); // Deseleccionado inicialmente

    printf("Iniciando prueba de pantalla...\n");

    // 3. Inicializar el chip de la pantalla
    tft_init();

    printf("Pintando pantalla de ROJO...\n");
    // El color rojo puro en formato RGB565 es 0xF800 (Alto: 0xF8, Bajo: 0x00)
    tft_llenar_color(0xF8, 0x00);

    while (1) {
        printf("Pantalla roja generada exitosamente.\n");
        sleep_ms(2000);
    }
    
    return 0;
}