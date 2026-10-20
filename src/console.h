// Copyright (C) 2026 Soni Romanov
// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace muthur {
enum class Action { Reply, Clear, Reboot, Exit };
struct Response { Action action = Action::Reply; std::vector<std::string> lines; };
enum class Stage {
    Intercept, Verification, Briefing, Transit, Orbit, Probe, Descent,
    Exterior, Perimeter, Door, Junction, Drain, Manual, Comms, Complete
};
enum Evidence : std::uint32_t {
    Archive = 1, Bearing = 2, ProbeData = 4, DoorScan = 8, Plans = 16,
    DetailedBrief = 32, Injury = 64, LowerPower = 128, Uplink = 256,
    EastLanding = 512, PerimeterRoute = 1024
};
struct Session {
    Stage stage = Stage::Intercept;
    std::uint32_t evidence = 0;
    int minutes = 0;
    bool restartPending = false;
    std::int64_t taskStarted = 0, taskDue = 0;
    bool scanning = false;
    std::vector<std::string> journal;
    bool has(Evidence value) const { return (evidence & value) != 0; }
};
inline int waitingSeconds(Stage stage) {
    switch (stage) {
    case Stage::Transit: return 360 * 60;
    case Stage::Probe: return 45 * 60;
    case Stage::Descent: return 18 * 60;
    case Stage::Perimeter: return 15 * 60;
    case Stage::Door: case Stage::Drain: return 4 * 60;
    case Stage::Manual: return 12 * 60;
    default: return 0;
    }
}
inline const char* taskName(const Session& s) {
    if (s.scanning) return "SCANNING";
    switch (s.stage) {
    case Stage::Transit: return "ORBITAL TRANSIT";
    case Stage::Probe: return "PROBE SURVEY";
    case Stage::Descent: return "LANDER DESCENT";
    case Stage::Perimeter: return "PERIMETER SEARCH";
    case Stage::Door: return "DOOR RELEASE";
    case Stage::Drain: return "DRAINAGE PUMP";
    case Stage::Manual: return "MANUAL BULKHEAD";
    default: return "NO ACTIVE TASK";
    }
}
inline std::string normalize(std::string s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    s = s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}
inline std::string elapsed(const Session& s) {
    return std::to_string(s.minutes / 60) + "H " + std::to_string(s.minutes % 60) + "M";
}
inline const char* chapter(const Session& s) {
    if (s.stage <= Stage::Briefing) return "01 / THE INTERRUPTION";
    if (s.stage <= Stage::Descent) return "02 / THE QUIET PLANET";
    if (s.stage == Stage::Complete) return "EPISODE 01 / SIGNAL RECOVERED";
    return "03 / STATION 04";
}
inline std::vector<std::string> opening() {
    return {"MU/TH/UR / SURVEY VESSEL KESTREL", "THE MERIDIAN SIGNAL / EPISODE 01", "",
        "You are the human officer on communications watch.",
        "Six crew in stasis. Routine mapping. Nothing overdue.", "",
        "MU/TH/UR / UNSCHEDULED CARRIER DETECTED",
        "An expedition identifier is repeating off our route.",
        "The registry marks that expedition as missing.", "",
        "Type SIGNAL to listen. HELP lists commands.",
        "Enter reveals a transmission. Page Up reads earlier text."};
}
inline std::vector<std::string> prompt(const Session& s) {
    if (s.restartPending) return {"NEW GAME WILL REPLACE YOUR SAVED EPISODE.",
        "Type CONFIRM RESTART, or CANCEL to keep playing."};
    if (s.scanning) return {"MU/TH/UR / SCANNING", "Acquiring telemetry. See the live progress panel.",
        "Results follow automatically. WAIT skips to results."};
    switch (s.stage) {
    case Stage::Intercept: return {"NEXT / Type SIGNAL to inspect the incoming carrier."};
    case Stage::Verification: {
        std::vector<std::string> out{"MU/TH/UR / VERIFY BEFORE WAKING COMMAND"};
        if (!s.has(Archive)) out.push_back("Type ARCHIVE MERIDIAN to check the expedition record.");
        if (!s.has(Bearing)) out.push_back("Type SCAN to locate and challenge the transmitter.");
        if (s.has(Archive) && s.has(Bearing)) out.push_back("1  Wake Captain Vale and present the contact.");
        return out;
    }
    case Stage::Briefing: return {"VALE / COMMAND", "Tell me what justifies leaving our survey route.", "",
        "1  Explain the missing ship and its live acknowledgment.",
        "2  Report verified distress. Request immediate diversion."};
    case Stage::Transit: return {"NEXT CONTACT / ARRIVAL IN SIX HOURS", "Type WAIT to advance to orbital insertion."};
    case Stage::Orbit:
        if (s.has(ProbeData)) return {"VALE / COMMAND", "Probe survey received. Select a landing site.", "",
            "1  Eastern shelf. Longer approach; stable ground. [+12 min]",
            "2  Western apron. Short approach; debris, poor visibility."};
        return {"VALE / COMMAND", "One survey probe available. How do we approach?", "",
            "1  Send the probe first. [+45 min / consumes probe]",
            "2  Land at the western apron now. Terrain unverified."};
    case Stage::Probe: return {"PROBE / SURVEY IN PROGRESS", "Type WAIT to advance 45 minutes to its report."};
    case Stage::Descent: return {"LANDER / DESCENT IN PROGRESS", "Type WAIT to advance 18 minutes to surface contact."};
    case Stage::Exterior: return {"ORTIZ / SURFACE", "At the station. Main entrance is sealed. Your call.",
        "SCAN STATION can check the door circuit before you decide.", "",
        s.has(DoorScan) || s.has(ProbeData) ? "1  Brace the damaged rail, then release the door." :
            "1  Release the powered door. Actuator condition uncertain.",
        "2  Search the perimeter for manual access. [+15 min]"};
    case Stage::Perimeter: return {"ORTIZ / SURFACE", "Checking the perimeter. Keep the channel open.",
        "Type WAIT to advance 15 minutes to the next contact."};
    case Stage::Door: return {"ORTIZ / SURFACE", "At the door controls. Waiting for the mechanism.",
        "Type WAIT to advance 4 minutes to the next contact."};
    case Stage::Junction: return {"ORTIZ / SURFACE", "The corridor to communications is flooded.",
        "ARCHIVE STATION may explain the pump wiring.", "",
        "1  Power the drainage pump. Shared circuit. [+4 min]",
        "2  Keep power isolated. Use the manual bulkhead. [+12 min]"};
    case Stage::Drain: return {"ORTIZ / SURFACE", "Pump running. Water level falling.",
        "Type WAIT to advance 4 minutes to the next contact."};
    case Stage::Manual: return {"ORTIZ / SURFACE", "Opening the bulkhead by hand. This will take a while.",
        "Type WAIT to advance 12 minutes to the next contact."};
    case Stage::Comms: return {"ORTIZ / SURFACE", "There is a fresh splice in the transmitter cables.",
        "Someone cut them. Then someone put them back together.", "",
        "1  Photograph the repair. Keep our equipment isolated.",
        "2  Connect the field reader. Recover its routing log.",
        "   Warning: this opens a data link to an unknown system."};
    case Stage::Complete: return {"END OF EPISODE 01 / THE MERIDIAN SIGNAL", "",
        "The investigation continues beyond this playable opening.",
        "Type LOG to review evidence, or STATUS for the outcome.",
        "NEW GAME lets you try a different approach."};
    }
    return {};
}
inline Response reply(std::vector<std::string> lines) { return {Action::Reply, std::move(lines)}; }
inline Response event(Session& s, std::vector<std::string> lines) {
    s.journal.push_back("[MISSION +" + elapsed(s) + "]");
    s.journal.insert(s.journal.end(), lines.begin(), lines.end());
    s.journal.push_back("");
    const auto next = prompt(s);
    lines.push_back("");
    lines.insert(lines.end(), next.begin(), next.end());
    return reply(std::move(lines));
}
inline Response discover(Session& s, Evidence evidence, std::vector<std::string> lines) {
    if (s.has(evidence)) return reply(std::move(lines));
    s.evidence |= evidence;
    return event(s, std::move(lines));
}
inline Response respondImmediate(const std::string& input, Session& s) {
    const auto q = normalize(input);
    if (q.empty()) return reply({});
    if (q == "EXIT" || q == "QUIT") return {Action::Exit, {}};
    if (q == "CLEAR") return {Action::Clear, prompt(s)};
    if (q == "REBOOT") return {Action::Reboot, {}};
    if (q == "NEW GAME" || q == "RESTART") {
        s.restartPending = true;
        return reply(prompt(s));
    }
    if (q == "CONFIRM RESTART") {
        if (!s.restartPending) return reply({"No restart requested. Type NEW GAME first."});
        s = {};
        return {Action::Reboot, {}};
    }
    if (q == "CANCEL") {
        s.restartPending = false;
        return reply(prompt(s));
    }
    if (q == "HELP") return reply({"MU/TH/UR / OPERATOR REFERENCE", "",
        "1 / 2          Select a numbered response when offered",
        "WAIT           Advance to the next scheduled contact",
        "COMMS          Repeat current message and choices",
        "SIGNAL / SCAN  Inspect transmissions and sensors",
        "ARCHIVE MERIDIAN / ARCHIVE STATION  Consult records",
        "STATUS / CREW / MISSION / LOG       Review progress",
        "CLEAR / REBOOT  Refresh terminal; preserve progress",
        "NEW GAME       Restart episode after confirmation",
        "EXIT / LICENSE Close app / read licensing terms", "",
        "Progress saves locally. Enter skips the typing effect.",
        "Timed tasks continue while the app is closed.",
        "Live panel shows local time, ETA and time remaining.",
        "WAIT fast-forwards the active task."});
    if (q == "LICENSE") return reply({"MU/TH/UR / COPYRIGHT (C) 2026 SONI ROMANOV", "",
        "MIT LICENSE.",
        "YOU MAY USE, MODIFY, AND REDISTRIBUTE THIS SOFTWARE.",
        "INCLUDE THE COPYRIGHT AND PERMISSION NOTICE.",
        "THIS PROGRAM COMES WITH ABSOLUTELY NO WARRANTY.",
        "SEE THE LICENSE FILE INCLUDED WITH THIS APP.", "",
        "SOURCE: HTTPS://GITHUB.COM/SONIROMANOV/MU-TH-UR"});
    if (q == "COMMS" || q == "REPEAT THAT") return reply(prompt(s));
    if (q == "MISSION") {
        auto lines = std::vector<std::string>{"MISSION / " + std::string(chapter(s)),
            "INITIAL DIRECTIVE: MAP THE OUTER SURVEY CORRIDOR.",
            s.stage >= Stage::Transit ? "ACTIVE DIRECTIVE: INVESTIGATE MERIDIAN DISTRESS." :
                "ACTIVE DIRECTIVE: AUTHENTICATE THE OFF-ROUTE SIGNAL.",
            "MISSION ELAPSED: " + elapsed(s), ""};
        auto next = prompt(s); lines.insert(lines.end(), next.begin(), next.end());
        return reply(std::move(lines));
    }
    if (q == "STATUS") return reply({"MU/TH/UR / KESTREL", "",
        "REACTOR / LIFE SUPPORT: NOMINAL",
        s.stage < Stage::Transit ? "COURSE: SURVEY CORRIDOR / DIVERSION NOT AUTHORIZED" :
            (s.stage == Stage::Transit ? "COURSE: DIVERTING TO MERIDIAN BEARING" : "COURSE: HOLDING PLANETARY ORBIT"),
        s.stage < Stage::Transit ? "RETURN FUEL: FULL CONTINGENCY RESERVE" : "RETURN FUEL: SAFE RETURN / LIMITED CONTINGENCY",
        std::string("SURVEY PROBE: ") + (s.stage == Stage::Probe ? "DEPLOYED" : (s.has(ProbeData) ? "EXPENDED" : "AVAILABLE")),
        std::string("LANDING TEAM: ") + (s.has(Injury) ? "ORTIZ / HAND INJURED, MOBILE" : "NO INJURIES REPORTED"),
        std::string("LOWER STATION CIRCUIT: ") + (s.has(LowerPower) ? "ENERGIZED BY PUMP" : "NOT ENERGIZED BY KESTREL"),
        std::string("FIELD DATA LINK: ") + (s.has(Uplink) ? "CONNECTED / UNVERIFIED TRAFFIC" : "ISOLATED"),
        "MISSION ELAPSED: " + elapsed(s)});
    if (q == "CREW") return reply({"KESTREL / PERSONNEL", "",
        "YOU / DUTY COMMUNICATIONS OFFICER / AT TERMINAL",
        std::string("VALE / COMMAND / ") + (s.stage >= Stage::Briefing ? "AWAKE" : "STASIS"),
        std::string("CHEN / NAVIGATION / ") + (s.stage >= Stage::Transit ? "AWAKE" : "STASIS"),
        std::string("ORTIZ / ENGINEERING / ") + (s.has(Injury) ? "SURFACE / HAND INJURED" :
            (s.stage >= Stage::Exterior ? "SURFACE / STABLE" : (s.stage >= Stage::Transit ? "AWAKE" : "STASIS"))),
        std::string("SHAW / MEDICAL / ") + (s.stage >= Stage::Exterior ? "SURFACE / STABLE" :
            (s.stage >= Stage::Transit ? "AWAKE" : "STASIS")),
        "KERR / SCIENCE / STASIS", "REED / OPERATIONS / STASIS", "",
        s.stage == Stage::Intercept ? "MERIDIAN / CARRIER AWAITING DECODE." :
            "MERIDIAN / THREE REMAIN, ACCORDING TO TRANSMISSION.",
        "NO SURVIVORS LOCATED OR VERIFIED."});
    if (q == "LOG") {
        auto lines = s.journal;
        lines.insert(lines.begin(), "MISSION LOG / RECORDED CONTACTS AND DECISIONS");
        if (s.journal.empty()) lines.push_back("No contacts recorded. Type SIGNAL to begin.");
        lines.push_back("END OF LOG / PAGE UP TO READ / COMMS FOR CURRENT CHOICES");
        return reply(std::move(lines));
    }
    if (q == "WHO AM I" || q == "WHO AM I?" || q == "ASK WHO AM I?")
        return reply({"You are Kestrel's human communications officer.", "Your post is this terminal. Ortiz leads the surface work."});
    if (q == "WHO ARE YOU" || q == "WHO ARE YOU?" || q == "ASK WHO ARE YOU?")
        return reply({"MU/TH/UR / KESTREL SYSTEMS INTERFACE", "I provide records, telemetry and communications.",
            "Mission decisions remain with the human crew."});
    if (s.restartPending) return reply(prompt(s));
    if (q == "SIGNAL") {
        if (s.stage == Stage::Intercept) {
            s.stage = Stage::Verification;
            return event(s, {"SIGNAL / DECODED FRAGMENT", "EXPEDITION MERIDIAN. SURFACE STATION 04.",
                "THREE PERSONNEL REMAIN. REQUEST RECOVERY. DO NOT--", "[PACKET LOST]", "",
                "MU/TH/UR / Identifier matches a missing expedition.",
                "A recording alone does not establish a live distress call."});
        }
        return reply({"SIGNAL / MERIDIAN / SURFACE STATION 04",
            s.stage == Stage::Complete ? "THREE REMAIN. REQUEST RECOVERY. DO NOT RESTORE THE LOWER ARRAY." :
                "THREE PERSONNEL REMAIN. REQUEST RECOVERY. DO NOT--",
            s.has(Bearing) ? "LIVE ACKNOWLEDGMENT VERIFIED." : "LIVE ORIGIN NOT YET VERIFIED. Type SCAN.",
            s.has(LowerPower) ? "NEW CARRIER ACTIVE BELOW THE STATION." : "ONE REPEATING CARRIER DETECTED."});
    }
    if (q == "ARCHIVE" || q == "ARCHIVE MERIDIAN") {
        if (s.stage == Stage::Intercept) return reply({"No active investigation. Type SIGNAL to inspect the contact."});
        return discover(s, Archive, {"ARCHIVE / EXPEDITION MERIDIAN", "Survey vessel. Twelve personnel. Lost nineteen years ago.",
            "Last position: nine light-years from this bearing.", "No return flight or surface station was registered.",
            "Final dispatch: 'Extending geological survey. Routine.'", "SEARCH CLOSED / NO WRECKAGE RECOVERED"});
    }
    if (q == "ARCHIVE STATION") {
        if (s.stage < Stage::Exterior) return reply({"Station model not identified. Await a surface inspection."});
        return discover(s, Plans, {"ARCHIVE / STANDARD SURVEY STATION / REVISION C", "Drainage pump shares its bus with the lower research rooms.",
            "Remote pump activation energizes BOTH branches.", "A manual bulkhead bypasses the flooded corridor.",
            "WARNING / Original plans. Local modifications unknown."});
    }
    if (q == "SCAN" || q == "SCAN STATION") {
        if (s.stage == Stage::Intercept) return reply({"Carrier at bearing 271.04 / -08.22. Type SIGNAL to decode."});
        if (s.stage <= Stage::Briefing) {
            if (!s.has(Bearing)) s.minutes += 11;
            return discover(s, Bearing, {"MU/TH/UR / ACTIVE CHALLENGE / +11 MINUTES", "Origin fixed: an unregistered planet off our survey route.",
                "Reply contains the code from our outgoing scan.", "This response was generated AFTER our transmission.",
                "An active system is listening. Human presence unconfirmed."});
        }
        if (s.stage < Stage::Exterior) return reply({"REMOTE SURVEY / STATION 04", "Surface structure located. No expedition vessel detected.",
            "Dust obscures access routes. Three underground heat returns.", "Heat returns are not confirmed life signs.",
            s.has(ProbeData) ? "Probe confirms eastern shelf and a damaged door rail." : "A close probe survey could resolve the approach."});
        // Once committed to opening the door, a later scan cannot retroactively
        // give Ortiz a warning. The choice captures the available evidence.
        if (s.stage != Stage::Exterior && !s.has(DoorScan))
            return reply({"STATION / NO NEW SAFE REMOTE TEST AVAILABLE",
                s.has(LowerPower) ? "Lower research circuit is drawing power." : "Interior readings remain incomplete."});
        return discover(s, DoorScan, {"STATION TELEMETRY / LIMITED REMOTE ACCESS", "Auxiliary power available. Door actuator draws uneven current.",
            "Main entrance may move unexpectedly when released.", "Tell Ortiz to brace the door before applying power.",
            "No reliable interior personnel count available."});
    }
    if (q == "WAIT") {
        switch (s.stage) {
        case Stage::Transit:
            s.minutes += 360; s.stage = Stage::Orbit;
            return event(s, {"MU/TH/UR / SIX HOURS LATER / ORBIT ESTABLISHED", "Station 04 is on the night side. No ship in orbit.",
                "Three heat returns below the surface. Source uncertain.", "", "ORTIZ / LANDER", "Shaw and I are ready. I'd like to know what we're landing on."});
        case Stage::Probe:
            s.minutes += 45; s.evidence |= ProbeData; s.stage = Stage::Orbit;
            return event(s, {"PROBE / FINAL SURVEY / BATTERY EXPENDED", "Eastern shelf is stable. Western apron is strewn with debris.",
                "Station entrance: bent door rail. Brace before release.", "No movement detected. No Meridian landing craft found.",
                "ORTIZ / LANDER", "Good call. That rail would have caught us by surprise."});
        case Stage::Descent:
            s.minutes += 18; s.stage = Stage::Exterior;
            return event(s, {"ORTIZ / SURFACE", s.has(EastLanding) ?
                "Down on the eastern shelf. Long walk, solid footing." : "Down on the western apron. Dust right up to our knees.",
                "Station lights are out. No footprints near the lander.",
                "There are twelve names beside the entrance.", "Someone has scratched out nine of them."});
        case Stage::Perimeter:
            s.minutes += 15; s.stage = Stage::Junction;
            return event(s, {"ORTIZ / SURFACE", "Found a service hatch. Manual release still works.",
                "We're inside. No station power needed.", "Someone painted arrows toward communications.",
                "All the arrows pointing downstairs have been crossed out."});
        case Stage::Door:
            s.minutes += 4; s.stage = Stage::Junction;
            if (!s.has(DoorScan) && !s.has(ProbeData)) s.evidence |= Injury;
            return event(s, {"ORTIZ / SURFACE", s.has(Injury) ?
                "Door jumped its rail. Caught my hand. Shaw is dressing it." :
                "Braced the damaged rail. Door is open. We're through.",
                s.has(Injury) ? "I can keep going. Check the equipment before the next order." :
                "Thanks for catching that before we put our hands near it.",
                "Painted arrows lead to communications.", "Every arrow pointing downstairs has been crossed out."});
        case Stage::Drain:
            s.minutes += 4; s.stage = Stage::Comms;
            return event(s, {"ORTIZ / SURFACE", "Corridor drained. We're in communications.",
                "When the pump started, lights came on below us.", "MU/TH/UR / SECOND CARRIER DETECTED BELOW STATION", "",
                "ORTIZ / SURFACE", "The transmitter was deliberately disconnected.", "The repair is clean. The insulation is still soft."});
        case Stage::Manual:
            s.minutes += 12; s.stage = Stage::Comms;
            return event(s, {"ORTIZ / SURFACE", "Through the bulkhead. Communications room is dry.",
                "The lower rooms are still dark. Nothing moving.", "The transmitter was deliberately disconnected.",
                "The repair is clean. The insulation is still soft.", "These tools haven't been lying here for nineteen years."});
        default: return reply({"No contact is scheduled. Waiting will not choose for you.", "Type COMMS for the current decision."});
        }
    }
    if (q == "1" || q == "2") {
        const bool first = q == "1";
        switch (s.stage) {
        case Stage::Verification:
            if (!first || !s.has(Archive) || !s.has(Bearing)) return reply(prompt(s));
            s.minutes += 18; s.stage = Stage::Briefing;
            return event(s, {"OPERATOR / WAKE CAPTAIN VALE", "MU/TH/UR / COMMAND REVIVAL COMPLETE / +18 MINUTES", "",
                "VALE / COMMAND", "Nineteen years. And it chooses our watch to call."});
        case Stage::Briefing:
            if (first) s.evidence |= DetailedBrief;
            s.stage = Stage::Transit;
            return event(s, {first ? "OPERATOR / FULL EVIDENCE BRIEF TRANSMITTED" : "OPERATOR / IMMEDIATE DIVERSION REQUESTED",
                "VALE / COMMAND", first ? "A fresh reply from a missing ship's code. Good work." :
                    "Verified distress is sufficient. Keep the raw records.",
                "Diversion authorized. Wake navigation, engineering and medical.",
                "We can return safely, but our fuel margin will be narrow.",
                "MU/TH/UR / SURVEY SUSPENDED / NEW COURSE ACCEPTED"});
        case Stage::Orbit:
            if (!s.has(ProbeData) && first) {
                s.stage = Stage::Probe;
                return event(s, {"OPERATOR / PROBE SURVEY AUTHORIZED", "MU/TH/UR / SURVEY PROBE RELEASED"});
            }
            if (s.has(ProbeData) && first) { s.evidence |= EastLanding; s.minutes += 12; }
            s.stage = Stage::Descent;
            return event(s, {s.has(EastLanding) ? "OPERATOR / EASTERN APPROACH SELECTED / +12 MINUTES" :
                "OPERATOR / WESTERN LANDING SITE SELECTED",
                "ORTIZ / LANDER", "Shaw has the medical kit. I've got the field reader.",
                "You'll have our voice channel and suit telemetry.", "Keep talking to us down there."});
        case Stage::Exterior:
            s.stage = first ? Stage::Door : Stage::Perimeter;
            if (!first) s.evidence |= PerimeterRoute;
            return event(s, {first ? "OPERATOR / RELEASE MAIN ENTRANCE" : "OPERATOR / SEARCH FOR MANUAL ACCESS",
                first && (s.has(ProbeData) || s.has(DoorScan)) ?
                    "OPERATOR / Brace the damaged rail before release." : "ORTIZ / Understood."});
        case Stage::Junction:
            s.stage = first ? Stage::Drain : Stage::Manual;
            if (first) s.evidence |= LowerPower;
            return event(s, {first ? "OPERATOR / ENERGIZE SHARED PUMP CIRCUIT" : "OPERATOR / KEEP POWER ISOLATED; USE MANUAL BULKHEAD",
                first && s.has(Plans) ? "MU/TH/UR / Lower research bus will also receive power." :
                    "ORTIZ / Following your instructions."});
        case Stage::Comms:
            s.minutes += 3;
            if (!first) s.evidence |= Uplink;
            s.stage = Stage::Complete;
            return event(s, {first ? "OPERATOR / DOCUMENT REPAIR; KEEP EQUIPMENT ISOLATED" : "OPERATOR / CONNECT FIELD READER",
                first ? "ORTIZ / Repair photographed. Fresh tool marks confirmed." :
                    "FIELD READER / REPAIR LOG: LOCAL ACCESS / 36 HOURS AGO",
                first ? "ORTIZ / Reader stays disconnected. We'll preserve the scene." :
                    "FIELD READER / UNSOLICITED REQUEST FOR KESTREL CREW MANIFEST",
                "", "SIGNAL / RECOVERED PACKET", "THREE PERSONNEL REMAIN. REQUEST RECOVERY.",
                "DO NOT RESTORE THE LOWER ARRAY.", "",
                s.has(LowerPower) ? "ORTIZ / Those lights downstairs just went out." :
                    "ORTIZ / There's a stairwell behind the transmitter.",
                s.has(DetailedBrief) ? "VALE / Keep the evidence coming. Nobody goes down yet." :
                    "VALE / Hold position. I want the complete record this time."});
        default: return reply({"No numbered response is available here. Type COMMS."});
        }
    }
    return reply({"Command not recognized. No action taken.",
        "Use the displayed numbers, COMMS for your next step, or HELP.",
        "Story decisions require an exact command or numbered choice."});
}
inline void startWaiting(Session& s, std::int64_t now) {
    const auto duration = waitingSeconds(s.stage);
    if (duration && !s.taskDue) { s.taskStarted = now; s.taskDue = now + duration; }
}
inline Response finishTask(Session& s) {
    const bool scan = s.scanning;
    s.scanning = false; s.taskStarted = s.taskDue = 0;
    // A pending restart confirmation must not prevent an already-running task.
    const bool restarting = s.restartPending;
    s.restartPending = false;
    auto result = respondImmediate(scan ? "SCAN" : "WAIT", s);
    s.restartPending = restarting;
    return result;
}
inline Response tick(Session& s, std::int64_t now) {
    if (s.taskDue && now >= s.taskDue) return finishTask(s);
    return reply({});
}
inline Response respond(const std::string& input, Session& s, std::int64_t now = std::time(nullptr)) {
    const auto q = normalize(input);
    if (q == "WAIT" && s.taskDue && !s.restartPending) return finishTask(s);
    if (s.scanning && (q == "1" || q == "2" || q == "SCAN" || q == "SCAN STATION"))
        return reply({"SCAN IN PROGRESS / See live ETA. WAIT skips to results."});
    if (!s.restartPending && (q == "SCAN" || q == "SCAN STATION") &&
        ((s.stage == Stage::Verification && !s.has(Bearing)) ||
         (s.stage == Stage::Exterior && !s.has(DoorScan)))) {
        s.scanning = true; s.taskStarted = now; s.taskDue = now + 8;
        return reply({"MU/TH/UR / SCANNING", "Acquiring telemetry. Results follow automatically.",
            "WAIT skips to results. Live panel shows completion ETA."});
    }
    const auto previous = s.stage;
    auto result = respondImmediate(input, s);
    if (previous != s.stage) { s.taskStarted = s.taskDue = 0; startWaiting(s, now); }
    return result;
}
inline Response respond(const std::string& input) { Session s; return respond(input, s); }
}
