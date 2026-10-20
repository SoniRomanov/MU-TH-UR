// Copyright (C) 2026 Soni Romanov
// SPDX-License-Identifier: MIT
#include "../src/save.h"
#include "../src/crt_filter.h"
#include "../src/display_finish.h"
#include <iostream>
#include <stdexcept>
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    // Reflections must follow changing screen content and leave the aperture clear.
    display::Frame frame;
    frame.configure(360, 200, 288, 162);
    constexpr int frameStride = 360 * 4 + 16;
    std::vector<std::uint8_t> screen(320 * 180 * 4, 0);
    std::vector<std::uint8_t> reflected(frameStride * 200, 123);
    auto leftLight = [&]() {
        frame.apply(screen.data(), 320 * 4, 320, 180, reflected.data(), frameStride);
        int light = 0;
        for (int y = 30; y < 170; ++y) for (int x = 30; x < 36; ++x)
            light += reflected[y * frameStride + x * 4 + 1];
        return light;
    };
    const int dark = leftLight();
    for (int y = 0; y < 180; ++y) for (int x = 0; x < 160; ++x)
        screen[(y * 320 + x) * 4 + 1] = 255;
    check(leftLight() > dark + 1000, "left screen illuminates left bevel");
    std::fill(screen.begin(), screen.end(), std::uint8_t{0});
    check(leftLight() == dark, "reflection clears when screen goes dark");
    check(reflected[100 * frameStride + 180 * 4 + 3] == 0, "frame leaves screen visible");
    for (int y = 0; y < 200; ++y) for (int x = 360 * 4; x < frameStride; ++x)
        check(reflected[y * frameStride + x] == 123, "frame respects row stride");
    // Exercise both upscaling and downscaling, including curved corners.
    for (auto size : {std::pair<int, int>{64, 48}, {120, 90}, {45, 34}, {160, 90}}) {
        crt::Filter filter;
        filter.configure(size.first, size.second, 64, 48);
        std::vector<std::uint8_t> pixels(64 * 48 * 4, 0);
        std::vector<std::uint8_t> output(size.first * size.second * 4, 123);
        filter.apply(pixels.data(), 64 * 4, output.data(), size.first * 4);
        for (size_t i = 0; i < output.size(); i += 4)
            check(output[i] == 0 && output[i + 1] == 0 && output[i + 2] == 0 && output[i + 3] == 255, "CRT preserves pure black");
        std::fill(pixels.begin(), pixels.end(), std::uint8_t{255});
        filter.apply(pixels.data(), 64 * 4, output.data(), size.first * 4);
        check(output[0] == 0, "curved corner masked");
        check(output[((size.second / 2) * size.first + size.first / 2) * 4 + 1] > 150, "CRT center remains bright");
    }
    // The original monochrome filter must not introduce colour into a grey
    // image, touch row padding, or assume that input images are at least 2x2.
    for (auto sourceSize : {std::pair<int, int>{1, 1}, {1, 7}, {9, 1}, {17, 13}}) {
        const int sw = sourceSize.first, sh = sourceSize.second;
        const int sourceStride = sw * 4 + 12, destinationStride = 37 * 4 + 20;
        std::vector<std::uint8_t> source(sourceStride * sh, 201);
        for (int y = 0; y < sh; ++y) for (int x = 0; x < sw; ++x)
            for (int c = 0; c < 3; ++c) source[y * sourceStride + x * 4 + c] = 128;
        const auto original = source;
        std::vector<std::uint8_t> result(destinationStride * 29, 79);
        crt::Filter monochrome;
        monochrome.configure(37, 29, sw, sh);
        monochrome.apply(source.data(), sourceStride, result.data(), destinationStride);
        check(source == original, "CRT leaves source unchanged");
        for (int y = 0; y < 29; ++y) {
            for (int x = 0; x < 37; ++x) {
                const auto p = result.data() + y * destinationStride + x * 4;
                check(p[0] == p[1] && p[1] == p[2], "monochrome CRT preserves neutral colours");
                check(p[3] == 255, "CRT output is opaque");
            }
            for (int x = 37 * 4; x < destinationStride; ++x)
                check(result[y * destinationStride + x] == 79, "CRT respects destination padding");
        }
        check(result[14 * destinationStride + 18 * 4] > 80, "degenerate input remains visible");
        std::vector<std::uint8_t> inverted(destinationStride * 29, 79);
        monochrome.apply(source.data() + (sh - 1) * sourceStride, -sourceStride,
            inverted.data() + 28 * destinationStride, -destinationStride);
        for (int y = 0; y < 29; ++y)
            check(std::equal(result.data() + y * destinationStride, result.data() + (y + 1) * destinationStride,
                inverted.data() + (28 - y) * destinationStride), "CRT supports negative row strides");
        bool rejected = false;
        try { monochrome.configure(0, 29, sw, sh); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && monochrome.width == 37, "invalid dimensions preserve valid filter");
        monochrome.configure(1, 1, sw, sh);
        std::uint8_t single[4]{};
        monochrome.apply(source.data(), sourceStride, single, 4);
        check(single[0] > 80 && single[3] == 255, "single output pixel");
    }
    using namespace muthur;
    auto sameSave = [](const Session& state) {
        Session restored;
        check(decodeSave(encodeSave(state), restored), "saved checkpoint loads");
        check(encodeSave(state) == encodeSave(restored), "checkpoint round trip preserves evidence, time and journal");
        check(prompt(state) == prompt(restored), "checkpoint restores available decisions");
    };
    auto contains = [](const Response& response, const std::string& needle) {
        return std::any_of(response.lines.begin(), response.lines.end(), [&](const auto& line) {
            return line.find(needle) != std::string::npos;
        });
    };
    Session initial;
    sameSave(initial);
    for (const auto* command : {"", " ", "1", "2", "WAIT", "SCAN", "ARCHIVE STATION",
        "CONFIRM RESTART", "WAKE CREW", "yes please", "do not divert", "ASK REBOOT", "format C:"}) {
        const auto before = encodeSave(initial);
        respond(command, initial);
        check(encodeSave(initial) == before, "out-of-order and ambiguous commands cannot advance the story");
    }
    check(respond(" clear \r\n").action == Action::Clear, "normalize lifecycle command");
    check(respond("quit").action == Action::Exit, "exit alias");
    check(contains(respond("WHO AM I?"), "human"), "player is human");
    check(contains(respond("WHO ARE YOU?"), "SYSTEMS INTERFACE"), "computer is distinct from player");
    check(contains(respond(std::string(10000, 'x')), "No action taken"), "unknown input is safe");
    respond("SIGNAL", initial);
    respond("1", initial);
    check(initial.stage == Stage::Verification, "cannot wake command without evidence");
    respond("ARCHIVE MERIDIAN", initial);
    respond("1", initial);
    check(initial.stage == Stage::Verification, "archive alone does not authenticate a live contact");
    respond("SCAN", initial); respond("WAIT", initial);
    const auto authenticated = encodeSave(initial);
    respond("SCAN", initial); respond("ARCHIVE MERIDIAN", initial); respond("SIGNAL", initial);
    check(encodeSave(initial) == authenticated, "repeated evidence does not consume time or duplicate journal");

    // Play every combination of briefing, landing, reconnaissance, entry,
    // power and data-link decisions through the real interpreter. Save/load
    // after every transition, including contacts that have not arrived yet.
    int routes = 0;
    for (int brief = 1; brief <= 2; ++brief)
    for (int approach = 0; approach < 3; ++approach)
    for (int scan = 0; scan < 2; ++scan)
    for (int entrance = 1; entrance <= 2; ++entrance)
    for (int plans = 0; plans < 2; ++plans)
    for (int pump = 1; pump <= 2; ++pump)
    for (int link = 1; link <= 2; ++link) {
        Session state;
        auto command = [&](const std::string& text) {
            auto result = respond(text, state);
            sameSave(state);
            if (state.taskDue) {
                Session automatic = state, skipped = state;
                tick(automatic, state.taskDue - 1);
                check(encodeSave(automatic) == encodeSave(state), "task stays pending before deadline");
                tick(automatic, state.taskDue + 86400);
                respond("WAIT", skipped);
                check(encodeSave(automatic) == encodeSave(skipped), "offline completion matches WAIT on every route");
            }
            if (state.scanning) result = respond("WAIT", state);
            sameSave(state);
            const auto saved = encodeSave(state);
            for (const auto* inquiry : {"HELP", "COMMS", "STATUS", "CREW", "MISSION", "LOG",
                "REPEAT THAT", "LICENSE", "CLEAR", "REBOOT", "ASK EXIT", "3"}) {
                respond(inquiry, state);
                check(encodeSave(state) == saved, "inquiries, invalid choices and interface refresh preserve progress");
            }
            Session restored;
            check(decodeSave(saved, restored), "resume after each command");
            state = std::move(restored);
            return result;
        };
        command("SIGNAL"); command("SCAN"); command("ARCHIVE MERIDIAN"); command("1");
        check(state.stage == Stage::Briefing && state.minutes == 29, "live contact and wake consume expected time");
        command(std::to_string(brief)); command("WAIT");
        check(state.stage == Stage::Orbit && state.minutes == 389, "diversion reaches orbit");
        if (approach != 0) { command("1"); command("WAIT"); }
        command(approach == 1 ? "1" : "2"); command("WAIT");
        check(state.stage == Stage::Exterior, "all landing routes reach exterior");
        if (scan) command("SCAN STATION");
        command(std::to_string(entrance));
        // A scan after committing to an unsafe door must not undo that choice.
        if (!scan && entrance == 1 && approach == 0) command("SCAN STATION");
        command("WAIT");
        check(state.stage == Stage::Junction, "both entrances reach junction");
        check(state.has(Injury) == (entrance == 1 && !scan && approach == 0), "door injury depends on prior evidence");
        if (plans) command("ARCHIVE STATION");
        command(std::to_string(pump)); command("WAIT");
        check(state.stage == Stage::Comms, "both power decisions reach transmitter");
        const auto ending = command(std::to_string(link));
        check(state.stage == Stage::Complete, "every route finishes the episode");
        check(state.has(LowerPower) == (pump == 1), "power choice persists");
        check(state.has(Uplink) == (link == 2), "data connection persists");
        check(state.has(DetailedBrief) == (brief == 1), "briefing choice persists");
        check(state.has(PerimeterRoute) == (entrance == 2), "entry route persists");
        check(contains(ending, "DO NOT RESTORE THE LOWER ARRAY"), "ending resolves the damaged warning");
        check(contains(ending, link == 2 ? "36 HOURS AGO" : "Fresh tool marks"), "ending evidence follows link choice");
        const int expected = 389 + (approach ? 45 : 0) + (approach == 1 ? 12 : 0) + 18 +
            (entrance == 1 ? 4 : 15) + (pump == 1 ? 4 : 12) + 3;
        check(state.minutes == expected, "branch time accounting is consistent");
        const auto completed = encodeSave(state);
        command("WAIT"); command("1"); command("2");
        check(encodeSave(state) == completed, "ending cannot be replayed accidentally");
        respond("NEW GAME", state);
        check(state.restartPending && encodeSave(state) == completed, "restart asks before replacing progress");
        respond("1", state);
        check(encodeSave(state) == completed, "number cannot accidentally confirm restart");
        respond("CANCEL", state);
        check(!state.restartPending && encodeSave(state) == completed, "restart cancellation retains episode");
        respond("NEW GAME", state); respond("CONFIRM RESTART", state);
        check(state.stage == Stage::Intercept && state.journal.empty() && state.evidence == 0 && state.minutes == 0,
            "explicit restart clears episode");
        ++routes;
    }
    Session timed;
    respond("SIGNAL", timed, 1000);
    respond("SCAN", timed, 1000);
    check(timed.scanning && !timed.has(Bearing), "scan withholds evidence until complete");
    Session resumed;
    check(decodeSave(encodeSave(timed), resumed), "active scan persists");
    tick(resumed, 999);
    check(resumed.scanning, "backward clock does not complete task");
    tick(resumed, 1007);
    check(!resumed.has(Bearing), "scan does not finish early");
    tick(resumed, 1008);
    check(resumed.has(Bearing) && !resumed.taskDue, "offline scan completes at deadline");
    const auto finished = encodeSave(resumed);
    tick(resumed, 90000);
    check(encodeSave(resumed) == finished, "completion occurs exactly once");
    respond("ARCHIVE MERIDIAN", resumed, 1010);
    respond("1", resumed, 1010); respond("1", resumed, 1010);
    check(resumed.taskDue == 22610, "transit uses six real hours");
    check(decodeSave(encodeSave(resumed), timed), "waiting deadline survives reopen");
    tick(timed, 22609);
    check(timed.stage == Stage::Transit, "transit remains pending before ETA");
    tick(timed, 90000);
    check(timed.stage == Stage::Orbit && !timed.taskDue, "offline completion stops at decision");
    respond("1", timed, 90000);
    respond("WAIT", timed, 90001);
    check(timed.has(ProbeData) && !timed.taskDue, "WAIT finishes and clears timer");
    respond("2", timed, 90002);
    respond("NEW GAME", timed, 90003); respond("CONFIRM RESTART", timed, 90004);
    check(!timed.taskDue && !timed.scanning, "restart cancels active timer");
    check(decodeSave("MUTHUR-MERIDIAN 1\n0 0 0 0\n", timed), "legacy saves still load");
    check(decodeSave("MUTHUR-MERIDIAN 1\n3 3 29 1\n\"Legacy transit\"\n", timed) &&
        timed.taskDue - timed.taskStarted == 21600, "legacy wait receives a full persistent deadline");
    sameSave(timed);
    Session destination = initial;
    for (const std::string bytes : {"", "MUTHUR-MERIDIAN 3\n0 0 0 0\n", "MUTHUR-MERIDIAN 2\n0 0 0 0\n",
        "MUTHUR-MERIDIAN 2\n0 0 0 0\n1 9 1\n", "MUTHUR-MERIDIAN 2\n3 3 29 1\n1000 1001 0\n\"Bad timer\"\n",
        "MUTHUR-MERIDIAN 1\n99 0 0 0\n",
        "MUTHUR-MERIDIAN 1\n1 999999 0 0\n", "MUTHUR-MERIDIAN 1\n1 0 -1 0\n",
        "MUTHUR-MERIDIAN 1\n1 0 0 999999\n", "MUTHUR-MERIDIAN 1\n1 0 0 1\n\"truncated",
        "MUTHUR-MERIDIAN 1\n1 0 0 1\n\"control\ncharacter\"", "MUTHUR-MERIDIAN 1\n0 0 0 0\ntrailing"}) {
        const auto before = encodeSave(destination);
        check(!decodeSave(bytes, destination), "reject malformed, unsupported and truncated save");
        check(encodeSave(destination) == before, "failed load preserves destination session");
    }
    check(!decodeSave(std::string(128 * 1024 + 1, 'x'), destination), "bound save size");
    std::cout << "All graphics, story, and save tests passed (" << routes << " complete routes).\n";
}
