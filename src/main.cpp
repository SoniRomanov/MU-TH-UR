// Copyright (C) 2026 Soni Romanov
// SPDX-License-Identifier: MIT
// Distributed without warranty. See LICENSE for the complete terms.
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <cmath>
#include <deque>
#include <cstdint>
#include <memory>
#include <muthur_version.h>
#include "console.h"
#include "save.h"
#include "crt_filter.h"
#include "display_finish.h"

using namespace Gdiplus;
namespace {
constexpr int DesignW = 1280, DesignH = 720;
constexpr int SceneW = 1600, SceneH = 900;
constexpr size_t Columns = 68;
constexpr int VisibleRows = 13;
struct Line { std::string text; bool operatorLine = false; };
std::deque<Line> transcript;
std::deque<char> transmission;
std::vector<std::string> history;
std::string input, draft;
muthur::Session session;
std::filesystem::path savePath;
bool preserveUnreadableSave = false;
bool saveFailed = false;
std::int64_t wallNow = std::time(nullptr);
size_t historyIndex = 0;
int scrollOffset = 0, maxScroll = 0;
bool fullscreen = false;
ULONGLONG startTime = 0;
ULONGLONG lastAdvance = 0;
WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
std::unique_ptr<Bitmap> flatSurface, curvedSurface, frameSurface;
struct Scene {
    std::vector<std::string> rows;
    std::string prompt;
    std::string clock, task, timing, progress;
    bool scrollback = false;
    bool operator==(const Scene& other) const {
        return rows == other.rows && prompt == other.prompt && clock == other.clock &&
            task == other.task && timing == other.timing && progress == other.progress &&
            scrollback == other.scrollback;
    }
};
Scene cachedScene;
std::unique_ptr<Bitmap> completedFrames[2];
int cachedWidth = 0, cachedHeight = 0;
bool hasPresented = false, lastCursor = false;
Color phosphor() { return Color(255, 173, 235, 74); }
Color dim() { return Color(255, 80, 114, 37); }
std::wstring wide(const std::string& text) { return {text.begin(), text.end()}; }
void queue(const std::vector<std::string>& lines) {
    if (transmission.empty()) lastAdvance = GetTickCount64();
    for (const auto& line : lines) { for (char c : line) transmission.push_back(c); transmission.push_back('\n'); }
}
void boot() {
    transcript.clear(); transmission.clear(); input.clear(); scrollOffset = maxScroll = 0;
    history.clear(); historyIndex = 0; draft.clear();
    transcript.push_back({""});
    if (session.stage == muthur::Stage::Intercept) queue(muthur::opening());
    else {
        queue({"MU/TH/UR / KESTREL / WATCH RESUMED", "MISSION +" + muthur::elapsed(session), ""});
        // Reconstruct the last contact, including its consequences, even if
        // the app closed while that contact was still typing onscreen.
        auto start = session.journal.end();
        while (start != session.journal.begin()) {
            --start;
            if (start->rfind("[MISSION +", 0) == 0) break;
        }
        queue(std::vector<std::string>(start, session.journal.end()));
        queue(muthur::prompt(session));
        queue({"LOG recalls earlier contacts. COMMS repeats your choices."});
    }
    if (preserveUnreadableSave) queue({"", "SAVE COULD NOT BE READ. ORIGINAL FILE PRESERVED.",
        "Progress will not save until NEW GAME is confirmed."});
    else if (saveFailed) queue({"", "SAVE UNAVAILABLE. Progress is currently in memory only."});
}
bool saveProgress() {
    if (savePath.empty() || preserveUnreadableSave) return false;
    std::error_code ec;
    std::filesystem::create_directories(savePath.parent_path(), ec);
    if (ec) return false;
    auto pending = savePath; pending += L".tmp";
    if (!muthur::writeSaveFile(pending, session)) return false;
    return MoveFileExW(pending.c_str(), savePath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
void loadProgress() {
    // Only the interactive application enables persistence. Tests and
    // snapshots never read or modify the player's save.
    wchar_t local[32768]{};
    const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    if (count == 0 || count >= 32768) { saveFailed = true; return; }
    savePath = std::filesystem::path(local) / L"MU-TH-UR" / L"meridian-v1.save";
    const auto result = muthur::loadSave(savePath, session);
    preserveUnreadableSave = result == muthur::LoadResult::Invalid;
    if (result == muthur::LoadResult::Loaded) {
        muthur::tick(session, wallNow);
        // Persist migrated deadlines immediately, even before another command.
        saveFailed = !saveProgress();
    }
}
void updateTasks(std::int64_t now) {
    wallNow = now;
    auto completed = muthur::tick(session, now);
    if (completed.lines.empty()) return;
    queue({"", "MU/TH/UR / TASK COMPLETE"});
    queue(completed.lines);
    if (!savePath.empty() && !preserveUnreadableSave) {
        saveFailed = !saveProgress();
        if (saveFailed) queue({"SAVE FAILED. Progress is in memory only; check disk access."});
    }
}
std::string localClock(std::int64_t value, bool date = false) {
    const auto timestamp = static_cast<std::time_t>(value);
    std::tm local{};
    if (localtime_s(&local, &timestamp) != 0) return "--:--:--";
    char buffer[40]{};
    std::strftime(buffer, sizeof(buffer), date ? "%d %b %H:%M:%S" : "%H:%M:%S", &local);
    return buffer;
}
void advance(size_t count) {
    int addedRows = 0;
    while (count-- && !transmission.empty()) {
        char c = transmission.front(); transmission.pop_front();
        if (c == '\n') { transcript.push_back({""}); ++addedRows; }
        else {
            if (transcript.empty()) { transcript.push_back({""}); ++addedRows; }
            const auto length = transcript.back().text.size();
            if (length > 0 && length % Columns == 0) ++addedRows;
            transcript.back().text += c;
        }
    }
    // Distance from the bottom grows with new rows, even when old rows are
    // evicted. Clamp only if the row being viewed has itself been evicted.
    if (scrollOffset > 0) scrollOffset += addedRows;
    while (transcript.size() > 500) transcript.pop_front();
    int rows = 0;
    for (const auto& line : transcript)
        rows += static_cast<int>(std::max(size_t{1}, (line.text.size() + Columns - 1) / Columns));
    maxScroll = std::max(0, rows - VisibleRows);
    scrollOffset = std::clamp(scrollOffset, 0, maxScroll);
}
void advanceTo(ULONGLONG now) {
    constexpr ULONGLONG millisecondsPerCharacter = 11;
    if (transmission.empty()) { lastAdvance = now; return; }
    const auto count = (now - lastAdvance) / millisecondsPerCharacter;
    lastAdvance += count * millisecondsPerCharacter;
    if (count) advance(static_cast<size_t>(count));
}
void fill(Graphics& g, float x, float y, float w, float h, Color color) {
    SolidBrush b(color); g.FillRectangle(&b, x, y, w, h);
}
void text(Graphics& g, const std::string& s, float x, float y, float size, Color color, bool /*glow*/ = false) {
    FontFamily family(L"Lucida Console"); Font font(&family, size, FontStyleRegular, UnitPixel);
    StringFormat format(StringFormat::GenericTypographic());
    const auto value = wide(s);
    SolidBrush brush(color);
    g.DrawString(value.c_str(), -1, &font, PointF(x, y), &format, &brush);
}
void toggleFullscreen(HWND window) {
    if (!fullscreen) {
        GetWindowPlacement(window, &placement);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtr(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtr(window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(window, &placement);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    fullscreen = !fullscreen;
}
void submit(HWND window) {
    // First Enter only reveals the current transmission; it cannot also
    // submit a choice typed while that transmission was arriving.
    if (!transmission.empty()) { advance(transmission.size()); return; }
    const auto command = muthur::normalize(input);
    if (command.empty()) return;
    transcript.push_back({"> " + command, true}); transcript.push_back({""});
    history.push_back(command); if (history.size() > 100) history.erase(history.begin());
    historyIndex = history.size(); input.clear(); draft.clear(); scrollOffset = 0;
    const auto before = muthur::encodeSave(session);
    const bool confirmedRestart = session.restartPending && command == "CONFIRM RESTART";
    auto response = muthur::respond(command, session, wallNow);
    if (confirmedRestart) preserveUnreadableSave = false;
    const bool shouldSave = before != muthur::encodeSave(session) || confirmedRestart || saveFailed;
    // Empty savePath intentionally disables persistence in off-screen runs.
    if (shouldSave && !savePath.empty() && !preserveUnreadableSave) saveFailed = !saveProgress();
    switch (response.action) {
    case muthur::Action::Clear: transcript.clear(); break;
    case muthur::Action::Reboot: boot(); break;
    case muthur::Action::Exit: DestroyWindow(window); return;
    default: break;
    }
    queue(response.lines);
    if (!response.lines.empty()) transmission.push_back('\n');
    if (shouldSave && saveFailed) queue({"SAVE FAILED. Progress is in memory only; check disk access."});
}
Scene sceneAt(double /*seconds*/) {
    Scene scene;
    scene.clock = "LOCAL " + localClock(wallNow);
    if (session.taskDue) {
        const auto duration = session.taskDue - session.taskStarted;
        const auto remaining = std::clamp(session.taskDue - wallNow, std::int64_t{0}, duration);
        const int percent = static_cast<int>((duration - remaining) * 100 / duration);
        char countdown[32]{};
        snprintf(countdown, sizeof(countdown), "%02lld:%02lld:%02lld", remaining / 3600, remaining / 60 % 60, remaining % 60);
        scene.task = muthur::taskName(session);
        scene.timing = "ETA " + localClock(session.taskDue, true) + " / LEFT " + countdown + " / WAIT TO SKIP";
        const int filled = percent * 24 / 100;
        scene.progress = "[" + std::string(filled, '=') + (filled < 24 ? ">" + std::string(23 - filled, '.') : "") +
            "] " + std::to_string(percent) + "%";
    }
    std::vector<std::string> rows;
    for (const auto& line : transcript) {
        if (line.text.empty()) rows.push_back("");
        else for (size_t pos = 0; pos < line.text.size(); pos += Columns)
            rows.push_back(line.text.substr(pos, Columns));
    }
    maxScroll = std::max(0, static_cast<int>(rows.size()) - VisibleRows);
    scrollOffset = std::clamp(scrollOffset, 0, maxScroll);
    const int end = static_cast<int>(rows.size()) - scrollOffset;
    const int begin = std::max(0, end - VisibleRows);
    scene.rows.assign(rows.begin() + begin, rows.begin() + end);
    scene.scrollback = scrollOffset > 0;
    if (!scene.scrollback && (transmission.empty() || !input.empty()))
        scene.prompt = "> " + (input.size() > Columns - 2 ? input.substr(input.size() - (Columns - 2)) : input);
    return scene;
}
bool cursorAt(double seconds) {
    return scrollOffset == 0 && transmission.empty() && std::fmod(seconds, 1.0) < .65;
}
bool needsRepaint(double seconds) {
    const auto scene = sceneAt(seconds);
    return !hasPresented || !(scene == cachedScene) || cursorAt(seconds) != lastCursor;
}
void renderFlat(Graphics& g, int width, int height, const Scene& scene, bool cursor) {
    g.Clear(Color(255, 0, 0, 0));
    const float scale = std::min(width / float(DesignW), height / float(DesignH));
    g.TranslateTransform((width - DesignW * scale) / 2, (height - DesignH * scale) / 2);
    g.ScaleTransform(scale, scale);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    constexpr float left = 130, top = 82, lineHeight = 32, fontSize = 25;
    const auto saved = g.Save();
    for (size_t i = 0; i < scene.rows.size(); ++i)
        text(g, scene.rows[i], left, top + static_cast<float>(i) * lineHeight,
            fontSize, phosphor(), true);

    const float promptY = top + static_cast<float>(scene.rows.size()) * lineHeight;
    if (scene.scrollback) {
        text(g, "SCROLLBACK / PAGE DOWN TO RETURN", left, 520, 16, dim());
    } else {
        if (!scene.prompt.empty()) {
            text(g, scene.prompt, left, promptY, fontSize, phosphor(), true);
            if (cursor)
                fill(g, left + static_cast<float>(scene.prompt.size()) * 15, promptY + 3, 13, 23, phosphor());
        }
    }
    g.Restore(saved);
    text(g, scene.clock, 900, 52, 16, phosphor());
    if (!scene.task.empty()) {
        fill(g, left, 558, 1020, 1, dim());
        text(g, scene.task, left, 576, 19, phosphor());
        text(g, scene.progress, 670, 576, 19, phosphor());
        text(g, scene.timing, left, 612, 17, phosphor());
    }
    text(g, "INTERFACE VER. " MUTHUR_VERSION "  /  THE MERIDIAN SIGNAL", left, 658, 13, dim());
    g.ResetTransform();
}

bool renderEffects(Graphics& g, int width, int height, const Scene& scene, bool cursor) {
    constexpr int sourceW = 960, sourceH = 540;
    // Keep the tube 16:9, with room for the thick molded housing and lower sill.
    const float scale = std::min(width / 16.0f, height / 9.0f) * .78f;
    const int apertureW = std::max(1, static_cast<int>(scale * 16));
    const int apertureH = std::max(1, static_cast<int>(scale * 9));
    // Overscan the curved tube behind the recessed opening. The filter still
    // runs at final pixel resolution; only its outermost edges are hidden.
    const int outW = std::max(1, static_cast<int>(apertureW / .94f));
    const int outH = std::max(1, static_cast<int>(apertureH / .94f));
    static crt::Filter filter;
    static display::Glass glass;
    static display::Frame frame;
    if (!flatSurface) flatSurface = std::make_unique<Bitmap>(sourceW, sourceH, PixelFormat32bppARGB);
    if (filter.width != outW || filter.height != outH) {
        filter.configure(outW, outH, sourceW, sourceH);
        curvedSurface = std::make_unique<Bitmap>(outW, outH, PixelFormat32bppARGB);
    }
    if (!frameSurface || frameSurface->GetWidth() != static_cast<UINT>(width) ||
        frameSurface->GetHeight() != static_cast<UINT>(height)) {
        frameSurface = std::make_unique<Bitmap>(width, height, PixelFormat32bppARGB);
        frame.configure(width, height, apertureW, apertureH);
    }
    { Graphics raster(flatSurface.get()); renderFlat(raster, sourceW, sourceH, scene, cursor); }
    Rect srcRect(0, 0, sourceW, sourceH), dstRect(0, 0, outW, outH);
    BitmapData source{}, target{};
    if (flatSurface->LockBits(&srcRect, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &source) != Ok) return false;
    if (curvedSurface->LockBits(&dstRect, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &target) != Ok) {
        flatSurface->UnlockBits(&source); return false;
    }
    glass.apply(static_cast<std::uint8_t*>(source.Scan0), source.Stride, sourceW, sourceH);
    filter.apply(static_cast<const std::uint8_t*>(source.Scan0), source.Stride,
        static_cast<std::uint8_t*>(target.Scan0), target.Stride);
    Rect area(0, 0, width, height); BitmapData frameData{};
    const bool frameReady = frameSurface->LockBits(&area, ImageLockModeWrite, PixelFormat32bppARGB, &frameData) == Ok;
    if (frameReady) {
        frame.apply(static_cast<const std::uint8_t*>(target.Scan0), target.Stride, outW, outH,
            static_cast<std::uint8_t*>(frameData.Scan0), frameData.Stride);
        frameSurface->UnlockBits(&frameData);
    }
    curvedSurface->UnlockBits(&target); flatSurface->UnlockBits(&source);
    if (!frameReady) return false;
    g.Clear(Color(255, 0, 0, 0));
    // Pattern is generated at final output resolution: never rescale the mask.
    g.DrawImage(curvedSurface.get(), (width - outW) / 2, (height - outH) / 2, outW, outH);
    g.DrawImage(frameSurface.get(), 0, 0, width, height);
    return true;
}
void render(Graphics& g, int width, int height, double seconds) {
    const auto scene = sceneAt(seconds);
    const bool cursor = cursorAt(seconds);
    if (!hasPresented || !(scene == cachedScene) || width != cachedWidth || height != cachedHeight) {
        completedFrames[0].reset(); completedFrames[1].reset();
        cachedScene = scene; cachedWidth = width; cachedHeight = height;
        hasPresented = false;
    }
    auto& completed = completedFrames[cursor ? 1 : 0];
    if (!completed) {
        auto bitmap = std::make_unique<Bitmap>(width, height, PixelFormat32bppARGB);
        Graphics raster(bitmap.get());
        if (!renderEffects(raster, width, height, scene, cursor)) return;
        completed = std::move(bitmap);
    }
    // Both cursor phases retain the complete CRT and reflection effects.
    // Copy cached pixels exactly. Default GDI+ image interpolation/compositing
    // can blend edge pixels with the previous contents of the paint buffer.
    const auto saved = g.Save();
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.DrawImage(completed.get(), Rect(0, 0, width, height), 0, 0, width, height, UnitPixel);
    g.Restore(saved);
    hasPresented = true; lastCursor = cursor;
}
void releaseSurfaces() {
    completedFrames[0].reset(); completedFrames[1].reset();
    flatSurface.reset(); curvedSurface.reset(); frameSurface.reset();
    hasPresented = false;
}
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: SetTimer(window, 1, 33, nullptr); return 0;
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {900, 540}; return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR:
        if (fullscreen && LOWORD(lParam) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
        break;
    case WM_TIMER: {
        const auto now = GetTickCount64();
        updateTasks(std::time(nullptr));
        advanceTo(now);
        if (!IsIconic(window) && needsRepaint((now - startTime) / 1000.0))
            InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint);
        RECT rect; GetClientRect(window, &rect);
        if (rect.right > 0 && rect.bottom > 0) {
            HDC buffer = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
            HGDIOBJ old = SelectObject(buffer, bitmap);
            { Graphics g(buffer); render(g, rect.right, rect.bottom, (GetTickCount64() - startTime) / 1000.0); }
            BitBlt(dc, 0, 0, rect.right, rect.bottom, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, old); DeleteObject(bitmap); DeleteDC(buffer);
        }
        EndPaint(window, &paint); return 0;
    }
    case WM_PASTE:
        if (OpenClipboard(window)) {
            HANDLE data = GetClipboardData(CF_UNICODETEXT);
            if (data) {
                auto value = static_cast<const wchar_t*>(GlobalLock(data));
                if (value) {
                    for (size_t i = 0; value[i] && input.size() < 240; ++i) {
                        if (value[i] == L'\r' || value[i] == L'\n') break;
                        if (value[i] >= 32 && value[i] < 127)
                            input += static_cast<char>(std::toupper(static_cast<unsigned char>(value[i])));
                    }
                    GlobalUnlock(data);
                }
            }
            CloseClipboard();
        }
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_CHAR:
        if (wParam == '\r') submit(window);
        else if (wParam == '\b' && !input.empty()) input.pop_back();
        else if (wParam >= 32 && wParam < 127 && input.size() < 240)
            input += static_cast<char>(std::toupper(static_cast<unsigned char>(wParam)));
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_KEYDOWN:
        if (wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) SendMessage(window, WM_PASTE, 0, 0);
        if (wParam == VK_F11 || (wParam == VK_ESCAPE && fullscreen)) toggleFullscreen(window);
        if (wParam == VK_PRIOR) scrollOffset = std::min(maxScroll, scrollOffset + 12);
        if (wParam == VK_NEXT) scrollOffset = std::max(0, scrollOffset - 12);
        if (wParam == VK_UP && historyIndex > 0) {
            if (historyIndex == history.size()) draft = input;
            input = history[--historyIndex];
        }
        if (wParam == VK_DOWN && historyIndex < history.size()) {
            ++historyIndex; input = historyIndex == history.size() ? draft : history[historyIndex];
        }
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL:
        scrollOffset = std::clamp(scrollOffset + GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 3, 0, maxScroll);
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: SetFocus(window); return 0;
    case WM_DESTROY: KillTimer(window, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}
int snapshot(bool story = false, bool task = false) {
    advance(10000);
    if (story) {
        for (const char* command : {"SIGNAL", "ARCHIVE MERIDIAN", "SCAN", "WAIT", "1", "1", "WAIT",
            "2", "WAIT", "SCAN STATION", "WAIT", "CLEAR"}) {
            input = command; submit(nullptr); advance(10000);
        }
    }
    if (task) {
        session = {}; wallNow = 1792321200;
        boot(); advance(10000);
        for (const char* command : {"SIGNAL", "SCAN"}) {
            input = command; submit(nullptr); advance(10000);
        }
        wallNow += 4;
    }
    Bitmap bitmap(SceneW, SceneH, PixelFormat32bppARGB);
    { Graphics g(&bitmap); render(g, SceneW, SceneH, 0.2); }
    // Standard GDI+ PNG encoder CLSID.
    const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    return bitmap.Save(task ? L"preview-task.png" : (story ? L"preview-story.png" : L"preview.png"), &png, nullptr) == Ok ? 0 : 1;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show) {
    SetProcessDPIAware();
    GdiplusStartupInput startup; ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &startup, nullptr) != Ok) return 1;
    const std::wstring args(commandLine);
    if (args == L"--snapshot" || args == L"--snapshot-story" || args == L"--snapshot-task") {
        boot(); startTime = GetTickCount64();
        int result = snapshot(args == L"--snapshot-story", args == L"--snapshot-task");
        releaseSurfaces();
        GdiplusShutdown(token); return result;
    }
    loadProgress(); boot(); startTime = GetTickCount64();
    WNDCLASS wc{}; wc.lpfnWndProc = windowProc; wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"MUTHURConsole";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    if (!RegisterClass(&wc)) { GdiplusShutdown(token); return 1; }
    RECT rect{0, 0, 1280, 720}; AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindow(wc.lpszClassName, L"MU/TH/UR - The Meridian Signal", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, nullptr);
    if (!window) { GdiplusShutdown(token); return 1; }
    toggleFullscreen(window);
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{}; int result;
    while ((result = GetMessage(&message, nullptr, 0, 0)) > 0) { TranslateMessage(&message); DispatchMessage(&message); }
    releaseSurfaces();
    GdiplusShutdown(token);
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
