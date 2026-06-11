/**
 * @file main.c
 * @brief Firmware principal del Cardiopico-Monitor.
 * * Sistema de instrumentación biomédica multimodal basado en RP2040. 
 * Realiza la adquisición de señales de electrocardiograma (ECG), fonocardiograma (PCG),
 * cinemática mediante acelerómetro (MPU6050), almacenamiento masivo en tarjeta SD (FatFS)
 * y renderizado en tiempo real en pantalla TFT ILI9341.
 */

#include <stdio.h>
#include <stdlib.h> 
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "hardware/i2c.h" 
#include "pico/time.h"
#include "ff.h"
#include "hw_config.h"
#include "font.h" 

// ==========================================
// 1. DEFINICIONES Y CONFIGURACIÓN DE PINES
// ==========================================

/** @name Configuración de Pantalla TFT (SPI0) */
/**@{*/
#define SPI_TFT_PORT spi0
#define PIN_CS   17  /**< Chip Select de la pantalla TFT */
#define PIN_SCK  18  /**< Reloj SPI para la pantalla TFT */
#define PIN_MOSI 19  /**< Master Out Slave In para la pantalla TFT */
#define PIN_RST  20  /**< Reset de la pantalla TFT */
#define PIN_DC   21  /**< Data/Command de la pantalla TFT */
/**@}*/

/** @name Configuración del Acelerómetro MPU6050 (I2C0) */
/**@{*/
#define I2C_PORT i2c0
#define I2C_SDA 4    /**< Línea de datos I2C (SDA) */
#define I2C_SCL 5    /**< Línea de reloj I2C (SCL) */
#define MPU6050_ADDR 0x68 /**< Dirección I2C del MPU6050 */
/**@}*/

/** @name Pines Digitales Auxiliares */
/**@{*/
#define PIN_LO_PLUS 2    /**< Detección de electrodo desconectado (Positivo) */
#define PIN_LO_MINUS 3   /**< Detección de electrodo desconectado (Negativo) */
#define PIN_BUZZER 15    /**< Pin de control del zumbador activo */
/**@}*/

/** @name Resolución de Pantalla TFT */
/**@{*/
#define ANCHO 320
#define ALTO  240
/**@}*/

/** @name Paleta de Colores (RGB565) */
/**@{*/
#define NEGRO    0x0000
#define VERDE    0x07E0 
#define AMARILLO 0xFFE0 
#define ROJO     0xF800 
#define BLANCO   0xFFFF 
/**@}*/

// ==========================================
// 2. VARIABLES GLOBALES Y PARÁMETROS MÉDICOS
// ==========================================

volatile bool flag_datos_listos = false; /**< Bandera activada por la ISR cuando hay nuevos datos ADC */
volatile uint16_t lectura_ecg_isr = 0;   /**< Última lectura cruda del ECG desde la ISR */
volatile uint16_t lectura_pcg_isr = 0;   /**< Última lectura cruda del PCG desde la ISR */
volatile uint32_t tiempo_isr_ms = 0;     /**< Marca de tiempo (ms) de la última adquisición */

bool sd_ok = false;                      /**< Bandera que indica si la MicroSD está operativa */
int16_t prev_aceleracion[3] = {0, 0, 0}; /**< Historial de aceleración [X, Y, Z] para calcular el Delta Cinemático */

/** @name Parámetros del Algoritmo Cardíaco (BPM) */
/**@{*/
#define UMBRAL_PICO_R 2400       /**< Valor ADC mínimo para considerar una contracción ventricular (Pico R) */
#define PERIODO_REFRACTARIO 300  /**< Tiempo muerto (ms) después de un latido para evitar dobles conteos */
uint32_t ultimo_tiempo_latido_ms = 0; /**< Marca de tiempo (ms) del último latido detectado */
uint16_t bpm_actual = 0;         /**< Latidos por minuto calculados en tiempo real */
/**@}*/

/**
 * @brief Estados de la máquina de estados finita (FSM) del sistema.
 */
typedef enum { 
    INIT_SYS,    /**< Inicialización de hardware y periféricos */
    IDLE_MODE,   /**< Configuración pre-muestreo y arranque de timers */
    ACQUISITION, /**< Bucle principal de adquisición, procesamiento y renderizado */
    ERROR_SYS    /**< Estado de bloqueo ante fallas críticas (Panic) */
} EstadoSistema;

