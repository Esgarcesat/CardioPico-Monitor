/**
 * @file hw_config.c
 * @brief Configuración de hardware para la interfaz SPI y la tarjeta SD usando la librería FatFS.
 */

#include "hw_config.h"
#include "ff.h"       
#include "diskio.h"   

/**
 * @brief Arreglo de configuración de los buses SPI.
 * * Define los pines y la velocidad de transferencia para la comunicación SPI
 * dedicada, en este caso, al módulo de la tarjeta MicroSD (SPI1).
 */
spi_t spis[] = {
    {
        .hw_inst = spi1,
        .miso_gpio = 12, /**< Pin MISO (Master In Slave Out) conectado a SD_MISO */
        .mosi_gpio = 11, /**< Pin MOSI (Master Out Slave In) conectado a SD_MOSI */
        .sck_gpio = 10,  /**< Pin SCK (Serial Clock) conectado a SD_SCK */
        .baud_rate = 12500 * 1000, /**< Velocidad de reloj segura definida a 12.5 MHz */
    }
};

/**
 * @brief Arreglo de configuración de las tarjetas SD del sistema.
 * * Estructura que asocia cada tarjeta SD con su respectivo bus SPI y define
 * parámetros de control específicos como el pin Chip Select (CS) y la detección física.
 */
sd_card_t sd_cards[] = {
    {
        .pcName = "0:",           /**< Nombre lógico de la unidad de almacenamiento para FatFS */
        .spi = &spis[0],          /**< Puntero a la configuración del bus SPI asociado (SPI1) */
        .ss_gpio = 13,            /**< Pin GPIO utilizado como SD_CS (Chip Select) */
        .use_card_detect = false, /**< Bandera booleana: false indica que no se usa un pin físico para detectar si hay tarjeta insertada */
        .m_Status = STA_NOINIT    /**< Estado inicial de la tarjeta en la máquina de estados (No inicializada) */
    }
};

// --- Funciones de interfaz (Hardware Abstraction Layer) requeridas por la librería FatFS ---

/**
 * @brief Obtiene el número total de tarjetas SD configuradas en el sistema.
 * * @return size_t Cantidad de elementos en el arreglo sd_cards.
 */
size_t sd_get_num() { return count_of(sd_cards); }

/**
 * @brief Obtiene el número total de buses SPI configurados en el sistema.
 * * @return size_t Cantidad de elementos en el arreglo spis.
 */
size_t spi_get_num() { return count_of(spis); }

/**
 * @brief Obtiene un puntero a la configuración de un bus SPI específico basado en su índice.
 * * @param num Índice numérico del bus SPI requerido.
 * @return spi_t* Puntero a la estructura spi_t correspondiente. Retorna NULL si el índice está fuera de rango.
 */
spi_t *spi_get_by_num(size_t num) {
    if (num < spi_get_num()) {
        return &spis[num];
    } else {
        return NULL;
    }
}

/**
 * @brief Obtiene un puntero a la configuración de una tarjeta SD específica basada en su índice.
 * * @param num Índice numérico de la tarjeta SD requerida.
 * @return sd_card_t* Puntero a la estructura sd_card_t correspondiente. Retorna NULL si el índice está fuera de rango.
 */
sd_card_t *sd_get_by_num(size_t num) {
    if (num < sd_get_num()) {
        return &sd_cards[num];
    } else {
        return NULL;
    }
}