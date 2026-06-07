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

// Dimensiones de la pantalla en modo horizontal
#define ANCHO 320
#define ALTO  240

// Colores RGB565
#define NEGRO   0x0000
#define VERDE   0x07E0
#define ROJO    0xF800
#define GRIS    0x7BEF

// --- Configuración del Filtro Digital ---
#define VENTANA_FILTRO 10  // Promediar las últimas 10 lecturas

uint16_t buffer_crudo[VENTANA_FILTRO] = {0};
uint8_t indice_filtro = 0;

// Función para inicializar el búfer del filtro
void init_filtro() {
    for(int i=0; i<VENTANA_FILTRO; i++) {
        buffer_crudo[i] = 2047; // Valor central
    }
}

// Función que aplica el filtro de media móvil
uint16_t aplicar_filtro_media(uint16_t nuevo_dato) {
    // 1. Guardar el nuevo dato crudo en el búfer circular
    buffer_crudo[indice_filtro] = nuevo_dato;
    indice_filtro = (indice_filtro + 1) % VENTANA_FILTRO;

    // 2. Calcular el promedio
    uint32_t suma = 0;
    for(int i=0; i<VENTANA_FILTRO; i++) {
        suma += buffer_crudo[i];
    }
    return (uint16_t)(suma / VENTANA_FILTRO);
}

// --- Funciones de Pantalla TFT ---
void tft_enviar_comando(uint8_t cmd) {
    gpio_put(PIN_CS, 0); gpio_put(PIN_DC, 0);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    gpio_put(PIN_CS, 1);
}

void tft_enviar_dato(uint8_t data) {
    gpio_put(PIN_CS, 0); gpio_put(PIN_DC, 1);
    spi_write_blocking(SPI_PORT, &data, 1);
    gpio_put(PIN_CS, 1);
}

void tft_init() {
    gpio_put(PIN_RST, 1); sleep_ms(50);
    gpio_put(PIN_RST, 0); sleep_ms(50);
    gpio_put(PIN_RST, 1); sleep_ms(150);

    tft_enviar_comando(0x01); sleep_ms(150);
    tft_enviar_comando(0x11); sleep_ms(250);

    // Rotar pantalla: Modo Horizontal (Landscape)
    tft_enviar_comando(0x36);
    tft_enviar_dato(0x28); // 240x320

    tft_enviar_comando(0x3A);
    tft_enviar_dato(0x55); // 16-bit color

    tft_enviar_comando(0x29); // Display ON
    sleep_ms(150);
}

// Dibuja una línea vertical de un color
void tft_dibujar_linea_v(uint16_t x, uint16_t y1, uint16_t y2, uint16_t color) {
    if(x >= ANCHO) return;
    if(y1 > y2) { uint16_t temp=y1; y1=y2; y2=temp; }
    if(y2 >= ALTO) y2 = ALTO - 1;

    tft_enviar_comando(0x2A); // Column Set
    tft_enviar_dato(x >> 8); tft_enviar_dato(x & 0xFF);
    tft_enviar_dato(x >> 8); tft_enviar_dato(x & 0xFF);

    tft_enviar_comando(0x2B); // Page Set
    tft_enviar_dato(y1 >> 8); tft_enviar_dato(y1 & 0xFF);
    tft_enviar_dato(y2 >> 8); tft_enviar_dato(y2 & 0xFF);

    tft_enviar_comando(0x2C); // Memory Write
    
    uint8_t alto = color >> 8;
    uint8_t bajo = color & 0xFF;
    
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 1);
    uint32_t pixeles = (y2 - y1) + 1;
    for(uint32_t i=0; i<pixeles; i++) {
        spi_write_blocking(SPI_PORT, &alto, 1);
        spi_write_blocking(SPI_PORT, &bajo, 1);
    }
    gpio_put(PIN_CS, 1);
}

void tft_limpiar_pantalla(uint16_t color) {
    tft_enviar_comando(0x2A);
    tft_enviar_dato(0x00); tft_enviar_dato(0x00);
    tft_enviar_dato((ANCHO-1) >> 8); tft_enviar_dato((ANCHO-1) & 0xFF);

    tft_enviar_comando(0x2B);
    tft_enviar_dato(0x00); tft_enviar_dato(0x00);
    tft_enviar_dato((ALTO-1) >> 8); tft_enviar_dato((ALTO-1) & 0xFF);

    tft_enviar_comando(0x2C);
    uint8_t alto = color >> 8; uint8_t bajo = color & 0xFF;
    gpio_put(PIN_CS, 0); gpio_put(PIN_DC, 1);
    for(uint32_t i=0; i<(ANCHO*ALTO); i++) {
        spi_write_blocking(SPI_PORT, &alto, 1);
        spi_write_blocking(SPI_PORT, &bajo, 1);
    }
    gpio_put(PIN_CS, 1);
}

