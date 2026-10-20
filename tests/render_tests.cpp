// Copyright (C) 2026 Soni Romanov
// SPDX-License-Identifier: MIT
// Exercise the actual window-independent app state and renderer. No HWND is
// created and no message is sent to the desktop by this test executable.
#include "../src/main.cpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void seed(int count) {
    transcript.clear(); transmission.clear(); input.clear(); scrollOffset = 0;
    for (int i = 0; i < count; ++i) transcript.push_back({"ROW " + std::to_string(i)});
    advance(0);
}
std::vector<std::uint8_t> pixels(Bitmap& bitmap) {
    const int w = static_cast<int>(bitmap.GetWidth()), h = static_cast<int>(bitmap.GetHeight());
    Rect rect(0, 0, w, h); BitmapData data{};
    check(bitmap.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &data) == Ok, "read test bitmap");
    std::vector<std::uint8_t> result(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        std::copy_n(static_cast<const std::uint8_t*>(data.Scan0) + y * data.Stride,
            w * 4, result.data() + static_cast<size_t>(y) * w * 4);
    bitmap.UnlockBits(&data);
    return result;
}
void paint(Bitmap& bitmap, double seconds) {
    Graphics g(&bitmap);
    render(g, static_cast<int>(bitmap.GetWidth()), static_cast<int>(bitmap.GetHeight()), seconds);
}
struct RenderDifference {
    size_t pixels = 0;
    int largest = 0;
    bool alphaChanged = false;
};
RenderDifference compareRenders(const std::vector<std::uint8_t>& actual,
    const std::vector<std::uint8_t>& expected) {
    check(actual.size() == expected.size() && actual.size() % 4 == 0, "matching render dimensions");
    RenderDifference difference;
    for (size_t p = 0; p < actual.size(); p += 4) {
        bool changed = false;
        for (size_t c = 0; c < 3; ++c) {
            const int delta = std::abs(int(actual[p + c]) - int(expected[p + c]));
            changed |= delta != 0;
            difference.largest = std::max(difference.largest, delta);
        }
        if (changed) ++difference.pixels;
        difference.alphaChanged |= actual[p + 3] != expected[p + 3];
    }
    return difference;
}
bool equivalentRenders(const RenderDifference& difference, size_t pixelCount) {
    // Separate GDI+ rasterizations can differ very slightly on Windows
    // runners. Allow <=4 RGB levels in <=0.01% of pixels, never alpha changes.
    // This allowance does not apply to the cached-frame copy below.
    return !difference.alphaChanged && difference.largest <= 4 &&
        difference.pixels <= pixelCount / 10000;
}
void checkFresh(Bitmap& bitmap, double seconds) {
    const auto actual = pixels(bitmap);
    auto* cached = completedFrames[cursorAt(seconds) ? 1 : 0].get();
    check(cached != nullptr && actual == pixels(*cached), "cached frame is copied pixel-for-pixel");
    Bitmap fresh(bitmap.GetWidth(), bitmap.GetHeight(), PixelFormat32bppARGB);
    { Graphics g(&fresh);
      check(renderEffects(g, static_cast<int>(fresh.GetWidth()), static_cast<int>(fresh.GetHeight()),
          sceneAt(seconds), cursorAt(seconds)), "fresh render succeeds"); }
    const auto difference = compareRenders(actual, pixels(fresh));
    if (!equivalentRenders(difference, actual.size() / 4)) {
        throw std::runtime_error("Cached/fresh pixel mismatch at " + std::to_string(bitmap.GetWidth()) + "x" +
            std::to_string(bitmap.GetHeight()) + ", time " + std::to_string(seconds) +
            ": changed pixels=" + std::to_string(difference.pixels) + ", max difference=" +
            std::to_string(difference.largest) + ", alpha changed=" + std::to_string(difference.alphaChanged));
    }
}
void run() {
    // Keep clock and ETA glyphs identical across machines and repeated runs.
    _putenv_s("TZ", "UTC0");
    _tzset();
    wallNow = 1792321200;
    // Validate packaged recordings without opening an audio device.
    WAVEFORMATEX format{}; std::vector<BYTE> music;
    check(!audio::readMusic(L"missing-track.wav", format, music), "missing music fails gracefully");
    wchar_t executable[32768]{};
    check(GetModuleFileNameW(nullptr, executable, 32768) > 0, "locate packaged audio");
    const auto recordings = audio::loadKeys(std::filesystem::path(executable).parent_path() / L"assets" / L"keys");
    check(recordings.size() == 32, "all 32 recorded key sounds are packaged and readable");
    for (const auto& key : recordings) {
        check(key.format.nChannels == 1 && key.format.nSamplesPerSec == 44100,
            "preserve original key recording format");
        check(!key.samples.empty() && key.samples.size() < 44100,
            "individual keystrokes remain short");
        check(std::any_of(key.samples.begin(), key.samples.end(), [](BYTE sample) { return sample != 0; }),
            "key recording contains audio");
    }
    check(recordings.front().samples != recordings.back().samples, "recorded keystroke variations are distinct");
    check(audio::loadKeys(L"missing-key-folder").empty(), "missing keys are silent without synthesized fallback");
    check(audio::readMusic(std::filesystem::path(executable).parent_path() /
        L"assets" / L"The Conch Call.wav", format, music), "packaged music loads through RIFF metadata");
    check(format.nChannels == 2 && format.nSamplesPerSec == 48000 && music.size() > 1000000,
        "music preserves stereo PCM payload");
    flickerEnabled = true;
    for (int i = 0; i < 1000; ++i)
        check(flickerAt(i / 30.0) >= 0 && flickerAt(i / 30.0) <= 3, "flicker stays subtle");
    seed(1);
    Bitmap steady(900, 540, PixelFormat32bppARGB), drift(900, 540, PixelFormat32bppARGB);
    flickerEnabled = false; paint(steady, .2);
    flickerEnabled = true;
    check(needsRepaint(.2), "flicker change repaints a cached scene");
    paint(drift, .2);
    const auto flickerDifference = compareRenders(pixels(steady), pixels(drift));
    check(flickerDifference.pixels > 0 && flickerDifference.largest <= 3 &&
        !flickerDifference.alphaChanged, "flicker changes brightness gently without transparency");
    flickerEnabled = false;
    check(needsRepaint(.2), "disabling flicker restores steady display");
    paint(drift, .2);
    check(pixels(steady) == pixels(drift), "disabled flicker restores exact cached pixels");
    // Ensure the independent-render allowance cannot hide a missing glyph,
    // a shifted image, a broad colour change, or altered transparency.
    std::vector<std::uint8_t> reference(10000 * 4, 100), changed = reference;
    changed[0] += 4;
    check(equivalentRenders(compareRenders(changed, reference), 10000), "allow sparse raster rounding");
    changed[0] += 1;
    check(!equivalentRenders(compareRenders(changed, reference), 10000), "reject larger pixel changes");
    changed = reference; changed[0] += 1; changed[4] += 1;
    check(!equivalentRenders(compareRenders(changed, reference), 10000), "reject widespread differences");
    changed = reference; changed[3] += 1;
    check(!equivalentRenders(compareRenders(changed, reference), 10000), "reject alpha changes");

    seed(30); scrollOffset = 12;
    auto anchored = sceneAt(10).rows;
    queue({"NEW"}); advance(4);
    check(sceneAt(10).rows == anchored, "newline preserves scrollback");
    transcript.back().text = std::string(Columns, 'X');
    queue({"Y"}); advance(1);
    check(sceneAt(10).rows == anchored, "wrapped row preserves scrollback");

    seed(500); transcript.front().text = std::string(Columns * 3, 'X');
    scrollOffset = 12; anchored = sceneAt(10).rows;
    queue({"NEW"}); advance(4);
    check(transcript.size() == 500 && sceneAt(10).rows == anchored,
        "evicting a wrapped old line preserves the visible content");
    scrollOffset = maxScroll;
    queue({"NEXT"}); advance(5);
    check(scrollOffset == maxScroll && sceneAt(10).rows.front() == "ROW 2",
        "evicted viewport anchor clamps to earliest remaining row");
    scrollOffset = 0;
    queue({"LATEST"}); advance(7);
    check(scrollOffset == 0 && sceneAt(10).rows.back().empty(), "bottom follows new output");

    seed(0); queue({std::string(1000, 'X')}); lastAdvance = 1000;
    advanceTo(1330);
    check(transcript.back().text.size() == 30, "delayed timer catches up using elapsed time");
    seed(0); queue({std::string(1000, 'X')}); lastAdvance = 1000;
    for (ULONGLONG now = 1033; now <= 1330; now += 33) advanceTo(now);
    check(transcript.back().text.size() == 30, "frequent and delayed ticks have equal speed");
    advanceTo(1339); check(transcript.back().text.size() == 30, "fractional tick is retained");
    advanceTo(1341); check(transcript.back().text.size() == 31, "fractional tick accumulates");

    seed(30);
    Bitmap output(960, 540, PixelFormat32bppARGB);
    paint(output, 10.2); checkFresh(output, 10.2);
    check(!needsRepaint(10.3), "idle tick does not request paint");
    check(needsRepaint(10.7), "cursor transition requests paint");
    const auto on = pixels(output);
    auto* onFrame = completedFrames[1].get();
    paint(output, 10.7); checkFresh(output, 10.7);
    check(pixels(output) != on, "cursor is visible only in its on phase");
    auto* offFrame = completedFrames[0].get();
    paint(output, 11.2);
    check(completedFrames[1].get() == onFrame && completedFrames[0].get() == offFrame,
        "cursor phases reuse their completed frames");
    check(pixels(output) == on, "cached cursor phase preserves pixels");
    for (const auto background : {Color(0, 0, 0, 0), Color(255, 255, 0, 255), Color(128, 255, 255, 255)}) {
        { Graphics g(&output); g.Clear(background); }
        paint(output, 11.2);
        check(pixels(output) == on, "cached copy is independent of destination contents");
    }
    int repaints = 0;
    for (int ms = 12000; ms < 14000; ms += 33) {
        if (needsRepaint(ms / 1000.0)) { ++repaints; paint(output, ms / 1000.0); }
    }
    check(repaints == 3, "idle timer repaints only on cursor transitions");
    input = "STATUS";
    check(needsRepaint(14.2), "typing requests repaint");
    paint(output, 14.2); checkFresh(output, 14.2);
    check(!completedFrames[0], "changed input invalidates the other cursor phase");
    scrollOffset = 12;
    paint(output, 14.2);
    check(!needsRepaint(14.7), "scrollback does not redraw for hidden cursor");
    queue({"NEW"}); advance(4);
    check(!needsRepaint(14.7), "offscreen output does not redraw anchored scrollback");

    seed(30);
    paint(output, 6.9);
    check(needsRepaint(7.0), "cursor transition requests repaint across seven seconds");
    check(sceneAt(6.9) == sceneAt(7.0), "version footer does not expire after startup");
    paint(output, 7.0); checkFresh(output, 7.0);
    Bitmap resized(1280, 720, PixelFormat32bppARGB);
    paint(resized, 7.0); checkFresh(resized, 7.0);
    check(completedFrames[1]->GetWidth() == 1280, "resize rebuilds cached dimensions");
    session = {}; boot();
    input = "SIGNAL"; submit(nullptr);
    check(session.stage == muthur::Stage::Intercept && transmission.empty() && input == "SIGNAL",
        "Enter reveals text without submitting a buffered command");
    submit(nullptr); advance(10000);
    check(session.stage == muthur::Stage::Verification, "app enters investigation through real submission path");
    input = "SCAN"; submit(nullptr); advance(10000);
    const auto scanStart = wallNow;
    check(session.scanning && sceneAt(0).task == "SCANNING", "scan panel appears");
    updateTasks(scanStart + 4);
    check(sceneAt(0).progress.find("50%") != std::string::npos &&
        sceneAt(0).timing.find("00:00:04") != std::string::npos, "panel tracks actual elapsed seconds");
    paint(output, 0.2); checkFresh(output, 0.2);
    updateTasks(scanStart + 8); advance(10000);
    check(!session.scanning && session.has(muthur::Bearing) && sceneAt(0).task.empty(),
        "timer delivers scan results and removes panel");
    for (const char* command : {"ARCHIVE MERIDIAN", "1", "1", "WAIT", "2", "WAIT"}) {
        input = command; submit(nullptr); advance(10000);
    }
    check(session.stage == muthur::Stage::Exterior, "app reaches surface dialogue");
    const auto progress = muthur::encodeSave(session);
    input = "CLEAR"; submit(nullptr); advance(10000);
    check(muthur::encodeSave(session) == progress, "app clear preserves story state");
    const auto choiceRows = sceneAt(10.2).rows;
    check(std::find(choiceRows.begin(), choiceRows.end(), "ORTIZ / SURFACE") != choiceRows.end() &&
        std::any_of(choiceRows.begin(), choiceRows.end(), [](const auto& row) { return row.rfind("1  ", 0) == 0; }) &&
        std::any_of(choiceRows.begin(), choiceRows.end(), [](const auto& row) { return row.rfind("2  ", 0) == 0; }),
        "clear restores speaker and both current choices in the same viewport");
    check(needsRepaint(10.2), "clear invalidates displayed content");
    paint(output, 10.2); checkFresh(output, 10.2);
    input = "REBOOT"; submit(nullptr);
    check(muthur::encodeSave(session) == progress, "app reboot preserves story state");
    check(needsRepaint(10.2), "reboot invalidates displayed content");
    paint(output, 10.2);
    advance(10000);
    check(needsRepaint(10.2), "transmission completion restores prompt and cursor");
    paint(output, 10.2); checkFresh(output, 10.2);

    // Exercise real file persistence only in a unique test directory, never
    // the player's LocalAppData. A bad load cannot overwrite the old save.
    const auto testDirectory = std::filesystem::temp_directory_path() /
        (L"muthur-save-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    savePath = testDirectory / L"meridian-v1.save";
    if (!saveProgress()) {
        std::cerr << "Save test path: " << savePath << "; Windows error: " << GetLastError() << '\n';
        check(false, "atomic save creates its destination directory");
    }
    muthur::Session loaded;
    check(muthur::loadSave(savePath, loaded) == muthur::LoadResult::Loaded &&
        muthur::encodeSave(loaded) == progress, "disk save resumes current choice");
    input = "2"; submit(nullptr); advance(10000);
    check(muthur::loadSave(savePath, loaded) == muthur::LoadResult::Loaded &&
        loaded.stage == muthur::Stage::Perimeter, "story transition automatically saves");
    {
        std::ofstream corrupt(savePath); corrupt << "unsupported save";
    }
    check(muthur::loadSave(savePath, loaded) == muthur::LoadResult::Invalid,
        "invalid save is reported without accepting it");
    preserveUnreadableSave = true;
    input = "WAIT"; submit(nullptr); advance(10000);
    check(muthur::loadSave(savePath, loaded) == muthur::LoadResult::Invalid,
        "unreadable save is preserved during play");
    input = "NEW GAME"; submit(nullptr); advance(10000);
    input = "CONFIRM RESTART"; submit(nullptr); advance(10000);
    check(muthur::loadSave(savePath, loaded) == muthur::LoadResult::Loaded &&
        loaded.stage == muthur::Stage::Intercept, "explicit new game replaces preserved invalid save");
    std::filesystem::remove(savePath);
    std::filesystem::remove(testDirectory);
    savePath.clear();

    for (auto size : {std::pair<int, int>{1920, 1080}, {3840, 2160}}) {
        Bitmap target(size.first, size.second, PixelFormat32bppARGB);
        paint(target, 10.2); paint(target, 10.7);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) paint(target, i % 2 ? 10.2 : 10.7);
        std::cout << size.first << 'x' << size.second << " cached ms/frame: "
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 10 << '\n';
    }
}
}
int main() {
    GdiplusStartupInput startup; ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &startup, nullptr) != Ok) return 1;
    int result = 0;
    try { run(); std::cout << "All off-screen rendering and scrollback tests passed.\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    releaseSurfaces();
    GdiplusShutdown(token);
    return result;
}
