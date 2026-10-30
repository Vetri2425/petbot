// PetBot UART line protocol v1 (protocol/PROTOCOL.md), firmware side.
//
// Platform-independent: no Arduino headers, no heap, no String, no exceptions.
// Must agree with brain/src/petbot_brain/protocol.py; protocol/test_vectors.txt
// is run against both implementations.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace petbot {
namespace protocol {

const size_t kMaxLineBytes = 128;  // including the '\n'
const size_t kMaxAnimNameLen = kMaxLineBytes - 6;  // "ANIM " + name + "\n"
const uint8_t kMaxIntensity = 100;  // hundredths: 100 is "1.00" on the wire

// Allowed names. This is the only place they are defined on the C++ side;
// enums and string tables are generated from these lists.
#define PETBOT_MOODS(X) X(neutral) X(happy) X(sad) X(angry) X(sleepy) X(curious) X(surprised)
#define PETBOT_TOUCH_DETAILS(X) X(head) X(left) X(right) X(back)
#define PETBOT_IMU_DETAILS(X) X(pickup) X(shake) X(tilt) X(fall)
#define PETBOT_EVENT_SOURCES(X) X(touch, PETBOT_TOUCH_DETAILS) X(imu, PETBOT_IMU_DETAILS)
// Known animations (v1). The wire accepts any [a-z0-9_]+ name; this is the supported set.
#define PETBOT_ANIMS(X)                                                                        \
    X(blink) X(look_left) X(look_right) X(wave_left) X(wave_right) X(hearts) X(stars) X(dizzy) \
    X(zzz) X(tear) X(blush) X(sweat) X(question) X(exclaim) X(music) X(scared) X(idea)

#define PETBOT_ENUM_ITEM(name) name,
#define PETBOT_SOURCE_ENUM_ITEM(name, details) name,
enum class MoodId : uint8_t { PETBOT_MOODS(PETBOT_ENUM_ITEM) kCount };
enum class EventSource : uint8_t { PETBOT_EVENT_SOURCES(PETBOT_SOURCE_ENUM_ITEM) kCount };
enum class TouchDetail : uint8_t { PETBOT_TOUCH_DETAILS(PETBOT_ENUM_ITEM) kCount };
enum class ImuDetail : uint8_t { PETBOT_IMU_DETAILS(PETBOT_ENUM_ITEM) kCount };
#undef PETBOT_ENUM_ITEM
#undef PETBOT_SOURCE_ENUM_ITEM

const char* moodName(MoodId mood);  // nullptr if out of range
size_t animCount();
const char* animName(size_t index);  // nullptr if out of range
bool isKnownAnim(const char* name);
const char* sourceName(EventSource source);
size_t detailCount(EventSource source);
const char* detailName(EventSource source, uint8_t detail);

// Look up NUL-terminated names; false if unknown.
bool findMood(const char* name, MoodId& out);
bool findEvent(const char* source, const char* detail, EventSource& outSource,
               uint8_t& outDetail);

enum class MessageType : uint8_t { Mood, Anim, Event };

struct Message {
    MessageType type;
    MoodId mood;        // Mood
    uint8_t intensity;  // Mood, hundredths 0-100 ("0.80" is 80)
    char anim[kMaxAnimNameLen + 1];  // Anim, NUL-terminated [a-z0-9_]+
    EventSource source;  // Event
    uint8_t detail;      // Event, index into the source's detail list
};

Message makeMood(MoodId mood, uint8_t intensity);
Message makeEvent(EventSource source, uint8_t detail);
bool makeAnim(const char* name, Message& out);  // false if the name is invalid

enum class ParseError : uint8_t {
    None,
    TooLong,
    NonAscii,
    Empty,
    ControlChar,
    Spacing,
    UnknownCommand,
    ArgCount,
    BadIntensity,
    IntensityRange,
    UnknownMood,
    BadAnimName,
    UnknownSource,
    UnknownDetail,
};

const char* errorName(ParseError err);

// Parse one line of `len` bytes, with or without its LF / CR LF terminator.
// Without a terminator the line is measured as if it had one (128-byte limit).
bool parse(const char* line, size_t len, Message& out);
bool parse(const char* line, size_t len, Message& out, ParseError& err);

// Write `msg` as an LF-terminated line plus a NUL into `buf`.
// Returns the line length including the LF (not the NUL), or 0 if the message
// is invalid or `cap` is too small.
size_t encode(const Message& msg, char* buf, size_t cap);

// Assembles bytes into lines in a fixed buffer. A line longer than
// kMaxLineBytes is discarded up to and including its next '\n'.
class LineAssembler {
public:
    enum class Result : uint8_t {
        Pending,   // need more bytes
        Line,      // line() / length() hold a complete line (with its '\n')
        Overflow,  // an over-long line started being discarded (reported once per line)
    };

    Result push(uint8_t byte);
    const char* line() const { return buf_; }
    size_t length() const { return len_; }

private:
    char buf_[kMaxLineBytes];
    size_t len_ = 0;
    bool discarding_ = false;
    bool ready_ = false;
};

}  // namespace protocol
}  // namespace petbot
