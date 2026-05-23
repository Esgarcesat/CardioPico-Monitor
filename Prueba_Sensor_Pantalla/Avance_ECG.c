#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/adc.h"

// Definición de pines para SPI0
#define SPI_PORT spi0
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_RST  20
#define PIN_DC   21

// Dimensiones de la pantalla (Modo Horizontal)
#define ANCHO 320
#define ALTO  240

// Colores en formato RGB565 (16 bits)
#define NEGRO   0x0000
#define VERDE   0x07E0

// Funciones básicas SPI para la pantalla
void tft_enviar_comando(uint8_t cmd) {
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 0);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    gpio_put(PIN_CS, 1);
}

void tft_enviar_dato(uint8_t data) {
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 1);
    spi_write_blocking(SPI_PORT, &data, 1);
    gpio_put(PIN_CS, 1);
}

// Inicialización del chip gráfico ILI9341
void tft_init() {
    gpio_put(PIN_RST, 1); sleep_ms(50);
    gpio_put(PIN_RST, 0); sleep_ms(50);
    gpio_put(PIN_RST, 1); sleep_ms(150);

    tft_enviar_comando(0x01); // Software Reset
    sleep_ms(150);
    tft_enviar_comando(0x11); // Sleep Out
    sleep_ms(250);

    tft_enviar_comando(0x36); // Memory Access Control (Rotación de pantalla)
    tft_enviar_dato(0x28);    // Configura modo horizontal (Landscape)

    tft_enviar_comando(0x3A); // Formato de píxel
    tft_enviar_dato(0x55);    // 16 bits (RGB565)

    tft_enviar_comando(0x29); // Display ON
    sleep_ms(150);
}

// Dibuja un único píxel en coordenadas X, Y
void tft_dibujar_pixel(uint16_t x, uint16_t y, uint16_t color) {
    if(x >= ANCHO || y >= ALTO) return;

    tft_enviar_comando(0x2A); // Column Set
    tft_enviar_dato(x >> 8); tft_enviar_dato(x & 0xFF);
    tft_enviar_dato(x >> 8); tft_enviar_dato(x & 0xFF);

    tft_enviar_comando(0x2B); // Page Set
    tft_enviar_dato(y >> 8); tft_enviar_dato(y & 0xFF);
    tft_enviar_dato(y >> 8); tft_enviar_dato(y & 0xFF);

    tft_enviar_comando(0x2C); // Memory Write
    uint8_t alto = color >> 8;
    uint8_t bajo = color & 0xFF;
    
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 1);
    spi_write_blocking(SPI_PORT, &alto, 1);
    spi_write_blocking(SPI_PORT, &bajo, 1);
    gpio_put(PIN_CS, 1);
}

// Limpia la pantalla inundándola de un color (ej. Negro)
void tft_limpiar_pantalla(uint16_t color) {
    tft_enviar_comando(0x2A);
    tft_enviar_dato(0x00); tft_enviar_dato(0x00);
    tft_enviar_dato((ANCHO-1) >> 8); tft_enviar_dato((ANCHO-1) & 0xFF);

    tft_enviar_comando(0x2B);
    tft_enviar_dato(0x00); tft_enviar_dato(0x00);
    tft_enviar_dato((ALTO-1) >> 8); tft_enviar_dato((ALTO-1) & 0xFF);

    tft_enviar_comando(0x2C);
    
    uint8_t alto = color >> 8;
    uint8_t bajo = color & 0xFF;

    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 1);
    for(uint32_t i = 0; i < (ANCHO * ALTO); i++) {
        spi_write_blocking(SPI_PORT, &alto, 1);
        spi_write_blocking(SPI_PORT, &bajo, 1);
    }
    gpio_put(PIN_CS, 1);
}

int main() {
    stdio_init_all();

    // 1. Inicializar SPI0 a 20 MHz (más rápido para que la gráfica fluya mejor)
    spi_init(SPI_PORT, 20000 * 1000);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // 2. Inicializar GPIOs de control de la pantalla
    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_init(PIN_DC); gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // 3. Inicializar ADC para el sensor ECG
    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);

    // 4. Encender y limpiar pantalla
    tft_init();
    tft_limpiar_pantalla(NEGRO);

    // Variables para el osciloscopio / graficador matemático
    uint16_t x = 0;
    uint16_t y_antigua[ANCHO] = {0}; // Guarda el dibujo anterior para borrarlo dinámicamente

    while (1) {
        // Leer el valor del corazón (0 a 4095)
        uint16_t lectura_adc = adc_read();

        // MAPEO MATEMÁTICO: Convertir el rango 0-4095 al rango de píxeles 0-239
        // Invertimos la resta (ALTO - 1 - ...) para que los picos altos del corazón vayan hacia arriba en la pantalla
        uint16_t y = (ALTO - 1) - (lectura_adc * (ALTO - 1) / 4095);

        // BORRADO DINÁMICO: Borramos el píxel viejo en esta coordenada X pintándolo de NEGRO
        tft_dibujar_pixel(x, y_antigua[x], NEGRO);

        // DIBUJO NUEVO: Pintamos el nuevo punto del electrocardiograma en color VERDE
        tft_dibujar_pixel(x, y, VERDE);

        // Guardamos la posición actual para poder borrarla en la siguiente vuelta completa
        y_antigua[x] = y;

        // Avanzar una columna a la derecha en la pantalla
        x++;
        if (x >= ANCHO) {
            x = 0; // Si llega al borde derecho (píxel 320), vuelve a empezar desde la izquierda
        }

        // Control de velocidad de la onda (aprox 200 Hz de muestreo)
        sleep_ms(5);
    }

    return 0;
}