EstadoSistema estado_actual = INIT_SYS; /**< Variable de estado actual de la FSM */

// ==========================================
// 3. SUBSISTEMAS DE PROCESAMIENTO (DSP)
// ==========================================

#define VENTANA_ECG 10
uint16_t buffer_ecg[VENTANA_ECG] = {0}; /**< Buffer circular para el filtro de media móvil del ECG */
uint8_t indice_ecg = 0;

#define VENTANA_PCG 3
uint16_t buffer_pcg[VENTANA_PCG] = {0}; /**< Buffer circular para el filtro de media móvil del PCG */
uint8_t indice_pcg = 0;

/**
 * @brief Inicializa los buffers de los filtros digitales con un valor ADC centrado.
 */
void init_filtros() {
    for(int i=0; i<VENTANA_ECG; i++) buffer_ecg[i] = 2047;
    for(int i=0; i<VENTANA_PCG; i++) buffer_pcg[i] = 2047;
}

/**
 * @brief Aplica un filtro de media móvil a la señal cruda del ECG.
 * @param nuevo_dato Valor ADC recién adquirido.
 * @return uint16_t Valor filtrado y suavizado.
 */
uint16_t aplicar_filtro_ecg(uint16_t nuevo_dato) {
    buffer_ecg[indice_ecg] = nuevo_dato;
    indice_ecg = (indice_ecg + 1) % VENTANA_ECG;
    uint32_t suma = 0;
    for(int i=0; i<VENTANA_ECG; i++) suma += buffer_ecg[i];
    return (uint16_t)(suma / VENTANA_ECG);
}

/**
 * @brief Aplica un filtro de media móvil a la señal cruda del PCG.
 * @param nuevo_dato Valor ADC recién adquirido.
 * @return uint16_t Valor acústico filtrado.
 */
uint16_t aplicar_filtro_pcg(uint16_t nuevo_dato) {
    buffer_pcg[indice_pcg] = nuevo_dato;
    indice_pcg = (indice_pcg + 1) % VENTANA_PCG;
    uint32_t suma = 0;
    for(int i=0; i<VENTANA_PCG; i++) suma += buffer_pcg[i];
    return (uint16_t)(suma / VENTANA_PCG);
}

/**
 * @brief Despierta e inicializa el acelerómetro MPU6050 vía I2C.
 */
void mpu6050_init() {
    uint8_t buf[2] = {0x6B, 0x00}; // Escribir 0 en PWR_MGMT_1 para despertar
    i2c_write_blocking(I2C_PORT, MPU6050_ADDR, buf, 2, false);
}

/**
 * @brief Lee los tres ejes (X, Y, Z) del acelerómetro de forma simultánea.
 * @param accel Arreglo de 3 elementos de 16-bits donde se guardarán los resultados crudos [X, Y, Z].
 */
void mpu6050_read_accel(int16_t accel[3]) {
    uint8_t buffer[6];
    uint8_t reg = 0x3B; // ACCEL_XOUT_H
    i2c_write_blocking(I2C_PORT, MPU6050_ADDR, &reg, 1, true); 
    i2c_read_blocking(I2C_PORT, MPU6050_ADDR, buffer, 6, false);
    accel[0] = (buffer[0] << 8) | buffer[1]; 
    accel[1] = (buffer[2] << 8) | buffer[3]; 
    accel[2] = (buffer[4] << 8) | buffer[5]; 
}

// ==========================================
// 4. INTERFAZ GRÁFICA (TFT & TEXTO)
// ==========================================

/**
 * @brief Envía un byte de comando al controlador de la pantalla TFT.
 * @param cmd Código hexadecimal del comando.
 */
void tft_enviar_comando(uint8_t cmd) {
    gpio_put(PIN_CS, 0); gpio_put(PIN_DC, 0);
    spi_write_blocking(SPI_TFT_PORT, &cmd, 1);
    gpio_put(PIN_CS, 1);
}

/**
 * @brief Envía un byte de datos al controlador de la pantalla TFT.
 * @param data Valor hexadecimal de los datos.
 */
void tft_enviar_dato(uint8_t data) {
    gpio_put(PIN_CS, 0); gpio_put(PIN_DC, 1);
    spi_write_blocking(SPI_TFT_PORT, &data, 1);
    gpio_put(PIN_CS, 1);
}

