#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h" // Libreria especifica para pines analogicos

int main() {
    // Inicializa la comunicación por puerto serial (USB)
    stdio_init_all();
    
    // Da un pequeño tiempo de espera para que abras el monitor serial en la PC
    sleep_ms(2000); 
    printf("Iniciando prueba de adquisicion de senales...\n");

    // 1. Inicializa el hardware general del ADC en la Pico
    adc_init();

    // 2. Prepara los pines físicos para que actúen como entradas analógicas
    adc_gpio_init(26); // Prepara el pin GPIO 26 para el ECG
    adc_gpio_init(27); // Prepara el pin GPIO 27 para el Micrófono

    // Bucle infinito de lectura
    while (1) {
        // --- LEER SENSOR ECG ---
        adc_select_input(0); // El canal ADC 0 corresponde al GPIO 26
        uint16_t valor_ecg = adc_read(); // Lee y guarda el valor (0 - 4095)

        // --- LEER MICRÓFONO ---
        adc_select_input(1); // El canal ADC 1 corresponde al GPIO 27
        uint16_t valor_mic = adc_read(); // Lee y guarda el valor (0 - 4095)

        // Imprime los datos por USB
        // El formato separado por comas es util para ver graficas
        printf("ECG: %d, MIC: %d\n", valor_ecg, valor_mic);

        // Retardo de 10 milisegundos = Frecuencia de muestreo de 100 Hz (aprox)
        sleep_ms(10); 
    }

    return 0;
}