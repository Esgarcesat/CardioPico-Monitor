# CardioPico-Monitor
Sistema de adquisición de señales cardíacas ECG y FCG con RP2040

## 🚀 Estado Actual y Avances del Proyecto

**Fase 1: Pruebas Unitarias y Primera Integración (Completada)**

Hasta el momento, se han superado con éxito las siguientes etapas de validación en hardware y software:
* **Adquisición Analógica:** Configuración y validación de los puertos ADC0 y ADC1 de la Raspberry Pi Pico para la lectura del sensor de electrocardiograma (AD8232) y el micrófono (MAX4466).
* **Interfaz Gráfica (SPI0):** Inicialización de la pantalla TFT ILI9341 y desarrollo de algoritmo para el trazado de la onda cardíaca sin parpadeo.
* **Integración:** Graficación del electrocardiograma en tiempo real.
* **Control de Versiones:** Implementación de estrategia de ramas (`main` para código estable y `feature/*` para integración de nuevos periféricos, como la MicroSD).

**📄 Documentación Técnica**
En la carpeta `Avances_PDFs` de este repositorio se encuentran adjuntos los reportes detallados que respaldan este avance, los cuales incluyen:
1. Plan de trabajo y porcentaje de ejecución.
2. Modificaciones a la arquitectura y ruteo de hardware (Aislamiento de buses SPI).
3. Justificación y acondicionamiento de los circuitos analógicos integrados.
4. Diagrama de bloques del hardware del monitor cardíaco.
5. Arquitectura del software (Diagrama de Flujo, Polling + Interrupciones y FSM).
6. Estrategia de control de versiones y ramas activas.
