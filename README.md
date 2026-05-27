# 🌾 Sistema de Riego Inteligente, Seguridad Automatizada e IA frente a Plagas

Este repositorio contiene el código fuente desarrollado en Arduino para un prototipo de invernadero inteligente utilizando el microcontrolador **XIAO ESP32S3**. El sistema gestiona de manera local el riego autónomo, la seguridad contra incendios y el análisis predictivo visual para la detección de plagas mediante un modelo de Machine Learning integrado (TinyML).

---

## 🛠️ Arquitectura y Pines Utilizados

El sistema se conecta a través de la **Seeed Studio Expansion Board v1.1** empleando la siguiente distribución de pines:

* **`D0` (SOIL_ANALOG_PIN):** Lectura analógica del sensor de humedad de suelo.
* **`D3` (SOIL_POWER_PIN):** Control de alimentación del sensor de suelo (evita la corrosión prematura del electrodo).
* **`D1` (RELAY_PIN):** Activación del módulo Relé para la bomba de agua de 5V.
* **`D2` (MQ135_PIN):** Lectura del sensor de gas/humo MQ-135.
* **Pantalla OLED integrada:** Conectada por bus I2C (`Wire.h`).

---

## 🔬 Explicación Técnica del Código

El firmware está estructurado para operar bajo un entorno multitarea simulado mediante el uso de la función `millis()`, evitando bloquear el procesador con funciones `delay()` innecesarias, excepto en casos de emergencia.

### 1. Calibración del Sensor de Humedad (Map y Constrain)
Dado que los sensores de humedad de suelo entregan valores analógicos puros, se realiza una conversión matemática en el código para trabajar con porcentajes tradicionales ($0\%$ a $100\%$):
* **`AIR_VALUE = 1023`:** El valor en seco absoluto (máxima resistencia).
* **`WATER_VALUE = 418`:** El valor sumergido en agua (mínima resistencia).
* **`map()` y `constrain()`:** Mapean linealmente las lecturas entre estos dos extremos. Si el sensor llega a entregar un valor fuera de rango por ruido eléctrico, `constrain()` bloquea el resultado estrictamente entre $0$ y $100$ para evitar porcentajes imposibles (como valores negativos o de $105\%$).

### 2. Gestión de Prioridad Máxima: Protocolo de Incendios
El flujo de ejecución del `loop()` prioriza por encima de todo la seguridad ambiental monitorizada por el **MQ-135**:
* Si la lectura analógica supera el umbral crítico de **3000**, el software activa inmediatamente una interrupción lógica (`return;`).
* Bypassa (ignora) por completo las tareas secundarias como el riego normal o el análisis de imágenes por IA.
* Fuerza la impresión de `"ALERTA: FUEGO"` en la pantalla OLED, hace parpadear el LED de estado y acciona la bomba durante 2 segundos como choque de mitigación inicial.

### 3. Clasificación de Imágenes e IA (Edge Impulse)
Si no existen emergencias por fuego, el procesador activa la cámara interna del XIAO ESP32S3 configurada en formato **QVGA (320x240)**.
* El búfer de la captura (`snapshot_buf`) es recortado e interpolado dinámicamente mediante la función `crop_and_interpolate_rgb888()` para adaptarse a las dimensiones exactas que exige el modelo matemático de Edge Impulse.
* El bucle clasificador analiza los descriptores de la imagen. Si la probabilidad de coincidencia con una etiqueta de plaga supera el **$80\%$ (`> 0.80`)**, la variable `plagaDetectada` cambia a verdadera, encendiendo el LED integrado y notificándolo en el panel OLED.

### 4. Refresco de Pantalla Dinámico
Para evitar el molesto parpadeo (*flicker*) visual que ocurre al borrar y reescribir una pantalla OLED en cada ciclo del procesador, el código implementa un temporizador:
```cpp
if (millis() - lastDisplay > 2000) { ... }
