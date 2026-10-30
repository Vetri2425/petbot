// Host-side (PlatformIO "native" env) tests for src/protocol.cpp.
// Runs protocol/test_vectors.txt, the same file the Python tests use.
#include <unity.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "protocol.h"

using namespace petbot::protocol;

namespace {

struct Vector {
    int lineno;
    bool valid;
    std::string raw;
    std::string note;  // VALID: canonical encoding, INVALID: reason
};

std::vector<Vector> g_vectors;
std::string g_protocolMd;

// pio test runs from firmware/; also allow the repo root.
std::string readRepoFile(const char* relPath) {
    const char* prefixes[] = {"../", "", "../../"};
    for (const char* prefix : prefixes) {
        std::ifstream in(std::string(prefix) + relPath, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }
    }
    return std::string();
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool unescape(const std::string& in, std::string& out) {
    out.clear();
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\') {
            out += in[i];
            continue;
        }
        if (i + 1 >= in.size()) return false;
        const char e = in[++i];
        if (e == '\\') {
            out += '\\';
        } else if (e == 'n') {
            out += '\n';
        } else if (e == 'r') {
            out += '\r';
        } else if (e == 't') {
            out += '\t';
        } else if (e == 'x' && i + 2 < in.size() && hexValue(in[i + 1]) >= 0 &&
                   hexValue(in[i + 2]) >= 0) {
            out += static_cast<char>(hexValue(in[i + 1]) * 16 + hexValue(in[i + 2]));
            i += 2;
        } else {
            return false;
        }
    }
    return true;
}

bool loadVectors(std::string& error) {
    const std::string text = readRepoFile("protocol/test_vectors.txt");
    if (text.empty()) {
        error = "cannot read protocol/test_vectors.txt";
        return false;
    }
    std::istringstream lines(text);
    std::string line;
    int lineno = 0;
    while (std::getline(lines, line)) {
        ++lineno;
        if (line.empty() || line[0] == '#') continue;
        const size_t sp = line.find(' ');
        const std::string kind = line.substr(0, sp);
        if (sp == std::string::npos || (kind != "VALID" && kind != "INVALID")) {
            error = "bad vector line " + std::to_string(lineno);
            return false;
        }
        const std::string rest = line.substr(sp + 1);
        const size_t hash = rest.find("  #");
        Vector v;
        v.lineno = lineno;
        v.valid = kind == "VALID";
        if (!unescape(rest.substr(0, hash), v.raw)) {
            error = "bad escape on line " + std::to_string(lineno);
            return false;
        }
        if (hash != std::string::npos) {
            v.note = rest.substr(hash + 3);
            const size_t first = v.note.find_first_not_of(' ');
            v.note = first == std::string::npos ? "" : v.note.substr(first);
        }
        if (v.valid) {
            if (v.note.compare(0, 3, "=> ") != 0) {
                error = "VALID without '=> ' on line " + std::to_string(lineno);
                return false;
            }
            v.note = v.note.substr(3);
        }
        g_vectors.push_back(v);
    }
    return true;
}

std::string encodeToString(const Message& msg) {
    char buf[kMaxLineBytes + 1];
    const size_t n = encode(msg, buf, sizeof(buf));
    return std::string(buf, n);
}

void reportFailures(const std::vector<std::string>& failures) {
    for (const std::string& f : failures) UnityPrint(("\n  " + f).c_str());
    if (!failures.empty()) UNITY_PRINT_EOL();
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_vectors_loaded() {
    TEST_ASSERT_TRUE_MESSAGE(g_vectors.size() >= 80, "expected at least 80 vectors");
    size_t valid = 0;
    for (const Vector& v : g_vectors) valid += v.valid ? 1 : 0;
    char msg[64];
    snprintf(msg, sizeof(msg), "%u vectors: %u valid, %u invalid",
             static_cast<unsigned>(g_vectors.size()), static_cast<unsigned>(valid),
             static_cast<unsigned>(g_vectors.size() - valid));
    TEST_MESSAGE(msg);
}

void test_vectors_parse() {
    std::vector<std::string> failures;
    for (const Vector& v : g_vectors) {
        Message msg;
        ParseError err;
        const bool ok = parse(v.raw.data(), v.raw.size(), msg, err);
        const std::string where = "line " + std::to_string(v.lineno) + ": ";
        if (v.valid && !ok) {
            failures.push_back(where + "rejected valid line (" + errorName(err) + ")");
        } else if (!v.valid && ok) {
            failures.push_back(where + "accepted invalid line (" + v.note + ")");
        } else if (v.valid && encodeToString(msg) != v.note + "\n") {
            std::string got = encodeToString(msg);
            if (!got.empty()) got.pop_back();  // drop the '\n'
            failures.push_back(where + "encoded as '" + got + "', want '" + v.note + "'");
        }
    }
    reportFailures(failures);
    TEST_ASSERT_EQUAL_MESSAGE(0, failures.size(), "C++ parser disagrees with test vectors");
}

// Feed every vector byte by byte through LineAssembler: exactly the VALID ones come out.
// Skipped: an LF inside the line, and a lone trailing CR (adding LF would make it CR LF).
void test_vectors_through_line_assembler() {
    LineAssembler assembler;
    std::vector<std::string> expected, received;
    for (const Vector& v : g_vectors) {
        const size_t inner = v.raw.find('\n');
        if (inner != std::string::npos && inner + 1 != v.raw.size()) continue;
        if (!v.raw.empty() && v.raw.back() == '\r') continue;
        if (v.valid) expected.push_back(v.note + "\n");
        std::string bytes = v.raw;
        if (bytes.empty() || bytes.back() != '\n') bytes += '\n';
        for (char c : bytes) {
            if (assembler.push(static_cast<uint8_t>(c)) != LineAssembler::Result::Line) continue;
            Message msg;
            if (parse(assembler.line(), assembler.length(), msg)) {
                received.push_back(encodeToString(msg));
            }
        }
    }
    TEST_ASSERT_EQUAL(expected.size(), received.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), received[i].c_str());
    }
}

