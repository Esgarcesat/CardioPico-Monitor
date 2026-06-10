#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// Definición de pines y bus I2C
#define I2C_PORT i2c0
#define I2C_SDA 4
#define I2C_SCL 5

// Dirección I2C por defecto del MPU6050
#define MPU6050_ADDR 0x68

void mpu6050_init() {
    // El MPU6050 arranca en modo "Sleep". Debemos escribir un 0 en el registro de control de energía (0x6B) para despertarlo.
    uint8_t buf[2];
    buf[0] = 0x6B; // Registro PWR_MGMT_1
    buf[1] = 0x00; // Valor a escribir (Despertar)
    i2c_write_blocking(I2C_PORT, MPU6050_ADDR, buf, 2, false);
}

void mpu6050_read_accel(int16_t accel[3]) {
    uint8_t buffer[6];
    uint8_t reg = 0x3B; // El primer registro de datos del acelerómetro (ACCEL_XOUT_H)

    // Apuntar al registro que queremos leer
    i2c_write_blocking(I2C_PORT, MPU6050_ADDR, &reg, 1, true); 
    
    // Leer 6 bytes consecutivos (Alta y Baja para X, Y, Z)
    i2c_read_blocking(I2C_PORT, MPU6050_ADDR, buffer, 6, false);

    // Unir los bytes altos y bajos para formar los enteros de 16 bits
    accel[0] = (buffer[0] << 8) | buffer[1]; // Eje X
    accel[1] = (buffer[2] << 8) | buffer[3]; // Eje Y
    accel[2] = (buffer[4] << 8) | buffer[5]; // Eje Z
}

int main() {
    stdio_init_all();

    // 1. Inicializar I2C a 400 kHz (Modo rápido)
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Bucle de espera clásico para el monitor serial USB
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    printf("\n--- Iniciando prueba MPU6050 ---\n");

    mpu6050_init();
    printf("MPU6050 inicializado correctamente en I2C0.\n");

    int16_t aceleracion[3];

    while (1) {
        mpu6050_read_accel(aceleracion);
        
        // Imprimir los valores crudos de aceleración
        printf("Aceleracion -> X: %d | Y: %d | Z: %d\n", aceleracion[0], aceleracion[1], aceleracion[2]);
        
        sleep_ms(200); // Leer 5 veces por segundo para la prueba visual
    }
    return 0;
}