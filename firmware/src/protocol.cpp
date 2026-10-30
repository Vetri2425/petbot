// PetBot UART line protocol v1, firmware side. See protocol.h.
#include "protocol.h"

#include <string.h>

namespace petbot {
namespace protocol {

namespace {

#define PETBOT_NAME_STR(name) #name,
#define PETBOT_DETAIL_ARRAY(name, details) \
    const char* const k_##name##_details[] = {details(PETBOT_NAME_STR)};
#define PETBOT_SOURCE_DEF(name, details) \
    {#name, k_##name##_details, sizeof(k_##name##_details) / sizeof(k_##name##_details[0])},

const char* const kMoodNames[] = {PETBOT_MOODS(PETBOT_NAME_STR)};
PETBOT_EVENT_SOURCES(PETBOT_DETAIL_ARRAY)

struct SourceDef {
    const char* name;
    const char* const* details;
    size_t count;
};
const SourceDef kSources[] = {PETBOT_EVENT_SOURCES(PETBOT_SOURCE_DEF)};

#undef PETBOT_NAME_STR
#undef PETBOT_DETAIL_ARRAY
#undef PETBOT_SOURCE_DEF

const size_t kMoodCount = sizeof(kMoodNames) / sizeof(kMoodNames[0]);
const size_t kSourceCount = sizeof(kSources) / sizeof(kSources[0]);
static_assert(kMoodCount == static_cast<size_t>(MoodId::kCount), "mood table");
static_assert(kSourceCount == static_cast<size_t>(EventSource::kCount), "source table");

struct Token {
    const char* p;
    size_t n;
};

bool tokenIs(const Token& t, const char* s) {
    const size_t n = strlen(s);
    return t.n == n && memcmp(t.p, s, n) == 0;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isNameChar(char c) { return (c >= 'a' && c <= 'z') || isDigit(c) || c == '_'; }

bool fail(ParseError& err, ParseError e) {
    err = e;
    return false;
}

// [0-9]+(\.[0-9]{1,2})? in the range 0.0-1.0, as hundredths 0-100.
bool parseIntensity(const Token& t, uint8_t& out, ParseError& err) {
    size_t i = 0;
    unsigned whole = 0;
    while (i < t.n && isDigit(t.p[i])) {
        if (whole <= 10) whole = whole * 10 + (t.p[i] - '0');  // > 1 is rejected anyway
        ++i;
    }
    if (i == 0) return fail(err, ParseError::BadIntensity);
    unsigned frac = 0;
    if (i < t.n) {
        if (t.p[i] != '.') return fail(err, ParseError::BadIntensity);
        const size_t fracStart = ++i;
        while (i < t.n && isDigit(t.p[i])) ++i;
        const size_t digits = i - fracStart;
        if (digits < 1 || digits > 2 || i != t.n) return fail(err, ParseError::BadIntensity);
        frac = (t.p[fracStart] - '0') * 10;
        if (digits == 2) frac += t.p[fracStart + 1] - '0';
    }
    const unsigned hundredths = whole * 100 + frac;
    if (hundredths > kMaxIntensity) return fail(err, ParseError::IntensityRange);
    out = static_cast<uint8_t>(hundredths);
    return true;
}

bool findToken(const Token& t, const char* const* names, size_t count, size_t& out) {
    for (size_t i = 0; i < count; ++i) {
        if (tokenIs(t, names[i])) {
            out = i;
            return true;
        }
    }
    return false;
}

bool validAnimName(const char* name) {
    size_t n = 0;
    while (n <= kMaxAnimNameLen && name[n] != '\0') {
        if (!isNameChar(name[n])) return false;
        ++n;
    }
    return n >= 1 && n <= kMaxAnimNameLen;
}

class Writer {
public:
    Writer(char* buf, size_t cap) : buf_(buf), cap_(cap) {}
    void ch(char c) {
        if (len_ + 1 < cap_) {  // keep room for the NUL
            buf_[len_++] = c;
        } else {
            ok_ = false;
        }
    }
    void str(const char* s) {
        while (*s) ch(*s++);
    }
    size_t finish() {
        if (!ok_ || cap_ == 0) return 0;
        buf_[len_] = '\0';
        return len_;
    }

private:
    char* buf_;
    size_t cap_;
    size_t len_ = 0;
    bool ok_ = true;
};

}  // namespace

const char* moodName(MoodId mood) {
    const size_t i = static_cast<size_t>(mood);
    return i < kMoodCount ? kMoodNames[i] : nullptr;
}

const char* sourceName(EventSource source) {
    const size_t i = static_cast<size_t>(source);
    return i < kSourceCount ? kSources[i].name : nullptr;
}

size_t detailCount(EventSource source) {
    const size_t i = static_cast<size_t>(source);
    return i < kSourceCount ? kSources[i].count : 0;
}

const char* detailName(EventSource source, uint8_t detail) {
    return detail < detailCount(source) ? kSources[static_cast<size_t>(source)].details[detail]
                                        : nullptr;
}

bool findMood(const char* name, MoodId& out) {
    size_t i;
    if (!findToken(Token{name, strlen(name)}, kMoodNames, kMoodCount, i)) return false;
    out = static_cast<MoodId>(i);
    return true;
}

bool findEvent(const char* source, const char* detail, EventSource& outSource,
               uint8_t& outDetail) {
    for (size_t s = 0; s < kSourceCount; ++s) {
        if (strcmp(source, kSources[s].name) != 0) continue;
        size_t d;
        if (!findToken(Token{detail, strlen(detail)}, kSources[s].details, kSources[s].count, d)) {
            return false;
        }
        outSource = static_cast<EventSource>(s);
        outDetail = static_cast<uint8_t>(d);
        return true;
    }
    return false;
}

Message makeMood(MoodId mood, uint8_t intensity) {
    Message m = {};
    m.type = MessageType::Mood;
    m.mood = mood;
    m.intensity = intensity;
    return m;
}

Message makeEvent(EventSource source, uint8_t detail) {
    Message m = {};
    m.type = MessageType::Event;
    m.source = source;
    m.detail = detail;
    return m;
}

bool makeAnim(const char* name, Message& out) {
    if (!validAnimName(name)) return false;
    out = Message();
    out.type = MessageType::Anim;
    strcpy(out.anim, name);  // length checked by validAnimName
    return true;
}

const char* errorName(ParseError err) {
    switch (err) {
        case ParseError::None: return "none";
        case ParseError::TooLong: return "line too long";
        case ParseError::NonAscii: return "non-ASCII bytes";
        case ParseError::Empty: return "empty line";
        case ParseError::ControlChar: return "control characters in line";
        case ParseError::Spacing: return "tokens must be separated by single spaces";
        case ParseError::UnknownCommand: return "unknown command";
        case ParseError::ArgCount: return "wrong number of arguments";
        case ParseError::BadIntensity: return "invalid intensity";
        case ParseError::IntensityRange: return "intensity must be 0.0-1.0";
        case ParseError::UnknownMood: return "unknown mood";
        case ParseError::BadAnimName: return "invalid animation name";
        case ParseError::UnknownSource: return "unknown event source";
        case ParseError::UnknownDetail: return "unknown event detail";
    }
    return "unknown error";
}

bool parse(const char* line, size_t len, Message& out) {
    ParseError err;
    return parse(line, len, out, err);
}

bool parse(const char* line, size_t len, Message& out, ParseError& err) {
    err = ParseError::None;
    const bool hasLf = len > 0 && line[len - 1] == '\n';
    if ((hasLf ? len : len + 1) > kMaxLineBytes) return fail(err, ParseError::TooLong);
    for (size_t i = 0; i < len; ++i) {
        if (static_cast<uint8_t>(line[i]) >= 0x80) return fail(err, ParseError::NonAscii);
    }

    size_t n = len;
    if (n >= 2 && line[n - 2] == '\r' && line[n - 1] == '\n') {
        n -= 2;
    } else if (hasLf) {
        n -= 1;
    }
    if (n == 0) return fail(err, ParseError::Empty);
    for (size_t i = 0; i < n; ++i) {
        if (line[i] < 0x20 || line[i] > 0x7e) return fail(err, ParseError::ControlChar);
    }

    // Split on single spaces. Keep the first 4 tokens; count all of them.
    Token tok[4];
    size_t count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= n; ++i) {
        if (i < n && line[i] != ' ') continue;
        if (i == start) return fail(err, ParseError::Spacing);
        if (count < 4) tok[count] = Token{line + start, i - start};
        ++count;
        start = i + 1;
    }
    const size_t argc = count - 1;

    if (tokenIs(tok[0], "MOOD")) {
        if (argc != 2) return fail(err, ParseError::ArgCount);
        uint8_t intensity;
        if (!parseIntensity(tok[2], intensity, err)) return false;
        size_t mood;
        if (!findToken(tok[1], kMoodNames, kMoodCount, mood)) {
            return fail(err, ParseError::UnknownMood);
        }
        out = makeMood(static_cast<MoodId>(mood), intensity);
        return true;
    }
    if (tokenIs(tok[0], "ANIM")) {
        if (argc != 1) return fail(err, ParseError::ArgCount);
        for (size_t i = 0; i < tok[1].n; ++i) {
            if (!isNameChar(tok[1].p[i])) return fail(err, ParseError::BadAnimName);
        }
        // tok[1].n <= kMaxAnimNameLen is guaranteed by the line length check.
        out = Message();
        out.type = MessageType::Anim;
        memcpy(out.anim, tok[1].p, tok[1].n);
        out.anim[tok[1].n] = '\0';
        return true;
    }
    if (tokenIs(tok[0], "EVT")) {
        if (argc != 2) return fail(err, ParseError::ArgCount);
        for (size_t s = 0; s < kSourceCount; ++s) {
            if (!tokenIs(tok[1], kSources[s].name)) continue;
            size_t d;
            if (!findToken(tok[2], kSources[s].details, kSources[s].count, d)) {
                return fail(err, ParseError::UnknownDetail);
            }
            out = makeEvent(static_cast<EventSource>(s), static_cast<uint8_t>(d));
            return true;
        }
        return fail(err, ParseError::UnknownSource);
    }
    return fail(err, ParseError::UnknownCommand);
}

size_t encode(const Message& msg, char* buf, size_t cap) {
    Writer w(buf, cap);
    switch (msg.type) {
        case MessageType::Mood: {
            const char* name = moodName(msg.mood);
            if (name == nullptr || msg.intensity > kMaxIntensity) return 0;
            const unsigned hundredths = msg.intensity;
            w.str("MOOD ");
            w.str(name);
            w.ch(' ');
            w.ch(static_cast<char>('0' + hundredths / 100));
            w.ch('.');
            w.ch(static_cast<char>('0' + (hundredths / 10) % 10));
            w.ch(static_cast<char>('0' + hundredths % 10));
            break;
        }
        case MessageType::Anim:
            if (!validAnimName(msg.anim)) return 0;
            w.str("ANIM ");
            w.str(msg.anim);
            break;
        case MessageType::Event: {
            const char* detail = detailName(msg.source, msg.detail);
            if (detail == nullptr) return 0;
            w.str("EVT ");
            w.str(sourceName(msg.source));
            w.ch(' ');
            w.str(detail);
            break;
        }
        default:
            return 0;
    }
    w.ch('\n');
    return w.finish();
}

LineAssembler::Result LineAssembler::push(uint8_t byte) {
    if (ready_) {
        len_ = 0;
        ready_ = false;
    }
    if (discarding_) {
        if (byte == '\n') discarding_ = false;
        return Result::Pending;
    }
    buf_[len_++] = static_cast<char>(byte);
    if (byte == '\n') {
        ready_ = true;
        return Result::Line;
    }
    if (len_ >= kMaxLineBytes) {
        len_ = 0;
        discarding_ = true;
        return Result::Overflow;
    }
    return Result::Pending;
}

}  // namespace protocol
}  // namespace petbot
