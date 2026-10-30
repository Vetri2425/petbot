// PetBot face simulator: firmware/lib/face in an SDL window, optionally acting as the
// ESP32 on a serial port (talks the line protocol to the brain).
//
//   face_sim                      window only; keys 1-7 force moods
//   face_sim --port /tmp/petbot-esp   also send EVTs to / receive MOOD/ANIM from the brain
//   SDL_VIDEODRIVER=dummy face_sim --snapshot <dir>   write PNGs and exit
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <string>

#include <SDL2/SDL.h>

#include "face.h"
#include "face_style.h"
#include "lvgl.h"
#include "protocol.h"

// LVGL's bundled lodepng (built with LV_USE_LODEPNG). Its header has C++-only parts,
// so declare the two C functions used here directly. LVGL's lodepng writes files via
// lv_fs, so encode to memory (allocated with lv_malloc) and write with stdio.
extern "C" unsigned lodepng_encode32(unsigned char** out, size_t* outsize,
                                     const unsigned char* image, unsigned w, unsigned h);
extern "C" const char* lodepng_error_text(unsigned code);

namespace proto = petbot::protocol;

namespace {

const float kWindowZoom = 2.0f;

struct EventKey {
    char key;
    const char* source;
    const char* detail;
};
// Same keys as sim/fake_esp32.py.
const EventKey kEventKeys[] = {
    {'h', "touch", "head"}, {'l', "touch", "left"}, {'r', "touch", "right"},
    {'b', "touch", "back"}, {'p', "imu", "pickup"}, {'s', "imu", "shake"},
    {'t', "imu", "tilt"},   {'f', "imu", "fall"},
};

int g_port = -1;
proto::LineAssembler g_assembler;
bool g_quit = false;

// ---- serial port (pty or real) ----

bool openPort(const char* path) {
    g_port = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (g_port < 0) {
        fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    termios tio;
    if (tcgetattr(g_port, &tio) == 0) {
        cfmakeraw(&tio);
        cfsetispeed(&tio, B921600);
        cfsetospeed(&tio, B921600);
        tcsetattr(g_port, TCSANOW, &tio);
    }
    printf("[sim] link on %s\n", path);
    return true;
}

void handleLine(const char* line, size_t len) {
    proto::Message msg;
    proto::ParseError err;
    if (!proto::parse(line, len, msg, err)) {
        printf("[sim] WARN skipping invalid line (%s)\n", proto::errorName(err));
        return;
    }
    char buf[proto::kMaxLineBytes + 1];
    const size_t n = proto::encode(msg, buf, sizeof(buf));
    printf("[sim] rx %.*s\n", static_cast<int>(n - 1), buf);
    switch (msg.type) {
        case proto::MessageType::Mood:
            face_set_mood(proto::moodName(msg.mood), msg.intensity);
            break;
        case proto::MessageType::Anim:
            if (!face_play_anim(msg.anim)) printf("[sim] WARN face has no anim '%s'\n", msg.anim);
            break;
        case proto::MessageType::Event:
            printf("[sim] WARN skipping EVT sent by the brain\n");
            break;
    }
}

void pollPort() {
    if (g_port < 0) return;
    uint8_t chunk[256];
    for (;;) {
        const ssize_t n = read(g_port, chunk, sizeof(chunk));
        if (n > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                const proto::LineAssembler::Result r = g_assembler.push(chunk[i]);
                if (r == proto::LineAssembler::Result::Line) {
                    handleLine(g_assembler.line(), g_assembler.length());
                } else if (r == proto::LineAssembler::Result::Overflow) {
                    printf("[sim] WARN skipping line longer than %u bytes\n",
                           static_cast<unsigned>(proto::kMaxLineBytes));
                }
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
        // EOF or EIO: the other end went away. Keep the face running without the link.
        printf("[sim] link closed (%s)\n", n == 0 ? "EOF" : strerror(errno));
        close(g_port);
        g_port = -1;
        return;
    }
}

void sendEvent(const char* source, const char* detail) {
    proto::EventSource s;
    uint8_t d;
    if (!proto::findEvent(source, detail, s, d)) return;
    char buf[proto::kMaxLineBytes + 1];
    const size_t n = proto::encode(proto::makeEvent(s, d), buf, sizeof(buf));
    if (g_port < 0) {
        printf("[sim] no link, not sent: %.*s\n", static_cast<int>(n - 1), buf);
        return;
    }
    if (write(g_port, buf, n) != static_cast<ssize_t>(n)) {
        printf("[sim] WARN write failed: %s\n", strerror(errno));
        return;
    }
    printf("[sim] tx %.*s\n", static_cast<int>(n - 1), buf);
}

// ---- keyboard (SDL window or terminal) ----

void handleKey(uint32_t key) {
    if (key == 'q' || key == LV_KEY_ESC) {
        g_quit = true;
        return;
    }
    if (key >= '1' && key < '1' + static_cast<uint32_t>(proto::MoodId::kCount)) {
        const char* mood = proto::moodName(static_cast<proto::MoodId>(key - '1'));
        printf("[sim] key %c: mood %s 100\n", static_cast<char>(key), mood);
        face_set_mood(mood, 100);
        return;
    }
    for (const EventKey& k : kEventKeys) {
        if (key == static_cast<uint32_t>(k.key)) sendEvent(k.source, k.detail);
    }
}

void onKey(lv_event_t* e) { handleKey(lv_event_get_key(e)); }

// Keys typed in the terminal work too (no Enter needed when stdin is a terminal).
termios g_savedTio;
bool g_tioSaved = false;

void restoreTerminal() {
    if (g_tioSaved) tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTio);
}

void initStdinKeys() {
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_savedTio) == 0) {
        termios tio = g_savedTio;
        tio.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &tio);
        g_tioSaved = true;
        atexit(restoreTerminal);
    }
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
}

