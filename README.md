# CardioPico-Monitor
Sistema de adquisición de señales médicas ECG y FCG (Fonocardiograma) con RP2040.

## 🚀 Estado Actual y Avances del Proyecto

**Fase 1: Pruebas Unitarias y Primera Integración (Completada)**
Se superaron con éxito las etapas de validación aislada en hardware y software:
* **Adquisición Analógica:** Configuración y validación de los puertos ADC0 y ADC1 de la Raspberry Pi Pico para la lectura del sensor de electrocardiograma (AD8232) y el micrófono (MAX4466).
* **Interfaz Gráfica (SPI0):** Inicialización de la pantalla TFT ILI9341 y desarrollo de algoritmo para el trazado de la onda cardíaca sin parpadeo (Borrado dinámico).
* **Control de Versiones:** Implementación de estrategia de ramas (`main` para código estable y `feature/*` para integración de nuevos periféricos).

**Fase 2: DSP, Multiplexación y Almacenamiento Masivo (Completada)**
Se estructuró el núcleo determinista del sistema:
* **Máquina de Estados y Polling de Alta Velocidad:** Implementación de interrupciones de hardware por Timer a 200 Hz (cada 5 ms) garantizando la lectura concurrente y sin jitter de ambos sensores mediante la multiplexación de los canales ADC.
* **Procesamiento Digital de Señales (DSP):** Implementación de filtros digitales de media móvil independientes para limpiar el ruido eléctrico del ECG y estabilizar el PCG en tiempo real.
* **Sistema de Archivos y SD (SPI1):** Implementación de la librería FatFS para el registro clínico continuo en formato `.csv`, gestionando de forma "suave" (tolerancia a fallos) la conexión/desconexión de la memoria.

**Fase 3: Algoritmia Cardíaca, Cinemática y UI Final (Completada)**
El monitor alcanzó el grado de sistema multiparámetro:
* **Detección de Movimiento Inteligente:** Integración de acelerómetro MPU6050 (I2C). Desarrollo de algoritmo de "Delta Cinemático" para aislar la fuerza de gravedad y detectar únicamente vibraciones y artefactos de movimiento.
* **Algoritmo BPM:** Detección matemática del Pico R mediante umbral dinámico y periodo refractario (300 ms) para el cálculo exacto de Latidos Por Minuto (BPM), sincronizado con un zumbador de feedback auditivo de 50 ms.
* **Motor de Renderizado de Texto:** Creación de una librería de fuentes Bitmap 8x8 personalizada para inyectar datos dinámicos a la pantalla dividida (Split-Screen) a 2 Hz, ahorrando ciclos de CPU.
* **Alertas Clínicas:** El sistema reacciona cambiando el color de las ondas (a rojo) y emitiendo alertas de texto en caso de bradicardia, taquicardia, desconexión de electrodos (pines LO+/LO-) o movimiento brusco del paciente.

## 📌 Conclusiones y Trabajo Futuro

**Conclusiones de Ingeniería:**
* **Capacidad del SoC RP2040:** Se comprobó que un microcontrolador de bajo costo puede ejecutar adquisición multimodal, filtrado digital y almacenamiento masivo de forma concurrente. La arquitectura basada en una Máquina de Estados (FSM) gobernada por interrupciones de hardware garantizó un determinismo estricto, manteniendo el jitter por debajo del 1% sin necesidad de un RTOS.
* **Eficiencia del DSP Ligero:** La implementación de filtros digitales de media móvil demostró ser una alternativa altamente eficiente para la remoción de ruido eléctrico de alta frecuencia y estática ambiental, limpiando la bioseñal sin saturar los ciclos del procesador.
* **Resiliencia y Tolerancia a Fallos:** El diseño del firmware incorporó rutinas de manejo de errores críticos comunes en entornos médicos, como la detección por hardware de la desconexión de electrodos (pines LO+/LO-) y la gestión no bloqueante de la tarjeta MicroSD. Esto garantiza que el monitor mantenga la operación continua y la visualización de datos de emergencia sin congelarse, priorizando siempre la observación del estado del paciente.
* **Concurrencia en Protocolos de Comunicación:** El aislamiento estratégico de los buses de hardware, dedicando el bus SPI0 exclusivamente al renderizado en la pantalla TFT y el bus SPI1 al almacenamiento en la MicroSD, demostró ser fundamental. Esta segregación evitó los clásicos cuellos de botella en la transferencia de datos, permitiendo un refresco visual fluido sin sacrificar la sincronía del registro clínico (Datalogging).

**Trabajo Futuro:**
Para iteraciones posteriores del prototipo, se plantean las siguientes mejoras:
1. **Arquitectura Multicore:** Habilitar el procesamiento asimétrico de doble núcleo (Core 0 para adquisición/DSP y Core 1 para SPI/FatFS) para lograr un desacople total de las tareas de entrada/salida.
2. **Seguridad Eléctrica:** Migrar el sistema a una fuente de alimentación aislada por batería de litio (3.7V - 5V) para desconectar físicamente el dispositivo de la red eléctrica, cumpliendo con la normativa estándar de protección al paciente.
3. **Transductor Acústico:** Reemplazar el micrófono ambiental (MAX4466) por un transductor piezoeléctrico de contacto médico. Esto aislará el ruido aéreo y mejorará la respuesta en bajas frecuencias, permitiendo la identificación algorítmica exacta de los ruidos cardíacos S1 y S2 mediante correlación digital.

## 📄 Documentación Técnica
En la carpeta `Avances_PDFs` de este repositorio se encuentran adjuntos los reportes detallados que respaldan este desarrollo, los cuales incluyen:
1. Descripción del proyecto, componentes, requisitos funcionales y no funcionales y costos.
1. Plan de trabajo y porcentaje de ejecución.
2. Modificaciones a la arquitectura y ruteo de hardware (Aislamiento de buses SPI).
3. Justificación y acondicionamiento de los circuitos analógicos integrados.
4. Diagrama de bloques del hardware del monitor cardíaco.
5. Arquitectura del software (Diagrama de Flujo, Polling + Interrupciones y FSM).
6. Estrategia de control de versiones y ramas activas.