// UART link task. For now received MOOD/ANIM commands are only logged to USB serial.
#include "link.h"

#include <Arduino.h>

#include "config.h"
#include "protocol.h"

namespace petbot {
namespace link {

namespace {

namespace proto = petbot::protocol;

HardwareSerial& uart = Serial1;
proto::LineAssembler assembler;  // used by the RX task only

// Print bytes with non-printables escaped, so a bad line can't garble the log.
void printEscaped(const char* data, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        const uint8_t c = static_cast<uint8_t>(data[i]);
        if (c >= 0x20 && c <= 0x7e && c != '\\') {
            Serial.write(c);
        } else {
            const char esc[4] = {'\\', 'x', kHex[c >> 4], kHex[c & 0x0f]};
            Serial.write(reinterpret_cast<const uint8_t*>(esc), sizeof(esc));
        }
    }
}

void handleLine(const char* line, size_t len) {
    proto::Message msg;
    proto::ParseError err;
    if (!proto::parse(line, len, msg, err)) {
        Serial.print("[link] WARN skipping invalid line (");
        Serial.print(proto::errorName(err));
        Serial.print("): ");
        printEscaped(line, len);
        Serial.println();
        return;
    }
    if (msg.type == proto::MessageType::Event) {
        Serial.println("[link] WARN skipping EVT sent by the brain");
        return;
    }
    char buf[proto::kMaxLineBytes + 1];
    const size_t n = proto::encode(msg, buf, sizeof(buf));
    Serial.print("[link] rx ");
    Serial.write(reinterpret_cast<const uint8_t*>(buf), n);  // includes '\n'
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
                    Serial.println("[link] WARN skipping line longer than 128 bytes");
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
    Serial.print("[link] UART1 ");
    Serial.print(config::kLinkBaud);
    Serial.print(" baud, TX=GPIO");
    Serial.print(config::kLinkTxPin);
    Serial.print(" RX=GPIO");
    Serial.println(config::kLinkRxPin);
}

bool sendEvent(const char* source, const char* detail) {
    proto::EventSource s;
    uint8_t d;
    if (!proto::findEvent(source, detail, s, d)) {
        Serial.print("[link] WARN unknown event: ");
        Serial.print(source);
        Serial.print(' ');
        Serial.println(detail);
        return false;
    }
    char buf[proto::kMaxLineBytes + 1];
    const size_t n = proto::encode(proto::makeEvent(s, d), buf, sizeof(buf));
    uart.write(reinterpret_cast<const uint8_t*>(buf), n);
    Serial.print("[link] tx ");
    Serial.write(reinterpret_cast<const uint8_t*>(buf), n);
    return true;
}

}  // namespace link
}  // namespace petbot