void pollStdinKeys() {
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c != '\n' && c != '\r') handleKey(static_cast<uint8_t>(c));
    }
}

void printKeys() {
    printf("[sim] keys:");
    for (const EventKey& k : kEventKeys) printf(" %c=%s %s", k.key, k.source, k.detail);
    printf("\n[sim]       1-%u=", static_cast<unsigned>(proto::MoodId::kCount));
    for (size_t m = 0; m < static_cast<size_t>(proto::MoodId::kCount); ++m) {
        printf("%s%s", m ? "," : "", proto::moodName(static_cast<proto::MoodId>(m)));
    }
    printf(" (intensity 100)  q=quit\n");
}

// ---- snapshots ----

bool savePng(lv_obj_t* obj, const std::string& path) {
    lv_draw_buf_t* buf = lv_snapshot_take(obj, LV_COLOR_FORMAT_ARGB8888);
    if (buf == nullptr) {
        fprintf(stderr, "error: snapshot failed for %s\n", path.c_str());
        return false;
    }
    const uint32_t w = buf->header.w, h = buf->header.h, stride = buf->header.stride;
    std::string rgba(static_cast<size_t>(w) * h * 4, '\0');
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* src = buf->data + y * stride;  // B, G, R, A
        for (uint32_t x = 0; x < w; ++x) {
            char* dst = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            dst[0] = static_cast<char>(src[x * 4 + 2]);
            dst[1] = static_cast<char>(src[x * 4 + 1]);
            dst[2] = static_cast<char>(src[x * 4 + 0]);
            dst[3] = static_cast<char>(src[x * 4 + 3]);
        }
    }
    lv_draw_buf_destroy(buf);
    unsigned char* png = nullptr;
    size_t pngSize = 0;
    const unsigned err = lodepng_encode32(
        &png, &pngSize, reinterpret_cast<const unsigned char*>(rgba.data()), w, h);
    if (err != 0) {
        fprintf(stderr, "error: encoding %s: %s\n", path.c_str(), lodepng_error_text(err));
        lv_free(png);
        return false;
    }
    FILE* f = fopen(path.c_str(), "wb");
    const bool written = f != nullptr && fwrite(png, 1, pngSize, f) == pngSize;
    if (f != nullptr && fclose(f) != 0) {
        fprintf(stderr, "error: closing %s: %s\n", path.c_str(), strerror(errno));
        lv_free(png);
        return false;
    }
    lv_free(png);
    if (!written) {
        fprintf(stderr, "error: writing %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }
    printf("[snapshot] %s\n", path.c_str());
    return true;
}

// ---- checks (run in --snapshot mode, so CI enforces them) ----

// Mean x of the cyan eye pixels in a full render of `obj`.
bool eyesCenterX(lv_obj_t* obj, float& cx) {
    lv_draw_buf_t* buf = lv_snapshot_take(obj, LV_COLOR_FORMAT_ARGB8888);
    if (buf == nullptr) return false;
    double sum = 0;
    size_t count = 0;
    for (uint32_t y = 0; y < buf->header.h; ++y) {
        const uint8_t* row = buf->data + y * buf->header.stride;  // B, G, R, A
        for (uint32_t x = 0; x < buf->header.w; ++x) {
            if (row[x * 4 + 1] > 128 && row[x * 4 + 0] > 128) {
                sum += x;
                ++count;
            }
        }
    }
    lv_draw_buf_destroy(buf);
    if (count == 0) return false;
    cx = static_cast<float>(sum / count);
    return true;
}

// ADR 0014: left/right is the robot's frame, so *_left moves the eyes toward screen-right.
bool checkDirections(lv_obj_t* face) {
    float neutral;
    face_tick(2000);
    if (!eyesCenterX(face, neutral)) return false;
    const struct {
        const char* anim;
        uint32_t at;
        float sign;  // +1: must move toward screen-right
    } cases[] = {{"look_left", FACE_LOOK_MS / 2, +1.0f},
                 {"look_right", FACE_LOOK_MS / 2, -1.0f},
                 {"wave_left", FACE_WAVE_MS / 2, +1.0f},
                 {"wave_right", FACE_WAVE_MS / 2, -1.0f}};
    bool ok = true;
    for (const auto& c : cases) {
        face_play_anim(c.anim);
        face_tick(c.at);
        float x;
        const bool measured = eyesCenterX(face, x);
        const bool pass = measured && (x - neutral) * c.sign >= 10.0f;
        printf("[check] %-10s eyes x=%.1f (neutral %.1f) -> %s: %s\n", c.anim, x, neutral,
               c.sign > 0 ? "screen-right" : "screen-left", pass ? "OK" : "FAIL");
        ok &= pass;
        face_tick(2000);
    }
    return ok;
}

// The face only invalidates changed eye areas. Refresh the real display every frame and
// compare its accumulated framebuffer with a full render: any stale pixel is a bug.
bool checkPartialRedraw(lv_display_t* disp, lv_obj_t* face) {
    face_set_idle(true);
    const char* moods[] = {"happy", "sad", "angry", "sleepy", "curious", "surprised", "neutral"};
    const char* anims[] = {"look_left", "wave_right", "blink", "look_right", "wave_left"};
    size_t worst = 0;
    int compared = 0;
    for (int frame = 0; frame < 400; ++frame) {
        if (frame % 50 == 0) face_set_mood(moods[(frame / 50) % 7], frame % 100 ? 60 : 100);
        if (frame % 70 == 35) face_play_anim(anims[(frame / 70) % 5]);
        face_tick(33);
        lv_refr_now(disp);
        if (frame % 5 != 4) continue;
        lv_draw_buf_t* full = lv_snapshot_take(face, LV_COLOR_FORMAT_ARGB8888);
        lv_draw_buf_t* shown = lv_display_get_buf_active(disp);
        if (full == nullptr || shown == nullptr) return false;
        const uint32_t bpp = lv_color_format_get_size(lv_display_get_color_format(disp));
        size_t diff = 0;
        for (uint32_t y = 0; y < full->header.h; ++y) {
            const uint8_t* a = full->data + y * full->header.stride;
            const uint8_t* b = shown->data + y * shown->header.stride;
            for (uint32_t x = 0; x < full->header.w; ++x) {
                for (int ch = 0; ch < 3; ++ch) {
                    if (abs(a[x * 4 + ch] - b[x * bpp + ch]) > 2) {
                        ++diff;
                        break;
                    }
                }
            }
        }
        lv_draw_buf_destroy(full);
        if (diff > worst) worst = diff;
        ++compared;
    }
    face_set_idle(false);
    const bool pass = worst == 0;
    printf("[check] partial redraw: %d frames compared, worst %zu stale pixels: %s\n", compared,
           worst, pass ? "OK" : "FAIL");
    return pass;
}

// Settle on a mood with no transition in progress.
void settle(const char* mood, uint8_t intensity) {
    face_set_mood(mood, intensity);
    face_tick(FACE_MOOD_TRANSITION_MS + 1);
}

int runSnapshots(lv_display_t* disp, lv_obj_t* face, const std::string& dir) {
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "error: cannot create %s: %s\n", dir.c_str(), strerror(errno));
        return 1;
    }
    face_set_idle(false);
    face_tick(1000);
    bool ok = true;
    char name[96];

    // Every protocol mood must exist in the face (catches drift between the two lists).
    for (size_t m = 0; m < static_cast<size_t>(proto::MoodId::kCount); ++m) {
        const char* mood = proto::moodName(static_cast<proto::MoodId>(m));
        const uint8_t levels[] = {50, 100};
        for (uint8_t level : levels) {
            if (!face_set_mood(mood, level)) {
                fprintf(stderr, "error: face has no mood '%s'\n", mood);
                return 1;
            }
            face_tick(FACE_MOOD_TRANSITION_MS + 1);
            snprintf(name, sizeof(name), "/mood_%u_%s_%03u.png", static_cast<unsigned>(m + 1),
                     mood, level);
            ok &= savePng(face, dir + name);
        }
    }

    // Mid-transition frames.
    const struct {
        const char* from;
        const char* to;
    } transitions[] = {{"neutral", "happy"}, {"happy", "sad"}, {"neutral", "surprised"}};
    for (const auto& tr : transitions) {
        settle(tr.from, 100);
        face_set_mood(tr.to, 100);
        uint32_t done = 0;
        const unsigned percents[] = {25, 50, 75};
        for (unsigned pct : percents) {
            const uint32_t at = FACE_MOOD_TRANSITION_MS * pct / 100;
            face_tick(at - done);
            done = at;
            snprintf(name, sizeof(name), "/transition_%s_to_%s_%02u.png", tr.from, tr.to, pct);
            ok &= savePng(face, dir + name);
        }
    }

    // Blink: closing, closed, opening.
    settle("neutral", 100);
    face_play_anim("blink");
    const struct {
        uint32_t step;
        const char* label;
    } blink[] = {{FACE_BLINK_CLOSE_MS / 2, "closing"},
                 {FACE_BLINK_CLOSE_MS / 2 + FACE_BLINK_HOLD_MS / 2, "closed"},
                 {FACE_BLINK_HOLD_MS / 2 + FACE_BLINK_OPEN_MS / 2, "opening"}};
    for (const auto& b : blink) {
        face_tick(b.step);
        snprintf(name, sizeof(name), "/blink_%s.png", b.label);
        ok &= savePng(face, dir + name);
    }
    face_tick(1000);

    // Animations at their peak.
    const struct {
        const char* anim;
        uint32_t at;
    } anims[] = {{"look_left", FACE_LOOK_MS / 2},
                 {"look_right", FACE_LOOK_MS / 2},
                 {"wave_left", FACE_WAVE_MS / (2 * FACE_WAVE_BOUNCES)},
                 {"wave_right", FACE_WAVE_MS / (2 * FACE_WAVE_BOUNCES)}};
    for (const auto& a : anims) {
        face_play_anim(a.anim);
        face_tick(a.at);
        snprintf(name, sizeof(name), "/anim_%s.png", a.anim);
        ok &= savePng(face, dir + name);
        face_tick(2000);
    }
    face_tick(2000);
    ok &= checkDirections(face);
    ok &= checkPartialRedraw(disp, face);
    return ok ? 0 : 1;
}

