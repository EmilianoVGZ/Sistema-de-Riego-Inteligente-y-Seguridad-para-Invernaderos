#include <EmilianoVG-project-1_inferencing.h> 
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include "DHT.h"
#include <Wire.h>
#include <U8g2lib.h>

// PANTALLA OLED INTEGRADA
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// PINES EXPANSION BOARD
#define SOIL_ANALOG_PIN D0 
#define SOIL_POWER_PIN  D3    
#define RELAY_PIN       D1   
#define MQ135_PIN       D2   
#define LED_PIN         LED_BUILTIN 

// CALIBRACIÓN SUELO
const int AIR_VALUE = 1023;    
const int WATER_VALUE = 418;   
const int UMBRAL_RIEGO = 30;   

// PINES CÁMARA XIAO S3
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     41
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     39
#define PCLK_GPIO_NUM     42

static bool is_initialised = false;
uint8_t *snapshot_buf; 

int porcentajeHumedad = 0;
int gasValue = 0;
bool plagaDetectada = false;
bool alertaFuego = false;

unsigned long lastCheck = 0;
unsigned long lastDisplay = 0;

void setup() {
    Serial.begin(115200);
    
    pinMode(SOIL_POWER_PIN, OUTPUT);
    digitalWrite(SOIL_POWER_PIN, LOW); 
    
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); 
    pinMode(LED_PIN, OUTPUT);
    
    // Iniciar I2C para la pantalla integrada
    Wire.begin(); 
    u8g2.begin();
    u8g2.setBusClock(400000);

    Serial.println("Iniciando Cámara...");
    if (!ei_camera_init()) {
        Serial.println("ERROR CÁMARA: Revisa el flex.");
    }

    snapshot_buf = (uint8_t*)malloc(EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3);
}

void loop() {
    
    // 1. LECTURA DE SENSORES (Cada 5 segundos de forma normal)
    if (millis() - lastCheck > 5000) {
        lastCheck = millis();
        
        // Medición del sensor de humo/gas primero para evaluar emergencias
        gasValue = analogRead(MQ135_PIN);

        // EVALUACIÓN DE PRIORIDAD MÁXIMA: ¿HAY FUEGO/HUMO CRÍTICO?
        if (gasValue > 3000) {
            alertaFuego = true;
            
            // Actuación inmediata: Forzar actualización de pantalla y activar bomba
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.setCursor(0, 35);
            u8g2.print("ALERTA: FUEGO");
            u8g2.sendBuffer();
            
            Serial.println("!!! INCENDIO DETECTADO !!! Activando bomba...");
            digitalWrite(RELAY_PIN, HIGH);
            delay(2000); // 2 segundos de mitigación inicial
            digitalWrite(RELAY_PIN, LOW);
            
            // Pausa extendida para evitar bucles infinitos inmediatos si el humo sigue disipándose
            lastCheck = millis() + 3000; 
            return; // Interrumpe el resto del loop para priorizar la emergencia
        } else {
            alertaFuego = false; // Si baja de 3000, el estado vuelve a la normalidad
        }

        // Lectura controlada del suelo si NO hay fuego
        digitalWrite(SOIL_POWER_PIN, HIGH);
        delay(50); 
        int rawSoil = analogRead(SOIL_ANALOG_PIN);
        digitalWrite(SOIL_POWER_PIN, LOW); 
        
        if (rawSoil > 50) { 
            porcentajeHumedad = map(rawSoil, AIR_VALUE, WATER_VALUE, 0, 100);
            porcentajeHumedad = constrain(porcentajeHumedad, 0, 100);

            // Riego normal por humedad (Solo si no hay fuego activo)
            if (porcentajeHumedad < UMBRAL_RIEGO && !alertaFuego) { 
                Serial.println("Riego normal por suelo seco.");
                digitalWrite(RELAY_PIN, HIGH);
                delay(3000); // 3 segundos para el riego normal de la planta
                digitalWrite(RELAY_PIN, LOW);
            }
        }
    }

    // 2. IA - DETECCIÓN DE PLAGAS (Solo opera si no está en alerta de fuego)
    if (is_initialised && !alertaFuego) {
        ei::signal_t signal;
        signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
        signal.get_data = &ei_camera_get_data;

        if (ei_camera_capture((size_t)EI_CLASSIFIER_INPUT_WIDTH, (size_t)EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf)) {
            ei_impulse_result_t result = { 0 };
            if (run_classifier(&signal, &result, false) == EI_IMPULSE_OK) {
                plagaDetectada = false;
                for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
                    if (String(result.classification[i].label) != "background") {
                        if (result.classification[i].value > 0.80) plagaDetectada = true;
                    }
                }
            }
        }
    }

    // El LED integrado parpadea o se enciende si hay plaga o fuego
    digitalWrite(LED_PIN, (plagaDetectada || alertaFuego) ? HIGH : LOW);

    // 3. GESTIÓN DINÁMICA DE LA PANTALLA OLED
    if (millis() - lastDisplay > 2000) {
        lastDisplay = millis();
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        
        if (alertaFuego) {
            // Pantalla exclusiva de Emergencia
            u8g2.setCursor(0, 35);
            u8g2.print("ALERTA: FUEGO");
        } else {
            // Visualización normal del sistema agrícola
            u8g2.setCursor(0, 15);
            u8g2.print("Humedad: "); u8g2.print(porcentajeHumedad); u8g2.print("%");
            
            u8g2.setCursor(0, 31);
            u8g2.print("Gas: "); u8g2.print(gasValue);
            
            u8g2.setCursor(0, 47);
            u8g2.print(plagaDetectada ? "ALERTA: PLAGA" : "Estado: OK");
   
        }
        
        u8g2.sendBuffer();
    }
}

/* --- Funciones base de la Cámara (QVGA) --- */
bool ei_camera_init(void) {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;
    config.frame_size = FRAMESIZE_QVGA; 
    config.pixel_format = PIXFORMAT_JPEG; config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM; config.jpeg_quality = 12; config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) return false;
    is_initialised = true;
    return true;
}

bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return false;
    bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
    esp_camera_fb_return(fb);
    if (!converted) return false;
    ei::image::processing::crop_and_interpolate_rgb888(out_buf, 320, 240, out_buf, img_width, img_height);
    return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;
    while (pixels_left != 0) {
        out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix + 2] << 16) + (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix];
        out_ptr_ix++; pixel_ix += 3; pixels_left--;
    }
    return 0;
}// Pega aquí tu código (setup/loop)