// Función auxiliar para mapear el ADC a los píxeles de pantalla (invierte el eje Y)
uint16_t map_adc_to_screen(uint16_t valor_adc) {
    // ADC es 0-4095. Pantalla es 0-239.
    // Invertimos la resta para que picos altos del corazón vayan arriba.
    return (uint16_t)( (ALTO-1) - (valor_adc * (ALTO-1) / 4095) );
}

int main() {
    stdio_init_all();

    // 1. Inicializar SPI0 a 31 MHz (velocidad máxima recomendada para ILI9341 en Pico)
    spi_init(SPI_PORT, 31000 * 1000);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_init(PIN_DC); gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // 2. Inicializar ADC
    adc_init();
    adc_gpio_init(26);
    adc_select_input(0); // GPIO 26

    // 3. Inicializar Pantalla y Filtro
    tft_init();
    tft_limpiar_pantalla(NEGRO);
    init_filtro();

    uint16_t x = 0;
    // Guardar el dibujo previo de la filtrada para borrar solo lo necesario
    uint16_t prev_y_filtrada = ALTO / 2;
    // Búfer para guardar la forma de la filtrada y poder redibujarla de atrás
    uint16_t buffer_y_filtrada[ANCHO];
    for(int i=0; i<ANCHO; i++) buffer_y_filtrada[i] = ALTO/2;

    printf("Iniciando modo osciloscopio filtrado...\n");

    while (1) {
        // --- Fase de Lectura y DSP ---
        uint16_t lectura_cruda = adc_read();
        uint16_t lectura_filtrada = aplicar_filtro_media(lectura_cruda);

        uint16_t y_cruda_scr = map_adc_to_screen(lectura_cruda);
        uint16_t y_filtrada_scr = map_adc_to_screen(lectura_filtrada);

        // --- Fase de Dibujo Avanzada en Pantalla (Barrido Horizontal) ---
        
        // 1. Borrado dinámico de la columna actual: Pintar de negro una línea de 3 pixeles de ancho
        // Esto crea un "vacío" que avanza de izquierda a derecha
        tft_dibujar_linea_v(x, 0, ALTO-1, NEGRO);
        if(x + 1 < ANCHO) tft_dibujar_linea_v(x+1, 0, ALTO-1, NEGRO);
        if(x + 2 < ANCHO) tft_dibujar_linea_v(x+2, 0, ALTO-1, NEGRO);
        if(x + 3 < ANCHO) tft_dibujar_linea_v(x+3, 0, ALTO-1, NEGRO);

        // 2. Redibujar la señal filtrada del píxel anterior (deja una estela verde gruesa)
        if(x > 0) {
            tft_dibujar_linea_v(x-1, buffer_y_filtrada[x-1], prev_y_filtrada, VERDE);
            // Hacer la línea más gruesa dibujando una columna adyacente
            tft_dibujar_linea_v(x-2, buffer_y_filtrada[x-2], buffer_y_filtrada[x-1], VERDE);
        }

        // 3. Dibujar la señal cruda actual (puntos rojos finos) para compararla
        tft_dibujar_linea_v(x, y_cruda_scr, y_cruda_scr, ROJO);

        // 4. Dibujar la señal filtrada actual (un punto verde al frente del barrido)
        tft_dibujar_linea_v(x, y_filtrada_scr, y_filtrada_scr, VERDE);

        // Guardar valores para el próximo ciclo
        buffer_y_filtrada[x] = y_filtrada_scr;
        prev_y_filtrada = y_filtrada_scr;

        // Avanzar el barrido
        x++;
        if (x >= ANCHO) {
            x = 0; // Llegó al borde derecho, vuelve a la izquierda
        }

        // --- Control de Muestreo ---
        // aprox 250 Hz (4ms) para una buena captura del corazón
        sleep_ms(4);
    }
    
    return 0;
}