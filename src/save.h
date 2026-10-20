// Copyright (C) 2026 Soni Romanov
// SPDX-License-Identifier: MIT
#pragma once
#include "console.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace muthur {
// Versioned, bounded text format. Decode into a temporary Session so a corrupt
// or future-format save can never partially replace a running episode.
inline std::string encodeSave(const Session& s) {
    std::ostringstream out;
    out << "MUTHUR-MERIDIAN 2\n" << static_cast<int>(s.stage) << ' '
        << s.evidence << ' ' << s.minutes << ' ' << s.journal.size() << '\n';
    out << s.taskStarted << ' ' << s.taskDue << ' ' << s.scanning << '\n';
    for (const auto& line : s.journal) out << std::quoted(line) << '\n';
    return out.str();
}
inline bool decodeSave(const std::string& bytes, Session& destination) {
    if (bytes.size() > 128 * 1024) return false;
    std::istringstream in(bytes);
    std::string magic;
    int version = 0, stage = 0;
    size_t count = 0;
    Session candidate;
    if (!(in >> magic >> version) || magic != "MUTHUR-MERIDIAN" || (version != 1 && version != 2)) return false;
    if (!(in >> stage >> candidate.evidence >> candidate.minutes >> count)) return false;
    if (stage < 0 || stage > static_cast<int>(Stage::Complete) ||
        candidate.evidence > 2047 || candidate.minutes < 0 || candidate.minutes > 1440 || count > 500) return false;
    candidate.stage = static_cast<Stage>(stage);
    if (version == 2) {
        int scanning = 0;
        if (!(in >> candidate.taskStarted >> candidate.taskDue >> scanning) || scanning < 0 || scanning > 1) return false;
        candidate.scanning = scanning == 1;
        const auto duration = candidate.scanning ? 8 : waitingSeconds(candidate.stage);
        if (candidate.scanning && !((candidate.stage == Stage::Verification && !candidate.has(Bearing)) ||
            (candidate.stage == Stage::Exterior && !candidate.has(DoorScan)))) return false;
        if (candidate.taskStarted < 0 || candidate.taskStarted > 32503680000LL ||
            candidate.taskDue < 0 || candidate.taskDue > 32503701600LL) return false;
        if (duration ? (candidate.taskStarted == 0 || candidate.taskDue - candidate.taskStarted != duration) :
            (candidate.taskStarted != 0 || candidate.taskDue != 0)) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        in >> std::ws;
        if (in.peek() != '"') return false;
        std::string line;
        if (!(in >> std::quoted(line)) || line.size() > 240) return false;
        for (unsigned char c : line) if (c < 32 || c > 126) return false;
        candidate.journal.push_back(std::move(line));
    }
    in >> std::ws;
    if (!in.eof()) return false;
    // Essential progression invariants; do not accept a save that could skip
    // authentication or describe an impossible route.
    if (candidate.stage >= Stage::Briefing && (!candidate.has(Archive) || !candidate.has(Bearing))) return false;
    if (candidate.has(EastLanding) && !candidate.has(ProbeData)) return false;
    if (candidate.stage == Stage::Probe && candidate.has(ProbeData)) return false;
    if (candidate.stage == Stage::Intercept && (candidate.evidence || candidate.minutes || count)) return false;
    if (candidate.stage != Stage::Intercept && count == 0) return false;
    if (version == 1) startWaiting(candidate, std::time(nullptr));
    destination = std::move(candidate);
    return true;
}
enum class LoadResult { Missing, Loaded, Invalid };
inline LoadResult loadSave(const std::filesystem::path& path, Session& destination) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return LoadResult::Invalid;
    if (!exists) return LoadResult::Missing;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 128 * 1024) return LoadResult::Invalid;
    std::ifstream file(path, std::ios::binary);
    if (!file) return LoadResult::Invalid;
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return LoadResult::Invalid;
    return decodeSave(bytes, destination) ? LoadResult::Loaded : LoadResult::Invalid;
}
// The host replaces the destination atomically after this file has closed.
inline bool writeSaveFile(const std::filesystem::path& path, const Session& s) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << encodeSave(s);
    file.close();
    return !file.fail();
}
}
