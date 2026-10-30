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
#include <vector>

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

// ---- images ----

struct Image {
    uint32_t w = 0, h = 0;
    std::string rgba;  // w * h * 4 bytes
};

// Full render of `obj` (independent of what's on the display).
bool capture(lv_obj_t* obj, Image& img) {
    lv_draw_buf_t* buf = lv_snapshot_take(obj, LV_COLOR_FORMAT_ARGB8888);
    if (buf == nullptr) return false;
    img.w = buf->header.w;
    img.h = buf->header.h;
    img.rgba.assign(static_cast<size_t>(img.w) * img.h * 4, '\0');
    for (uint32_t y = 0; y < img.h; ++y) {
        const uint8_t* src = buf->data + y * buf->header.stride;  // B, G, R, A
        for (uint32_t x = 0; x < img.w; ++x) {
            char* dst = &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4];
            dst[0] = static_cast<char>(src[x * 4 + 2]);
            dst[1] = static_cast<char>(src[x * 4 + 1]);
            dst[2] = static_cast<char>(src[x * 4 + 0]);
            dst[3] = static_cast<char>(src[x * 4 + 3]);
        }
    }
    lv_draw_buf_destroy(buf);
    return true;
}

bool writePng(const Image& img, const std::string& path) {
    unsigned char* png = nullptr;
    size_t pngSize = 0;
    const unsigned err = lodepng_encode32(
        &png, &pngSize, reinterpret_cast<const unsigned char*>(img.rgba.data()), img.w, img.h);
    if (err != 0) {
        fprintf(stderr, "error: encoding %s: %s\n", path.c_str(), lodepng_error_text(err));
        lv_free(png);
        return false;
    }
    FILE* f = fopen(path.c_str(), "wb");
    const bool written = f != nullptr && fwrite(png, 1, pngSize, f) == pngSize;
    const bool closed = f != nullptr && fclose(f) == 0;
    lv_free(png);
    if (!written || !closed) {
        fprintf(stderr, "error: writing %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }
    printf("[snapshot] %s\n", path.c_str());
    return true;
}

// Capture the face with a small caption in its bottom-left corner (for sheets and strips).
bool captureLabeled(lv_obj_t* face, const char* caption, Image& img) {
    lv_obj_t* label = lv_label_create(face);
    lv_label_set_text(label, caption);
    lv_obj_set_style_text_color(label, lv_color_hex(0x9AA0A6), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 6, -4);
    lv_obj_update_layout(label);
    const bool ok = capture(face, img);
    lv_obj_delete(label);
    return ok;
}

// Tile images into one sheet, `cols` per row, with a 4 px gray gutter.
Image tile(const std::vector<Image>& cells, uint32_t cols) {
    const uint32_t gap = 4, cw = cells[0].w, ch = cells[0].h;
    const uint32_t rows = (static_cast<uint32_t>(cells.size()) + cols - 1) / cols;
    Image out;
    out.w = cols * cw + (cols + 1) * gap;
    out.h = rows * ch + (rows + 1) * gap;
    out.rgba.assign(static_cast<size_t>(out.w) * out.h * 4, '\0');
    for (size_t i = 0; i < out.rgba.size(); i += 4) {
        out.rgba[i] = out.rgba[i + 1] = out.rgba[i + 2] = static_cast<char>(0x30);
        out.rgba[i + 3] = static_cast<char>(0xFF);
    }
    for (size_t i = 0; i < cells.size(); ++i) {
        const uint32_t x0 = gap + (i % cols) * (cw + gap), y0 = gap + (i / cols) * (ch + gap);
        for (uint32_t y = 0; y < ch; ++y) {
            memcpy(&out.rgba[((y0 + y) * static_cast<size_t>(out.w) + x0) * 4],
                   &cells[i].rgba[static_cast<size_t>(y) * cw * 4], static_cast<size_t>(cw) * 4);
        }
    }
    return out;
}

// ---- checks (run in --snapshot mode, so CI enforces them) ----

// Mean x of the cyan eye pixels in a full render of `obj`.
bool eyesCenterX(lv_obj_t* obj, float& cx) {
    Image img;
    if (!capture(obj, img)) return false;
    double sum = 0;
    size_t count = 0;
    for (uint32_t y = 0; y < img.h; ++y) {
        for (uint32_t x = 0; x < img.w; ++x) {
            const uint8_t* px = reinterpret_cast<const uint8_t*>(&img.rgba[(y * img.w + x) * 4]);
            if (px[1] > 128 && px[2] > 128 && px[0] < 100) {  // cyan: G and B high, R low
                sum += x;
                ++count;
            }
        }
    }
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

// Every anim in the protocol's known list must be implemented by the face.
bool checkAllAnimsImplemented() {
    bool ok = true;
    for (size_t i = 0; i < proto::animCount(); ++i) {
        if (!face_play_anim(proto::animName(i))) {
            printf("[check] anim %s: NOT IMPLEMENTED\n", proto::animName(i));
            ok = false;
        }
    }
    face_tick(5000);
    printf("[check] all %u known anims implemented: %s\n",
           static_cast<unsigned>(proto::animCount()), ok ? "OK" : "FAIL");
    return ok;
}

// The face only invalidates changed areas. Refresh the real display every frame and
// compare its accumulated framebuffer with a full render: any stale pixel is a bug.
bool checkPartialRedraw(lv_display_t* disp, lv_obj_t* face, const std::string& dir) {
    face_set_idle(true);
    const char* moods[] = {"happy", "sad", "angry", "sleepy", "curious", "surprised", "neutral"};
    std::vector<const char*> anims;
    for (size_t i = 0; i < proto::animCount(); ++i) anims.push_back(proto::animName(i));
    size_t worst = 0;
    int compared = 0, worst_frame = -1;
    lv_area_t worst_area = {0, 0, 0, 0};
    Image worst_full, worst_shown;
    for (int frame = 0; frame < 900; ++frame) {
        if (frame % 50 == 0) face_set_mood(moods[(frame / 50) % 7], frame % 100 ? 60 : 100);
        if (frame % 30 == 15) face_play_anim(anims[(frame / 30) % anims.size()]);
        face_tick(33);
        lv_refr_now(disp);
        if (frame % 5 != 4) continue;
        lv_draw_buf_t* full = lv_snapshot_take(face, LV_COLOR_FORMAT_ARGB8888);
        lv_draw_buf_t* shown = lv_display_get_buf_active(disp);
        if (full == nullptr || shown == nullptr) return false;
        const uint32_t bpp = lv_color_format_get_size(lv_display_get_color_format(disp));
        size_t diff = 0;
        lv_area_t area = {INT32_MAX, INT32_MAX, -1, -1};
        for (uint32_t y = 0; y < full->header.h; ++y) {
            const uint8_t* a = full->data + y * full->header.stride;
            const uint8_t* b = shown->data + y * shown->header.stride;
            for (uint32_t x = 0; x < full->header.w; ++x) {
                for (int ch = 0; ch < 3; ++ch) {
                    // Translucent layers (blush) blend with slightly different rounding into
                    // the ARGB8888 snapshot and the XRGB8888 display: allow a few levels. A
                    // stale pixel differs by tens or hundreds.
                    if (abs(a[x * 4 + ch] - b[x * bpp + ch]) > 8) {
                        ++diff;
                        area.x1 = LV_MIN(area.x1, (int32_t)x);
                        area.y1 = LV_MIN(area.y1, (int32_t)y);
                        area.x2 = LV_MAX(area.x2, (int32_t)x);
                        area.y2 = LV_MAX(area.y2, (int32_t)y);
                        break;
                    }
                }
            }
        }
        if (diff > worst) {
            worst = diff;
            worst_frame = frame;
            worst_area = area;
            capture(face, worst_full);  // keep both images for the failure report
            worst_shown = worst_full;
            for (uint32_t y = 0; y < worst_shown.h; ++y) {
                const uint8_t* b = shown->data + y * shown->header.stride;
                for (uint32_t x = 0; x < worst_shown.w; ++x) {
                    char* d = &worst_shown.rgba[(static_cast<size_t>(y) * worst_shown.w + x) * 4];
                    d[0] = static_cast<char>(b[x * bpp + 2]);
                    d[1] = static_cast<char>(b[x * bpp + 1]);
                    d[2] = static_cast<char>(b[x * bpp + 0]);
                    d[3] = static_cast<char>(0xFF);
                }
            }
        }
        lv_draw_buf_destroy(full);
        ++compared;
    }
    face_set_idle(false);
    const bool pass = worst == 0;
    printf("[check] partial redraw: %d frames compared, worst %zu stale pixels: %s\n", compared,
           worst, pass ? "OK" : "FAIL");
    if (!pass) {
        printf("[check]   worst at frame %d, area x %d-%d y %d-%d (see redraw_*.png)\n",
               worst_frame, (int)worst_area.x1, (int)worst_area.x2, (int)worst_area.y1,
               (int)worst_area.y2);
        writePng(worst_full, dir + "/redraw_expected.png");
        writePng(worst_shown, dir + "/redraw_shown.png");
    }
    return pass;
}

// ---- snapshots ----

// Settle on a mood: long enough for every spring to come to rest.
void settle(const char* mood, uint8_t intensity) {
    face_set_mood(mood, intensity);
    face_tick(FACE_MOOD_SETTLE_MS * 3);
}

void clearFx() {
    face_show_glyph(FACE_GLYPH_NONE, 0);
    face_tick(6000);  // glyph pops back, accessories finish
}

bool saveFace(lv_obj_t* face, const std::string& path) {
    Image img;
    return capture(face, img) && writePng(img, path);
}

// A strip of labeled frames at the given times (ms) after `start` runs.
bool saveStrip(lv_obj_t* face, const std::string& path, const char* title, void (*start)(),
               const std::vector<uint32_t>& times = {0, 40, 80, 120, 200}) {
    std::vector<Image> cells;
    uint32_t done = 0;
    start();
    for (uint32_t t : times) {
        face_tick(t - done);
        done = t;
        char caption[64];
        snprintf(caption, sizeof(caption), "%s  %u ms", title, static_cast<unsigned>(t));
        Image img;
        if (!captureLabeled(face, caption, img)) return false;
        cells.push_back(img);
    }
    return writePng(tile(cells, 5), path);
}

int snapshotStyle(lv_display_t* disp, lv_obj_t* face, const std::string& dir) {
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "error: cannot create %s: %s\n", dir.c_str(), strerror(errno));
        return 1;
    }
    face_set_idle(false);
    face_tick(1000);
    bool ok = true;
    char name[96];
    std::vector<Image> sheet;

    // Every protocol mood must exist in the face (catches drift between the two lists).
    for (size_t m = 0; m < static_cast<size_t>(proto::MoodId::kCount); ++m) {
        const char* mood = proto::moodName(static_cast<proto::MoodId>(m));
        const uint8_t levels[] = {50, 100};
        for (uint8_t level : levels) {
            if (!face_set_mood(mood, level)) {
                fprintf(stderr, "error: face has no mood '%s'\n", mood);
                return 1;
            }
            face_tick(FACE_MOOD_SETTLE_MS * 3);
            snprintf(name, sizeof(name), "/mood_%u_%s_%03u.png", static_cast<unsigned>(m + 1),
                     mood, level);
            ok &= saveFace(face, dir + name);
            Image img;
            snprintf(name, sizeof(name), "mood %s %u", mood, static_cast<unsigned>(level));
            ok &= captureLabeled(face, name, img);
            sheet.push_back(img);
        }
    }

    // Glyph eyes, settled after their pop.
    settle("neutral", 100);
    for (int g = FACE_GLYPH_HEART; g < FACE_GLYPH_COUNT; ++g) {
        face_show_glyph(static_cast<face_glyph_t>(g), 0);
        face_tick(1500);
        snprintf(name, sizeof(name), "/glyph_%s.png", face_glyph_name(static_cast<face_glyph_t>(g)));
        ok &= saveFace(face, dir + name);
        Image img;
        snprintf(name, sizeof(name), "glyph %s", face_glyph_name(static_cast<face_glyph_t>(g)));
        ok &= captureLabeled(face, name, img);
        sheet.push_back(img);
    }
    clearFx();

    // Accessories, at a representative moment of their life.
    const float when[FACE_ACC_COUNT] = {0.4f, 0.55f, 0.5f, 0.55f, 0.45f, 0.35f, 0.5f};
    const uint32_t life[FACE_ACC_COUNT] = {FACE_BLUSH_MS,    FACE_TEARS_MS,    FACE_SWEAT_MS,
                                           FACE_ZZZ_MS,      FACE_QUESTION_MS, FACE_EXCLAIM_MS,
                                           FACE_MUSIC_MS};
    for (int a = 0; a < FACE_ACC_COUNT; ++a) {
        const face_accessory_t acc = static_cast<face_accessory_t>(a);
        settle(acc == FACE_ACC_ZZZ ? "sleepy" : "neutral", 100);
        face_show_accessory(acc);
        face_tick(static_cast<uint32_t>(life[a] * when[a]));
        snprintf(name, sizeof(name), "/accessory_%s.png", face_accessory_name(acc));
        ok &= saveFace(face, dir + name);
        Image img;
        snprintf(name, sizeof(name), "accessory %s", face_accessory_name(acc));
        ok &= captureLabeled(face, name, img);
        sheet.push_back(img);
        clearFx();
    }
    // Emoji faces that aren't a mood: the scared reflex face (emoji style; filled shows its mood).
    settle("neutral", 100);
    face_play_anim("scared");
    face_tick(600);
    {
        Image img;
        ok &= captureLabeled(face, "anim scared", img);
        sheet.push_back(img);
        ok &= saveFace(face, dir + "/anim_scared.png");
    }
    face_tick(3000);
    ok &= writePng(tile(sheet, 6), dir + "/contact_sheet.png");

    // Mid-transition frames (springs: these overshoot and settle).
    const struct {
        const char* from;
        const char* to;
    } transitions[] = {{"neutral", "happy"}, {"happy", "sad"}, {"neutral", "surprised"}};
    for (const auto& tr : transitions) {
        settle(tr.from, 100);
        face_set_mood(tr.to, 100);
        uint32_t done = 0;
        const unsigned at_ms[] = {80, 160, 320};
        for (unsigned at : at_ms) {
            face_tick(at - done);
            done = at;
            snprintf(name, sizeof(name), "/transition_%s_to_%s_%03ums.png", tr.from, tr.to, at);
            ok &= saveFace(face, dir + name);
        }
    }

    // Motion strips at 0/40/80/120/200 ms: one spring transition, one saccade.
    settle("neutral", 100);
    ok &= saveStrip(face, dir + "/strip_spring_neutral_to_surprised.png", "neutral -> surprised",
                    [] { face_set_mood("surprised", 100); });
    settle("neutral", 100);
    ok &= saveStrip(face, dir + "/strip_saccade_look_left.png", "saccade look_left",
                    [] { face_play_anim("look_left"); });
    face_tick(3000);

    // Tears loop: sad enough to cry, frames across one drop cycle.
    settle("sad", 100);
    const uint32_t q = FACE_TEARS_PERIOD_MS / 4;
    ok &= saveStrip(face, dir + "/strip_tears_loop.png", "sad 100 tears", [] {},
                    {0, q, 2 * q, 3 * q, 4 * q});
    settle("neutral", 100);
    face_tick(4000);

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
        ok &= saveFace(face, dir + name);
    }
    face_tick(1000);

    // Look/wave animations at their peak.
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
        ok &= saveFace(face, dir + name);
        face_tick(2000);
    }
    face_tick(2000);
    ok &= checkAllAnimsImplemented();
    ok &= checkDirections(face);
    ok &= checkPartialRedraw(disp, face, dir);
    return ok ? 0 : 1;
}

// Every style gets the full set of snapshots and checks, in its own folder.
int runSnapshots(lv_display_t* disp, lv_obj_t* face, const std::string& dir) {
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "error: cannot create %s: %s\n", dir.c_str(), strerror(errno));
        return 1;
    }
    const struct {
        face_style_t style;
        const char* name;
    } styles[] = {{FACE_STYLE_EMOJI, "emoji"}, {FACE_STYLE_FILLED, "filled"}};
    int rc = 0;
    for (const auto& st : styles) {
        printf("[style] %s\n", st.name);
        face_set_style(st.style);
        rc |= snapshotStyle(disp, face, dir + "/" + st.name);
    }
    face_set_style(FACE_DEFAULT_STYLE);
    return rc;
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