/**
 * @brief Inicializa el panel TFT ILI9341 con la secuencia de arranque estándar y orientación horizontal.
 */
void tft_init() {
    gpio_put(PIN_RST, 1); sleep_ms(50);
    gpio_put(PIN_RST, 0); sleep_ms(50);
    gpio_put(PIN_RST, 1); sleep_ms(150);
    tft_enviar_comando(0x01); sleep_ms(150); // Software Reset
    tft_enviar_comando(0x11); sleep_ms(250); // Sleep Out
    tft_enviar_comando(0x36); tft_enviar_dato(0x68); // MADCTL: Memory Access Control (Horizontal)
    tft_enviar_comando(0x3A); tft_enviar_dato(0x55); // Pixel Format Set: 16 bits/pixel
    tft_enviar_comando(0x29); sleep_ms(150); // Display ON
}

/**
 * @brief Dibuja una línea vertical de un color específico en la TFT utilizando ventanas de memoria.
 * @param x Coordenada en el eje X.
 * @param y1 Coordenada inicial en el eje Y.
 * @param y2 Coordenada final en el eje Y.
 * @param color Color en formato RGB565.
 */
void tft_dibujar_linea_v(uint16_t x, uint16_t y1, uint16_t y2, uint16_t color) {
    if(x >= ANCHO) return;
    if(y1 > y2) { uint16_t temp=y1; y1=y2; y2=temp; }
    if(y2 >= ALTO) y2 = ALTO - 1;

    tft_enviar_comando(0x2A); // Configura el rango de columnas (Column Address Set)
    tft_enviar_dato(x >> 8); tft_enviar_dato(x & 0xFF);
    tft_enviar_dato(x >> 8); tft_enviar_dato(x & 0xFF);

    tft_enviar_comando(0x2B); // Configura el rango de páginas/filas (Page Address Set)
    tft_enviar_dato(y1 >> 8); tft_enviar_dato(y1 & 0xFF);
    tft_enviar_dato(y2 >> 8); tft_enviar_dato(y2 & 0xFF);

    tft_enviar_comando(0x2C); // Comando de escritura en memoria (Memory Write)
    uint8_t alto = color >> 8; uint8_t bajo = color & 0xFF;
    
    gpio_put(PIN_CS, 0); gpio_put(PIN_DC, 1);
    uint32_t pixeles = (y2 - y1) + 1;
    for(uint32_t i=0; i<pixeles; i++) {
        spi_write_blocking(SPI_TFT_PORT, &alto, 1);
        spi_write_blocking(SPI_TFT_PORT, &bajo, 1);
    }
    gpio_put(PIN_CS, 1);
}

/**
 * @brief Llena toda la pantalla con un color uniforme usando líneas verticales (Barrido rápido).
 * @param color Color de fondo en formato RGB565.
 */
void tft_limpiar_pantalla(uint16_t color) {
    for(uint16_t x=0; x<ANCHO; x++) tft_dibujar_linea_v(x, 0, ALTO-1, color);
}

/**
 * @brief Dibuja un solo carácter en la pantalla utilizando una fuente Bitmap 8x8.
 * @param x Coordenada X superior izquierda.
 * @param y Coordenada Y superior izquierda.
 * @param c Carácter ASCII a dibujar.
 * @param color Color del texto.
 * @param bg_color Color de fondo del bloque 8x8.
 */
void tft_dibujar_caracter(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color) {
    if (c < 32 || c > 127) return; 
    const uint8_t *bitmap = font8x8[c - 32];
    
    // Abre una "ventana" de memoria en la TFT de tamaño 8x8
    tft_enviar_comando(0x2A);
    tft_enviar_dato(x >> 8); tft_enviar_dato(x & 0xFF);
    tft_enviar_dato((x+7) >> 8); tft_enviar_dato((x+7) & 0xFF);

    tft_enviar_comando(0x2B);
    tft_enviar_dato(y >> 8); tft_enviar_dato(y & 0xFF);
    tft_enviar_dato((y+7) >> 8); tft_enviar_dato((y+7) & 0xFF);

    tft_enviar_comando(0x2C);
    gpio_put(PIN_CS, 0); gpio_put(PIN_DC, 1);
    
    for(uint8_t i=0; i<8; i++) {
        uint8_t row = bitmap[i];
        for(uint8_t j=0; j<8; j++) {
            uint16_t pixel_color = (row & (0x80 >> j)) ? color : bg_color;
            uint8_t alto = pixel_color >> 8;
            uint8_t bajo = pixel_color & 0xFF;
            spi_write_blocking(SPI_TFT_PORT, &alto, 1);
            spi_write_blocking(SPI_TFT_PORT, &bajo, 1);
        }
    }
    gpio_put(PIN_CS, 1);
}