void usage() {
    fprintf(stderr,
            "usage: face_sim [--port <serial-device>] [--snapshot <dir>]\n"
            "  --port      act as the ESP32 on this serial port (e.g. /tmp/petbot-esp)\n"
            "  --snapshot  render moods, transitions, blink and anims to PNGs in <dir>, then exit\n");
}

}  // namespace

int main(int argc, char** argv) {
    const char* port = nullptr;
    const char* snapshotDir = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = argv[++i];
        } else if (strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) {
            snapshotDir = argv[++i];
        } else {
            usage();
            return 2;
        }
    }

    lv_init();
    lv_display_t* disp = lv_sdl_window_create(FACE_SCREEN_W, FACE_SCREEN_H);
    if (disp == nullptr) {
        fprintf(stderr, "error: cannot create SDL window (try SDL_VIDEODRIVER=dummy)\n");
        return 1;
    }
    lv_sdl_window_set_zoom(disp, kWindowZoom);
    lv_sdl_window_set_title(disp, "PetBot face");
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_t* face = face_init(lv_screen_active());

    if (snapshotDir != nullptr) return runSnapshots(disp, face, snapshotDir);

    if (port != nullptr && !openPort(port)) return 1;

    lv_indev_t* keyboard = lv_sdl_keyboard_create();
    lv_group_t* group = lv_group_create();
    lv_obj_t* keyTarget = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(keyTarget);
    lv_obj_set_size(keyTarget, 0, 0);
    lv_obj_add_event_cb(keyTarget, onKey, LV_EVENT_KEY, nullptr);
    lv_group_add_obj(group, keyTarget);
    lv_indev_set_group(keyboard, group);
    initStdinKeys();
    printKeys();

    uint32_t last = SDL_GetTicks();
    while (!g_quit) {
        const uint32_t now = SDL_GetTicks();
        lv_tick_inc(now - last);
        face_tick(now - last);
        last = now;
        pollPort();
        pollStdinKeys();
        lv_timer_handler();
        if (!lv_is_initialized()) break;  // window closed: the SDL driver de-inits LVGL
        SDL_Delay(5);
    }
    if (g_port >= 0) close(g_port);
    restoreTerminal();
    return 0;
}
