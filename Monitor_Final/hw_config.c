#include "hw_config.h"
#include "ff.h"       
#include "diskio.h"   

// Configuración del bus SPI1
spi_t spis[] = {
    {
        .hw_inst = spi1,
        .miso_gpio = 12, // SD_MISO
        .mosi_gpio = 11, // SD_MOSI
        .sck_gpio = 10,  // SD_SCK
        .baud_rate = 12500 * 1000, // Velocidad segura 12.5 MHz
    }
};

// Configuración de la tarjeta SD
sd_card_t sd_cards[] = {
    {
        .pcName = "0:",           // Nombre lógico de la unidad
        .spi = &spis[0],          // Enlaza con el SPI1 configurado arriba
        .ss_gpio = 13,            // SD_CS (Chip Select)
        .use_card_detect = false, // No usamos pin extra para detectar si hay tarjeta
        .m_Status = STA_NOINIT
    }
};

// --- Funciones obligatorias que pide la librería FatFS ---
size_t sd_get_num() { return count_of(sd_cards); }
size_t spi_get_num() { return count_of(spis); }

// Función que faltaba: Buscar el SPI por su número
spi_t *spi_get_by_num(size_t num) {
    if (num < spi_get_num()) {
        return &spis[num];
    } else {
        return NULL;
    }
}

// Función que faltaba: Buscar la tarjeta SD por su número
sd_card_t *sd_get_by_num(size_t num) {
    if (num < sd_get_num()) {
        return &sd_cards[num];
    } else {
        return NULL;
    }
}