/**
 * @brief Imprime una cadena de texto en la pantalla avanzando automáticamente el cursor horizontal.
 * @param x Coordenada inicial X.
 * @param y Coordenada inicial Y.
 * @param texto Puntero a la cadena de caracteres terminada en nulo.
 * @param color Color del texto.
 * @param bg_color Color de fondo.
 */
void tft_imprimir_texto(uint16_t x, uint16_t y, const char *texto, uint16_t color, uint16_t bg_color) {
    uint16_t cursor_x = x;
    while (*texto) {
        tft_dibujar_caracter(cursor_x, y, *texto, color, bg_color);
        cursor_x += 8; 
        texto++;
    }
}

/**
 * @brief Mapea linealmente un valor ADC de 12 bits a coordenadas Y en la pantalla para la onda ECG.
 * @param valor_adc Dato de entrada (0 a 4095).
 * @return uint16_t Coordenada Y mapeada al segmento superior de la pantalla.
 */
uint16_t map_ecg_to_screen(uint16_t valor_adc) {
    return (uint16_t)( 125 - (valor_adc * 115 / 4095) ); 
}

/**
 * @brief Mapea linealmente un valor ADC de 12 bits a coordenadas Y en la pantalla para la onda PCG.
 * @param valor_adc Dato de entrada (0 a 4095).
 * @return uint16_t Coordenada Y mapeada al segmento inferior de la pantalla.
 */
uint16_t map_pcg_to_screen(uint16_t valor_adc) {
    return (uint16_t)( 239 - (valor_adc * 114 / 4095) );
}

// ==========================================
// 5. RUTINA DE INTERRUPCIÓN DE HARDWARE (ISR)
// ==========================================

/**
 * @brief Función Callback de interrupción del temporizador de hardware (Hardware Timer ISR).
 * * Se ejecuta automáticamente con un periodo estricto (e.g. 5ms). Se encarga del muestreo de bajo 
 * nivel de los canales ADC y avisa al hilo principal cuando los datos están listos.
 * * @param t Puntero a la estructura del repeating_timer.
 * @return true Para mantener activa la repetición del temporizador.
 */
bool timer_adquisicion_callback(struct repeating_timer *t) {
    adc_select_input(0); 
    lectura_ecg_isr = adc_read();
    
    adc_select_input(1); 
    lectura_pcg_isr = adc_read();
    
    tiempo_isr_ms = to_ms_since_boot(get_absolute_time());
    flag_datos_listos = true;
    return true; 
}

// ==========================================
// 6. PROGRAMA PRINCIPAL (MAIN)
// ==========================================

/**
 * @brief Punto de entrada principal del sistema Cardiopico-Monitor.
 * * Controla la inicialización de hardware y ejecuta la máquina de estados principal (FSM)
 * encargada del procesamiento asíncrono, detección de eventos, actualización visual
 * y registro en tarjeta MicroSD.
 * * @return int Código de salida del programa (típicamente no retorna en sistemas embebidos).
 */
