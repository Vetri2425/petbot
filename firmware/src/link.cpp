// UART link task: logs received MOOD/ANIM and hands them to the face task (display.h).
#include "link.h"

#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "protocol.h"
#include "serial_log.h"

namespace petbot {
namespace link {

namespace {

namespace proto = petbot::protocol;

HardwareSerial& uart = Serial1;
proto::LineAssembler assembler;  // used by the RX task only

// Copy bytes with non-printables escaped as \xHH, so a bad line can't garble the log.
void escapeInto(char* out, size_t cap, const char* data, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    size_t o = 0;
    for (size_t i = 0; i < len && o + 4 < cap; ++i) {
        const uint8_t c = static_cast<uint8_t>(data[i]);
        if (c >= 0x20 && c <= 0x7e && c != '\\') {
            out[o++] = static_cast<char>(c);
        } else {
            out[o++] = '\\';
            out[o++] = 'x';
            out[o++] = kHex[c >> 4];
            out[o++] = kHex[c & 0x0f];
        }
    }
    out[o] = '\0';
}

void handleLine(const char* line, size_t len) {
    proto::Message msg;
    proto::ParseError err;
    if (!proto::parse(line, len, msg, err)) {
        char escaped[proto::kMaxLineBytes * 4 + 1];
        escapeInto(escaped, sizeof(escaped), line, len);
        logLine("[link] WARN skipping invalid line (%s): %s", proto::errorName(err), escaped);
        return;
    }
    if (msg.type == proto::MessageType::Event) {
        logLine("[link] WARN skipping EVT sent by the brain");
        return;
    }
    char buf[proto::kMaxLineBytes + 1];
    const size_t n = proto::encode(msg, buf, sizeof(buf));
    logLine("[link] rx %.*s", static_cast<int>(n > 0 ? n - 1 : 0), buf);  // without '\n'
    display::post(msg);  // queued; LVGL is only touched by the face task
}

void rxTask(void*) {
    for (;;) {
        while (uart.available() > 0) {
            const int c = uart.read();
            if (c < 0) break;
            switch (assembler.push(static_cast<uint8_t>(c))) {
                case proto::LineAssembler::Result::Line:
                    handleLine(assembler.line(), assembler.length());
                    break;
                case proto::LineAssembler::Result::Overflow:
                    logLine("[link] WARN skipping line longer than %u bytes",
                            static_cast<unsigned>(proto::kMaxLineBytes));
                    break;
                case proto::LineAssembler::Result::Pending:
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

}  // namespace

void begin() {
    uart.setRxBufferSize(config::kLinkRxBufferBytes);
    uart.begin(config::kLinkBaud, SERIAL_8N1, config::kLinkRxPin, config::kLinkTxPin);
    xTaskCreate(rxTask, "link_rx", 4096, nullptr, 3, nullptr);
    logLine("[link] UART1 %u baud, TX=GPIO%d RX=GPIO%d", static_cast<unsigned>(config::kLinkBaud),
            config::kLinkTxPin, config::kLinkRxPin);
}

bool sendEvent(const char* source, const char* detail) {
    proto::EventSource s;
    uint8_t d;
    if (!proto::findEvent(source, detail, s, d)) {
        logLine("[link] WARN unknown event: %s %s", source, detail);
        return false;
    }
    char buf[proto::kMaxLineBytes + 1];
    const size_t n = proto::encode(proto::makeEvent(s, d), buf, sizeof(buf));
    uart.write(reinterpret_cast<const uint8_t*>(buf), n);
    logLine("[link] tx %.*s", static_cast<int>(n - 1), buf);  // without '\n'
    return true;
}

}  // namespace link
}  // namespace petbot
