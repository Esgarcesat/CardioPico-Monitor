#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h" // Librería necesaria para detectar la conexión
#include "ff.h"
#include "hw_config.h"

int main() {
    stdio_init_all();

    // ¡EL TRUCO MÁGICO! 
    // Esto crea un bucle infinito que pausa la Pico hasta que le des a "Start Monitoring"
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    sleep_ms(1000); // 1 segundo extra para que la terminal termine de renderizar la ventana

    printf("\n--- Iniciando prueba de MicroSD ---\n");

    // 1. Inicializar y montar la tarjeta SD
    sd_card_t *pSD = sd_get_by_num(0);
    FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    
    if (fr != FR_OK) {
        printf("Error critico montando la tarjeta SD: Codigo %d\n", fr);
        printf("Revisa los cables y el formateo FAT32.\n");
        while(1) sleep_ms(1000);
    }
    printf("Tarjeta SD montada correctamente en el bus SPI1.\n");

    // 2. Crear y abrir un archivo CSV
    FIL archivo;
    fr = f_open(&archivo, "0:datos_ecg.csv", FA_WRITE | FA_CREATE_ALWAYS);
    
    if (fr == FR_OK) {
        printf("Archivo CSV creado. Escribiendo datos falsos de prueba...\n");
        
        // Escribir cabecera y datos
        f_printf(&archivo, "Tiempo(ms),Valor_ECG\n");
        f_printf(&archivo, "100,2048\n");
        f_printf(&archivo, "200,2100\n");
        f_printf(&archivo, "300,1950\n");
        
        // Guardar y cerrar
        f_close(&archivo);
        printf("Datos guardados exitosamente.\n");
    } else {
        printf("Error abriendo el archivo para escritura: %d\n", fr);
    }

    // 3. Desmontar de forma segura
    f_unmount(pSD->pcName);
    printf("Prueba finalizada. Ya puedes sacar la MicroSD y leerla en tu computador.\n");

    while (true) {
        sleep_ms(1000);
    }
}