void test_round_trip_moods() {
    for (size_t m = 0; m < static_cast<size_t>(MoodId::kCount); ++m) {
        for (int h = 0; h <= 100; ++h) {
            const Message in = makeMood(static_cast<MoodId>(m), static_cast<uint8_t>(h));
            const std::string line = encodeToString(in);
            Message out;
            TEST_ASSERT_TRUE_MESSAGE(parse(line.data(), line.size(), out), line.c_str());
            TEST_ASSERT_EQUAL(static_cast<int>(MessageType::Mood), static_cast<int>(out.type));
            TEST_ASSERT_EQUAL(m, static_cast<size_t>(out.mood));
            TEST_ASSERT_EQUAL(h, out.intensity);
            TEST_ASSERT_EQUAL_STRING(line.c_str(), encodeToString(out).c_str());
        }
    }
}

void test_round_trip_events() {
    size_t total = 0;
    for (size_t s = 0; s < static_cast<size_t>(EventSource::kCount); ++s) {
        const EventSource source = static_cast<EventSource>(s);
        for (uint8_t d = 0; d < detailCount(source); ++d) {
            const std::string line = encodeToString(makeEvent(source, d));
            const std::string want = std::string("EVT ") + sourceName(source) + " " +
                                     detailName(source, d) + "\n";
            TEST_ASSERT_EQUAL_STRING(want.c_str(), line.c_str());
            Message out;
            TEST_ASSERT_TRUE(parse(line.data(), line.size(), out));
            TEST_ASSERT_EQUAL(s, static_cast<size_t>(out.source));
            TEST_ASSERT_EQUAL(d, out.detail);
            ++total;
        }
    }
    TEST_ASSERT_EQUAL(8, total);
}

void test_round_trip_anim() {
    const std::string longest(kMaxAnimNameLen, 'a');
    const char* names[] = {"wave_left", "wave_right", "a", "0", "_", longest.c_str()};
    for (const char* name : names) {
        Message in;
        TEST_ASSERT_TRUE(makeAnim(name, in));
        const std::string line = encodeToString(in);
        TEST_ASSERT_EQUAL_STRING((std::string("ANIM ") + name + "\n").c_str(), line.c_str());
        Message out;
        TEST_ASSERT_TRUE(parse(line.data(), line.size(), out));
        TEST_ASSERT_EQUAL_STRING(name, out.anim);
    }
    TEST_ASSERT_EQUAL(kMaxLineBytes, std::string("ANIM ").size() + longest.size() + 1);
}

void test_find_event() {
    EventSource source;
    uint8_t detail;
    TEST_ASSERT_TRUE(findEvent("touch", "head", source, detail));
    TEST_ASSERT_EQUAL(static_cast<int>(EventSource::touch), static_cast<int>(source));
    TEST_ASSERT_EQUAL(static_cast<int>(TouchDetail::head), detail);
    TEST_ASSERT_TRUE(findEvent("imu", "fall", source, detail));
    TEST_ASSERT_EQUAL(static_cast<int>(ImuDetail::fall), detail);
    TEST_ASSERT_FALSE(findEvent("imu", "head", source, detail));
    TEST_ASSERT_FALSE(findEvent("sonar", "ping", source, detail));
    MoodId mood;
    TEST_ASSERT_TRUE(findMood("surprised", mood));
    TEST_ASSERT_EQUAL(static_cast<int>(MoodId::surprised), static_cast<int>(mood));
    TEST_ASSERT_FALSE(findMood("grumpy", mood));
}

