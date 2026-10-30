// Face task: owns LVGL and the ST7789. Other tasks talk to it only through a queue.
#include "display.h"

#include <Arduino.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "config.h"
#include "face.h"
#include "face_style.h"
#include "lv_psram_pool.h"
#include "serial_log.h"

namespace petbot {
namespace display {

namespace {

namespace proto = petbot::protocol;

const uint32_t kFramePeriodMs = 33;  // ~30 fps
const UBaseType_t kQueueDepth = 8;  // also holds messages that arrive while LVGL starts
const uint32_t kTaskStackBytes = 12 * 1024;
const UBaseType_t kTaskPriority = 2;  // below the link RX task (3)
const uint32_t kPanelWidth = 240;     // native portrait size; rotated to 320x240
const uint32_t kPanelHeight = 320;

QueueHandle_t g_queue = nullptr;
volatile bool g_disabled = false;  // face failed to start; nothing will drain the queue
void* g_pool = nullptr;
SPIClass g_spi(FSPI);

// ---- memory ----

void* allocPreferPsram(size_t bytes, bool& inPsram) {
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    inPsram = p != nullptr;
    if (p == nullptr) p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

// ---- LVGL hooks ----

uint32_t tickMs() { return millis(); }

void delayMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1); }

void lvglLog(lv_log_level_t, const char* buf) {
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) --n;
    logLine("[lvgl] %.*s", static_cast<int>(n), buf);
}

// ---- ST7789 over SPI (blocking writes; DMA can come later) ----

void spiWrite(const uint8_t* cmd, size_t cmdSize, const uint8_t* data, size_t dataSize) {
    g_spi.beginTransaction(SPISettings(config::kDisplaySpiHz, MSBFIRST, SPI_MODE0));
    digitalWrite(config::kDisplayCsPin, LOW);
    digitalWrite(config::kDisplayDcPin, LOW);
    g_spi.writeBytes(cmd, cmdSize);
    digitalWrite(config::kDisplayDcPin, HIGH);
    if (dataSize > 0) g_spi.writeBytes(data, dataSize);
    digitalWrite(config::kDisplayCsPin, HIGH);
    g_spi.endTransaction();
}

void sendCmd(lv_display_t*, const uint8_t* cmd, size_t cmdSize, const uint8_t* param,
             size_t paramSize) {
    spiWrite(cmd, cmdSize, param, paramSize);
}

void sendColor(lv_display_t* disp, const uint8_t* cmd, size_t cmdSize, uint8_t* param,
               size_t paramSize) {
    spiWrite(cmd, cmdSize, param, paramSize);
    lv_display_flush_ready(disp);
}

// ---- setup ----

bool startLvgl() {
    bool poolInPsram;
    g_pool = allocPreferPsram(LV_MEM_SIZE, poolInPsram);
    if (g_pool == nullptr) {
        logLine("[display] ERROR no memory for the LVGL pool (%u bytes)",
                static_cast<unsigned>(LV_MEM_SIZE));
        return false;
    }

    pinMode(config::kDisplayCsPin, OUTPUT);
    pinMode(config::kDisplayDcPin, OUTPUT);
    pinMode(config::kDisplayRstPin, OUTPUT);
    pinMode(config::kDisplayBacklightPin, OUTPUT);
    digitalWrite(config::kDisplayCsPin, HIGH);
    digitalWrite(config::kDisplayBacklightPin, LOW);
    digitalWrite(config::kDisplayRstPin, LOW);
    delayMs(10);
    digitalWrite(config::kDisplayRstPin, HIGH);
    delayMs(120);
    g_spi.begin(config::kDisplaySckPin, -1, config::kDisplayMosiPin, -1);  // CS driven manually

    lv_init();
    lv_tick_set_cb(tickMs);
    lv_delay_set_cb(delayMs);
    lv_log_register_print_cb(lvglLog);

    // The panel is write-only over SPI, so this succeeds even with no display attached.
    lv_display_t* disp =
        lv_st7789_create(kPanelWidth, kPanelHeight, LV_LCD_FLAG_NONE, sendCmd, sendColor);
    if (disp == nullptr) {
        logLine("[display] ERROR lv_st7789_create failed");
        return false;
    }
    lv_st7789_set_invert(disp, config::kDisplayInvertColors);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_rotation(disp, config::kDisplayRotate270 ? LV_DISPLAY_ROTATION_270
                                                            : LV_DISPLAY_ROTATION_90);

    // One full-frame buffer in PSRAM; partial mode only re-renders the changed eye areas.
    size_t bufBytes = FACE_SCREEN_W * FACE_SCREEN_H * 2;
    bool bufInPsram;
    void* buf = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bufInPsram = buf != nullptr;
    if (buf == nullptr) {  // no PSRAM: a 1/10-screen buffer in internal RAM
        bufBytes = FACE_SCREEN_W * (FACE_SCREEN_H / 10) * 2;
        buf = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (buf == nullptr) {
        logLine("[display] ERROR no memory for the draw buffer");
        return false;
    }
    lv_display_set_buffers(disp, buf, nullptr, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    face_init(lv_screen_active());
    lv_timer_handler();  // first frame before the backlight comes on
    digitalWrite(config::kDisplayBacklightPin, HIGH);

    logLine("[display] ST7789 %dx%d, LVGL pool %u KB (%s), draw buffer %u KB (%s)",
            FACE_SCREEN_W, FACE_SCREEN_H, static_cast<unsigned>(LV_MEM_SIZE / 1024),
            poolInPsram ? "PSRAM" : "internal", static_cast<unsigned>(bufBytes / 1024),
            bufInPsram ? "PSRAM" : "internal");
    logLine("[display] free heap: internal %u KB, PSRAM %u KB",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    return true;
}

void apply(const proto::Message& msg) {
    if (msg.type == proto::MessageType::Mood) {
        face_set_mood(proto::moodName(msg.mood), msg.intensity);
    } else if (msg.type == proto::MessageType::Anim && !face_play_anim(msg.anim)) {
        logLine("[display] WARN face has no anim '%s'", msg.anim);
    }
}

void faceTask(void*) {
    if (!startLvgl()) {
        g_disabled = true;
        logLine("[display] face disabled; link keeps running");
        vTaskDelete(nullptr);
        return;
    }
    uint32_t prev = millis();
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        proto::Message msg;
        while (xQueueReceive(g_queue, &msg, 0) == pdTRUE) apply(msg);
        const uint32_t now = millis();
        face_tick(now - prev);
        prev = now;
        lv_timer_handler();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kFramePeriodMs));
    }
}

}  // namespace

void begin() {
    g_queue = xQueueCreate(kQueueDepth, sizeof(proto::Message));
    if (g_queue == nullptr ||
        xTaskCreate(faceTask, "face", kTaskStackBytes, nullptr, kTaskPriority, nullptr) != pdPASS) {
        logLine("[display] ERROR cannot start the face task; link keeps running");
    }
}

void post(const proto::Message& msg) {
    // Queue even while the face is still starting; it drains the queue once LVGL is up.
    if (g_queue == nullptr || g_disabled) return;  // face never started (logged at boot)
    if (xQueueSend(g_queue, &msg, 0) != pdTRUE) {
        logLine("[display] WARN face queue full (%u), dropping a message",
                static_cast<unsigned>(kQueueDepth));
    }
}

}  // namespace display
}  // namespace petbot

extern "C" void* petbot_lv_pool_alloc(size_t size) {
    return size <= LV_MEM_SIZE ? petbot::display::g_pool : nullptr;
}