int main() {
    stdio_init_all();
    
    sd_card_t *pSD = NULL;
    FIL archivo_master;
    uint32_t contador_sync = 0;

    uint16_t x = 0; // Coordenada X actual de la gráfica en pantalla
    
    uint16_t prev_y_ecg = 70;
    uint16_t prev_y_pcg = 180;
    uint16_t buffer_y_ecg[ANCHO];
    uint16_t buffer_y_pcg[ANCHO];
    
    for(int i=0; i<ANCHO; i++) {
        buffer_y_ecg[i] = 70;
        buffer_y_pcg[i] = 180;
    }

    struct repeating_timer timer;

    while (1) {
        switch (estado_actual) {

            case INIT_SYS:
                printf("\n[FSM] Estado: INIT_SYS -> Inicializando hardware...\n");
                
                i2c_init(I2C_PORT, 400 * 1000); 
                gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
                gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
                gpio_pull_up(I2C_SDA);
                gpio_pull_up(I2C_SCL);
                mpu6050_init();

                gpio_init(PIN_BUZZER);
                gpio_set_dir(PIN_BUZZER, GPIO_OUT);
                gpio_put(PIN_BUZZER, 0);

                gpio_init(PIN_LO_PLUS);
                gpio_set_dir(PIN_LO_PLUS, GPIO_IN);
                
                gpio_init(PIN_LO_MINUS);
                gpio_set_dir(PIN_LO_MINUS, GPIO_IN);

                spi_init(SPI_TFT_PORT, 31000 * 1000);
                gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
                gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
                gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);
                gpio_init(PIN_DC); gpio_set_dir(PIN_DC, GPIO_OUT);
                gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
                
                tft_init();
                tft_limpiar_pantalla(NEGRO);
                init_filtros();

                adc_init();
                adc_gpio_init(26);
                adc_gpio_init(27);

                pSD = sd_get_by_num(0);
                FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
                
                if (fr != FR_OK) {
                    sd_ok = false;
                } else {
                    fr = f_open(&archivo_master, "0:registro_paciente.csv", FA_WRITE | FA_CREATE_ALWAYS);
                    if (fr == FR_OK) {
                        f_printf(&archivo_master, "Tiempo_ms;ECG_Crudo;ECG_Filtrado;PCG_Filtrado;AccX;AccY;AccZ;BPM\n");
                        sd_ok = true;
                    } else {
                        sd_ok = false;
                    }
                }
                
                tft_imprimir_texto(5, 5, "CARDIOPICO-MONITOR", BLANCO, NEGRO);
                tft_imprimir_texto(5, 125, "PCG (Acustico)", BLANCO, NEGRO);
                
                estado_actual = IDLE_MODE;
                break;

            case IDLE_MODE:
                mpu6050_read_accel(prev_aceleracion); 
                add_repeating_timer_ms(-5, timer_adquisicion_callback, NULL, &timer);
                estado_actual = ACQUISITION;
                break;

            case ACQUISITION:
                if (flag_datos_listos) {
                    flag_datos_listos = false; 

                    uint16_t ecg_crudo_local = lectura_ecg_isr;
                    uint16_t pcg_crudo_local = lectura_pcg_isr;
                    uint32_t tiempo_local = tiempo_isr_ms;

                    // 1. Filtrado de Señales
                    uint16_t ecg_filtrado = aplicar_filtro_ecg(ecg_crudo_local);
                    uint16_t pcg_filtrado = aplicar_filtro_pcg(pcg_crudo_local);

                    // ==========================================
                    // 2. ALGORITMO DE DETECCIÓN DE LATIDOS (Pico R)
                    // ==========================================
                    if (ecg_filtrado > UMBRAL_PICO_R) {
                        if ((tiempo_local - ultimo_tiempo_latido_ms) > PERIODO_REFRACTARIO) {
                            uint32_t delta_tiempo = tiempo_local - ultimo_tiempo_latido_ms;
                            bpm_actual = 60000 / delta_tiempo;
                            ultimo_tiempo_latido_ms = tiempo_local;
                            gpio_put(PIN_BUZZER, 1);
                        }
                    }

                    if (gpio_get(PIN_BUZZER) && ((tiempo_local - ultimo_tiempo_latido_ms) > 50)) {
                        gpio_put(PIN_BUZZER, 0);
                    }

                    // 3. Acelerómetro y Detección de Errores (Delta Cinemático)
                    int16_t aceleracion[3];
                    mpu6050_read_accel(aceleracion);
                    int32_t delta_x = abs(aceleracion[0] - prev_aceleracion[0]);
                    int32_t delta_y = abs(aceleracion[1] - prev_aceleracion[1]);
                    int32_t delta_z = abs(aceleracion[2] - prev_aceleracion[2]);
                    prev_aceleracion[0] = aceleracion[0]; prev_aceleracion[1] = aceleracion[1]; prev_aceleracion[2] = aceleracion[2];

                    bool electrodos_desconectados = gpio_get(PIN_LO_PLUS) || gpio_get(PIN_LO_MINUS);
                    bool paciente_en_movimiento = (delta_x > 1500 || delta_y > 1500 || delta_z > 1500);

                    // 4. Mapeo a Pantalla
                    uint16_t y_ecg_scr = map_ecg_to_screen(ecg_filtrado);
                    uint16_t y_pcg_scr = map_pcg_to_screen(pcg_filtrado);

                    tft_dibujar_linea_v(x, 15, ALTO-1, NEGRO);
                    if(x + 1 < ANCHO) tft_dibujar_linea_v(x+1, 15, ALTO-1, NEGRO);
                    if(x + 2 < ANCHO) tft_dibujar_linea_v(x+2, 15, ALTO-1, NEGRO);

                    uint16_t color_trazo_ecg = VERDE;
                    if (electrodos_desconectados || paciente_en_movimiento) color_trazo_ecg = ROJO; 

                    if(x > 0) {
                        tft_dibujar_linea_v(x-1, buffer_y_ecg[x-1], prev_y_ecg, color_trazo_ecg);
                        tft_dibujar_linea_v(x-2, buffer_y_ecg[x-2], buffer_y_ecg[x-1], color_trazo_ecg);
                        
                        tft_dibujar_linea_v(x-1, buffer_y_pcg[x-1], prev_y_pcg, AMARILLO);
                        tft_dibujar_linea_v(x-2, buffer_y_pcg[x-2], buffer_y_pcg[x-1], AMARILLO);
                    }
                    
                    tft_dibujar_linea_v(x, y_ecg_scr, y_ecg_scr, color_trazo_ecg);
                    tft_dibujar_linea_v(x, y_pcg_scr, y_pcg_scr, AMARILLO);

                    buffer_y_ecg[x] = y_ecg_scr; prev_y_ecg = y_ecg_scr;
                    buffer_y_pcg[x] = y_pcg_scr; prev_y_pcg = y_pcg_scr;
                    
                    x++;
                    if (x >= ANCHO) x = 0;

                    // 5. Actualización de Textos Dinámicos (Refresco a ~2 Hz basado en contador SD)
                    if (contador_sync == 0) {
                        if(sd_ok) tft_imprimir_texto(240, 5, "SD: OK   ", VERDE, NEGRO);
                        else tft_imprimir_texto(240, 5, "SD: ERR  ", ROJO, NEGRO);

                        if(electrodos_desconectados) tft_imprimir_texto(5, 110, "ALERTA: CABLE DESCONECTADO  ", ROJO, NEGRO);
                        else if (paciente_en_movimiento) tft_imprimir_texto(5, 110, "RUIDO: ARTEFACTO MOVIMIENTO ", AMARILLO, NEGRO);
                        else tft_imprimir_texto(5, 110, "ESTADO: PACIENTE NORMAL     ", VERDE, NEGRO);
                        
                        char texto_bpm[16];
                        if (bpm_actual > 0 && bpm_actual < 250 && !electrodos_desconectados) {
                            sprintf(texto_bpm, "BPM: %03d", bpm_actual);
                            uint16_t color_bpm = BLANCO;
                            if (bpm_actual < 40 || bpm_actual > 150) color_bpm = ROJO; // Alerta de bradicardia/taquicardia
                            tft_imprimir_texto(240, 110, texto_bpm, color_bpm, NEGRO);
                        } else {
                            tft_imprimir_texto(240, 110, "BPM: ---", BLANCO, NEGRO);
                        }
                    }

                    // 6. Almacenamiento Sincronizado
                    if (sd_ok && !electrodos_desconectados) {
                        f_printf(&archivo_master, "%lu;%u;%u;%u;%d;%d;%d;%u\n", 
                                 tiempo_local, ecg_crudo_local, ecg_filtrado, pcg_filtrado,
                                 aceleracion[0], aceleracion[1], aceleracion[2], bpm_actual);
                    }
                    
                    contador_sync++;
                    if (contador_sync >= 100) {
                        if (sd_ok) f_sync(&archivo_master);
                        contador_sync = 0;
                    }
                }
                break;

            case ERROR_SYS:
                tft_limpiar_pantalla(ROJO);
                while(1) { sleep_ms(1000); }
                break;
        }
    }
    return 0;
}