void test_encode_rejects_invalid_messages() {
    char buf[kMaxLineBytes + 1];
    TEST_ASSERT_EQUAL(0, encode(makeMood(MoodId::happy, 101), buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, encode(makeMood(MoodId::happy, 255), buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, encode(makeMood(MoodId::kCount, 50), buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, encode(makeEvent(EventSource::imu, 4), buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, encode(makeEvent(EventSource::kCount, 0), buf, sizeof(buf)));
    Message anim;
    TEST_ASSERT_FALSE(makeAnim("Wave", anim));
    TEST_ASSERT_FALSE(makeAnim("", anim));
    TEST_ASSERT_FALSE(makeAnim(std::string(kMaxAnimNameLen + 1, 'a').c_str(), anim));
}

void test_encode_buffer_too_small() {
    const Message msg = makeMood(MoodId::happy, 80);  // "MOOD happy 0.80\n" = 16 bytes
    char buf[32];
    TEST_ASSERT_EQUAL(16, encode(msg, buf, 17));  // 16 + NUL fits
    TEST_ASSERT_EQUAL_STRING("MOOD happy 0.80\n", buf);
    TEST_ASSERT_EQUAL(0, encode(msg, buf, 16));
    TEST_ASSERT_EQUAL(0, encode(msg, buf, 0));
}

void test_line_assembler_discards_overlong_line() {
    LineAssembler a;
    int overflows = 0, lines = 0;
    std::string last;
    const std::string input = std::string(300, 'a') + "\nEVT touch head\n";
    for (char c : input) {
        const LineAssembler::Result r = a.push(static_cast<uint8_t>(c));
        if (r == LineAssembler::Result::Overflow) ++overflows;
        if (r == LineAssembler::Result::Line) {
            ++lines;
            last.assign(a.line(), a.length());
        }
    }
    TEST_ASSERT_EQUAL(1, overflows);
    TEST_ASSERT_EQUAL(1, lines);
    TEST_ASSERT_EQUAL_STRING("EVT touch head\n", last.c_str());
}

void test_line_assembler_limits() {
    LineAssembler a;
    // 127 bytes + '\n' = 128: a line.
    for (int i = 0; i < 127; ++i) {
        TEST_ASSERT_EQUAL(static_cast<int>(LineAssembler::Result::Pending),
                          static_cast<int>(a.push('a')));
    }
    TEST_ASSERT_EQUAL(static_cast<int>(LineAssembler::Result::Line),
                      static_cast<int>(a.push('\n')));
    TEST_ASSERT_EQUAL(128, a.length());
    // 128 bytes without '\n': overflow on the 128th.
    for (int i = 0; i < 127; ++i) a.push('a');
    TEST_ASSERT_EQUAL(static_cast<int>(LineAssembler::Result::Overflow),
                      static_cast<int>(a.push('a')));
}

// PROTOCOL.md must list exactly the names in protocol.h (Python checks the same doc).
void test_names_match_protocol_md() {
    TEST_ASSERT_FALSE_MESSAGE(g_protocolMd.empty(), "cannot read protocol/PROTOCOL.md");
    std::string moods = "Mood names: ";
    for (size_t m = 0; m < static_cast<size_t>(MoodId::kCount); ++m) {
        if (m) moods += ", ";
        moods += std::string("`") + moodName(static_cast<MoodId>(m)) + "`";
    }
    TEST_ASSERT_TRUE_MESSAGE(g_protocolMd.find(moods + "\n") != std::string::npos, moods.c_str());
    for (size_t s = 0; s < static_cast<size_t>(EventSource::kCount); ++s) {
        const EventSource source = static_cast<EventSource>(s);
        std::string row = std::string("| `") + sourceName(source) + "` | ";
        for (uint8_t d = 0; d < detailCount(source); ++d) {
            if (d) row += ", ";
            row += std::string("`") + detailName(source, d) + "`";
        }
        row += " |\n";
        TEST_ASSERT_TRUE_MESSAGE(g_protocolMd.find(row) != std::string::npos, row.c_str());
    }
}

int main() {
    std::string error;
    const bool loaded = loadVectors(error);
    g_protocolMd = readRepoFile("protocol/PROTOCOL.md");

    UNITY_BEGIN();
    if (!loaded) {
        UnityPrint(error.c_str());
        UNITY_PRINT_EOL();
    }
    RUN_TEST(test_vectors_loaded);
    RUN_TEST(test_vectors_parse);
    RUN_TEST(test_vectors_through_line_assembler);
    RUN_TEST(test_round_trip_moods);
    RUN_TEST(test_round_trip_events);
    RUN_TEST(test_round_trip_anim);
    RUN_TEST(test_find_event);
    RUN_TEST(test_encode_rejects_invalid_messages);
    RUN_TEST(test_encode_buffer_too_small);
    RUN_TEST(test_line_assembler_discards_overlong_line);
    RUN_TEST(test_line_assembler_limits);
    RUN_TEST(test_names_match_protocol_md);
    return UNITY_END();
}
