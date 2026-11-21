// midirecv [port-substring] [--ch N] [--clock] [--secs S] [--link] [--full]
// midirecv --decode-file FILE
// midirecv --list
// Prints every MIDI message from the matching CoreMIDI sources with a millisecond timestamp.
// Default port substring: "MPC Live III". Clock (0xF8) is counted, not printed, unless --clock.
// SysEx is reassembled from CoreMIDI's UMP packets (at most 160 bytes; longer ones are dropped) and printed as
// "sysex (N bytes)", or with --link decoded as Harmony Link v1 (plugins/ThirdPlanet/docs/harmony-link.md):
//   PLAN #33 key E Lydian | now Emaj7#11 → next Am9 held | sec Bridge bar 7/8 | layers D:90 S:0 ... | timed pos 13
//   EVENT You NextChord        BYE #5        FADE level 0 bars 16        HOLD on
// --full adds a PLAN's other fields (flags, slots, ...) on indented lines. A lost Third Planet message (a gap in
// seq) is noted; any other SysEx prints as hex.
// --decode-file decodes every "NAME: hex" line of a vectors file (docs/harmony-link-vectors.txt) with --full
// detail, and exits 1 if any line fails to decode or there are none.
// Build: swiftc -O midirecv.swift -o midirecv
import CoreMIDI
import Foundation

var match = "MPC Live III"
var chFilter = 0
var showClock = false
var secs = 0.0
var linkMode = false
var full = false
var decodeFile: String?
var it = CommandLine.arguments.dropFirst().makeIterator()
while let a = it.next() {
    switch a {
    case "--ch": chFilter = Int(it.next() ?? "0") ?? 0
    case "--clock": showClock = true
    case "--secs": secs = Double(it.next() ?? "0") ?? 0
    case "--link": linkMode = true
    case "--full": full = true
    case "--decode-file":
        guard let f = it.next() else { print("--decode-file needs a file"); exit(2) }
        decodeFile = f
    case "--list":
        for i in 0..<MIDIGetNumberOfSources() { print("source: \(name(MIDIGetSource(i)))") }
        exit(0)
    default:
        if a.hasPrefix("--") { print("unknown option \(a)"); exit(2) }
        match = a
    }
}

func name(_ e: MIDIEndpointRef) -> String {
    var n: Unmanaged<CFString>?
    MIDIObjectGetStringProperty(e, kMIDIPropertyDisplayName, &n)
    return (n?.takeRetainedValue() as String?) ?? "?"
}

func hex(_ b: [UInt8]) -> String { b.map { String(format: "%02X", $0) }.joined(separator: " ") }

// MARK: Harmony Link v1 decoder (docs/harmony-link.md; src/link_codec.cpp is the reference encoder)

let kNoteName = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
let kModeName = ["Major", "Dorian", "Phrygian", "Lydian", "Mixolyd.", "Minor", "Pent maj", "Pent min"]  // EN_MODE_NAME
let kSection = ["Emerge", "Flow", "Solo", "Bridge"]
let kLayerShort = ["D", "S", "L", "S2", "A"]  // the five mute layers: Drone, Seq, Lead, Seq 2, Atmos
let kWho = ["You", "Evolve", "Piece"]  // tp::app::Who
let kWhat = ["Param", "Voice", "Hold", "Lock", "Play", "Forget", "NextChord", "NewChords", "ShakeAll", "Reset",
             "Mute", "Solo", "Nudge", "Shake", "Piece"]  // tp::app::What
let kSlotState = ["gap", "walk", "written"]
let kLinkLen: [UInt8: Int] = [0x01: 146, 0x02: 17, 0x03: 8, 0x10: 11, 0x11: 9]
let kMaxSysex = 160

struct LinkMsg {
    var type: UInt8
    var seq: Int
    var line: String       // the one-line summary
    var detail: [String]   // extra lines (PLAN only)
}

enum LinkResult {
    case notLink              // does not start F0 7D 54 50
    case bad(String)          // starts like Link but is malformed
    case ok(LinkMsg)
}

func isLink(_ m: [UInt8]) -> Bool { m.count >= 4 && m[0] == 0xF0 && m[1] == 0x7D && m[2] == 0x54 && m[3] == 0x50 }

func chordName(_ p: ArraySlice<UInt8>) -> String {
    let b = Array(p)
    if b[0] == 127 { return "-" }
    let n = String(bytes: b[3..<11].prefix { $0 != 0 }, encoding: .ascii) ?? "?"
    if !n.isEmpty { return n }
    return b[0] < 12 ? kNoteName[Int(b[0])] : "root\(b[0])"
}

func chordFull(_ p: ArraySlice<UInt8>) -> String {
    let b = Array(p)
    let mask = Int(b[1]) | Int(b[2]) << 7
    let pcs = (0..<12).filter { mask & (1 << $0) != 0 }.map(String.init).joined(separator: ",")
    let root = b[0] == 127 ? "none" : b[0] < 12 ? kNoteName[Int(b[0])] : "\(b[0])"
    return "\(chordName(p)) (root \(root), mask 0x\(String(format: "%03X", mask)) {\(pcs)})"
}

func name(_ table: [String], _ v: Int) -> String { v >= 0 && v < table.count ? table[v] : "?\(v)" }
func s14(_ lo: UInt8, _ hi: UInt8) -> Int { (Int(lo) | Int(hi) << 7) - 8192 }

func decodeLink(_ m: [UInt8]) -> LinkResult {
    guard isLink(m) else { return .notLink }
    guard m.count >= 8 else { return .bad("too short (\(m.count) bytes)") }
    guard m.last == 0xF7 else { return .bad("no F7 at the end") }
    if let i = m[1..<(m.count - 1)].firstIndex(where: { $0 > 0x7F }) {
        return .bad(String(format: "byte %d is %02X, over 7F", i, m[i]))
    }
    guard m[4] == 0x01 else { return .bad("version \(m[4]), not 1") }
    let type = m[5]
    guard let want = kLinkLen[type] else { return .bad(String(format: "unknown type %02X", type)) }
    guard m.count == want else { return .bad(String(format: "type %02X is %d bytes, want %d", type, m.count, want)) }
    let seq = Int(m[6])
    let p = Array(m[7..<(m.count - 1)])
    switch type {
    case 0x01:
        let flags = p[0]
        let timed = flags & 16 != 0
        let toNext = Int(p[26]) - 64, toChange = Int(p[27]) - 64
        let key = "\(name(kNoteName, Int(p[2]))) \(name(kModeName, Int(p[3])))"
        let now = chordName(p[4..<15]), next = chordName(p[15..<26])
        let when = toNext == -2 ? "held" : toNext < 0 ? "(no count)" : toNext == 1 ? "in 1 bar" : "in \(toNext) bars"
        let layers = (0..<5).map { "\(kLayerShort[$0]):\(p[132 + $0])" }.joined(separator: " ")
        let line = "PLAN #\(seq) key \(key) | now \(now) → next \(next) \(when) | sec \(name(kSection, Int(p[127]))) "
            + "bar \(Int(p[128]) + 1)/8 | layers \(layers) | \(timed ? "timed" : "untimed") pos \(p[137])"
        let fl = [(1, "playing"), (2, "held"), (4, "locked"), (8, "loopMode"), (16, "timed")]
            .map { "\($0.1) \(flags & UInt8($0.0) != 0 ? 1 : 0)" }.joined(separator: ", ")
        var d = ["flags: \(fl); piece \(p[1]), keyRoot \(p[2]), keyMode \(p[3])",
                 "now: \(chordFull(p[4..<15]))",
                 "next: \(chordFull(p[15..<26])); barsToNext \(toNext), barsToChange \(toChange)",
                 "progPos \(Int(p[28]) - 1), progCount \(p[29]), walk \(p[30])"]
        for s in 0..<8 {
            let o = 31 + s * 12
            d.append("slot \(s): \(name(kSlotState, Int(p[o]))), \(chordFull(p[(o + 1)..<(o + 12)]))")
        }
        let heard = (0..<5).filter { p[131] & (1 << $0) != 0 }.map { kLayerShort[$0] }.joined(separator: " ")
        d.append("section \(p[127]) (\(name(kSection, Int(p[127])))), phraseBar \(p[128]), evolveChance \(p[129]), "
                 + "densityShift \(Int(p[130]) - 64)")
        d.append(String(format: "heardMask 0x%02X (%@), activity %@; pos16 %d", p[131], heard,
                        (0..<5).map { String(p[132 + $0]) }.joined(separator: " "), p[137]))
        return .ok(LinkMsg(type: type, seq: seq, line: line, detail: d))
    case 0x02:
        let what = Int(p[1]), layer = Int(p[2]) - 1, param = s14(p[3], p[4])
        let from = s14(p[5], p[6]), to = s14(p[7], p[8])
        var line = "EVENT \(name(kWho, Int(p[0]))) \(name(kWhat, what))"
        if layer >= 0 { line += " layer \(layer)" }
        if param >= 0 { line += " param \(param)" }
        // Values matter for parameters, voices, the on/off switches and pieces; elsewhere they are 0.
        if [0, 1, 2, 3, 4, 10, 11, 14].contains(what) || from != 0 || to != 0 { line += " \(from) → \(to)" }
        return .ok(LinkMsg(type: type, seq: seq, line: line, detail: []))
    case 0x03:
        return .ok(LinkMsg(type: type, seq: seq, line: "BYE #\(seq)", detail: []))
    case 0x10:
        let bars = Int(p[1]) | Int(p[2]) << 7
        return .ok(LinkMsg(type: type, seq: seq, line: "FADE level \(p[0]) bars \(bars)", detail: []))
    default:  // 0x11
        guard p[0] <= 1 else { return .bad("HOLD on is \(p[0]), not 0 or 1") }
        return .ok(LinkMsg(type: type, seq: seq, line: "HOLD " + (p[0] == 1 ? "on" : "off"), detail: []))
    }
}

// MARK: --decode-file

func parseHex(_ s: Substring) -> [UInt8]? {
    var out: [UInt8] = []
    for tok in s.split(whereSeparator: { $0 == " " || $0 == "\t" }) {
        guard tok.count == 2, let v = UInt8(tok, radix: 16) else { return nil }
        out.append(v)
    }
    return out.isEmpty ? nil : out
}

if let path = decodeFile {
    guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { print("cannot read \(path)"); exit(2) }
    var n = 0, failed = 0
    for raw in text.split(separator: "\n", omittingEmptySubsequences: false) {
        let line = raw.trimmingCharacters(in: .whitespaces)
        if line.isEmpty || line.hasPrefix("#") { continue }
        n += 1
        guard let c = line.firstIndex(of: ":"), let bytes = parseHex(line[line.index(after: c)...]) else {
            print("FAIL unparsable line: \(line)"); failed += 1; continue
        }
        let label = line[..<c]
        switch decodeLink(bytes) {
        case .ok(let m):
            print("\(label): \(m.line)")
            for d in m.detail { print("    \(d)") }
        case .bad(let why): print("FAIL \(label): \(why)"); failed += 1
        case .notLink: print("FAIL \(label): not Harmony Link (no F0 7D 54 50)"); failed += 1
        }
    }
    if n == 0 { print("FAIL no vectors in \(path)"); exit(1) }
    print("\(n - failed)/\(n) vectors decoded")
    exit(failed == 0 ? 0 : 1)
}

// MARK: live

let t0 = Date().timeIntervalSince1970
var clocks = 0
var lastClockReport = t0
let lock = NSLock()
var lastSeq: [String: Int] = [:]  // per source: the last seq of a Third Planet message (PLAN, EVENT, BYE)

func emit(_ src: String, _ text: String) {
    print(String(format: "%9.1f  [%@] %@", (Date().timeIntervalSince1970 - t0) * 1000, src, text))
}

func describeSysex(_ m: [UInt8], _ src: String) -> [String] {
    guard linkMode else { return ["sysex (\(m.count) bytes)"] }
    switch decodeLink(m) {
    case .notLink: return ["sysex (\(m.count) bytes) \(hex(m))"]
    case .bad(let why): return ["LINK? \(why): \(hex(m))"]
    case .ok(let msg):
        var line = msg.line
        if msg.type < 0x10 {  // Third Planet's own counter: note lost messages
            if let last = lastSeq[src], (last + 1) & 0x7F != msg.seq {
                line += "  [seq gap: \(last) → \(msg.seq), \(((msg.seq - last - 1) & 0x7F)) lost]"
            }
            lastSeq[src] = msg.seq
        }
        return [line] + (full ? msg.detail.map { "    " + $0 } : [])
    }
}

func describe(_ b: [UInt8]) -> String {
    guard let s = b.first else { return "" }
    let ch = Int(s & 0x0F) + 1
    switch s & 0xF0 {
    case 0x80: return "off ch\(ch) n\(b[1])"
    case 0x90: return "on  ch\(ch) n\(b[1]) v\(b[2])"
    case 0xB0: return "cc  ch\(ch) #\(b[1]) = \(b[2])"
    case 0xC0: return "pc  ch\(ch) \(b[1])"
    case 0xE0: return "pb  ch\(ch)"
    default:
        switch s {
        case 0xFA: return "START"
        case 0xFB: return "CONTINUE"
        case 0xFC: return "STOP"
        case 0xF2: return "SPP \(Int(b[1]) | Int(b[2]) << 7)"
        default: return hex(b)
        }
    }
}

func handle(_ bytes: [UInt8], _ src: String) {
    // Split a packet's short messages (running status not expected from CoreMIDI). SysEx comes separately.
    var i = 0
    while i < bytes.count {
        let s = bytes[i]
        var len = 1
        if s < 0xF0 { len = (s & 0xF0 == 0xC0 || s & 0xF0 == 0xD0) ? 2 : 3 }
        else if s == 0xF2 { len = 3 }
        else if s == 0xF1 || s == 0xF3 { len = 2 }
        let msg = Array(bytes[i..<min(i + len, bytes.count)])
        i += len
        let now = Date().timeIntervalSince1970
        if s == 0xF8 && !showClock {
            clocks += 1
            if now - lastClockReport >= 1.0 {
                emit(src, "clock \(clocks)/s")
                clocks = 0; lastClockReport = now
            }
            continue
        }
        if chFilter > 0 && s < 0xF0 && Int(s & 0x0F) + 1 != chFilter { continue }
        emit(src, describe(msg))
    }
}

// SysEx reassembly from UMP type 3 (sysex7) packets, per source and group, across callbacks.
struct SysexBuf { var data: [UInt8] = []; var active = false; var overflow = false }
var sysex: [String: SysexBuf] = [:]

func sysex7(_ w0: UInt32, _ w1: UInt32, _ src: String) {
    let status = (w0 >> 20) & 0xF, count = min(Int((w0 >> 16) & 0xF), 6)
    let all: [UInt8] = [UInt8((w0 >> 8) & 0xFF), UInt8(w0 & 0xFF), UInt8(w1 >> 24), UInt8((w1 >> 16) & 0xFF),
                        UInt8((w1 >> 8) & 0xFF), UInt8(w1 & 0xFF)]
    let bytes = all.prefix(count)
    let key = "\(src)#\((w0 >> 24) & 0xF)"
    var b = sysex[key] ?? SysexBuf()
    if status == 0 || status == 1 {  // complete or start: a new message (an unfinished one is dropped)
        if b.active { emit(src, "sysex dropped: no end (\(b.data.count) bytes)") }
        b = SysexBuf(data: [0xF0], active: true, overflow: false)
    } else if !b.active {  // continue or end with no start
        emit(src, "sysex dropped: continue without start")
        sysex[key] = b
        return
    }
    if !b.overflow {
        b.data += bytes
        if b.data.count + 1 > kMaxSysex { b.overflow = true; b.data.removeAll() }
    }
    if status == 0 || status == 3 {  // complete or end
        if b.overflow { emit(src, "sysex dropped: over \(kMaxSysex) bytes") }
        else { for l in describeSysex(b.data + [0xF7], src) { emit(src, l) } }
        b = SysexBuf()
    }
    sysex[key] = b
}

var client = MIDIClientRef()
MIDIClientCreate("midirecv" as CFString, nil, nil, &client)
var port = MIDIPortRef()
MIDIInputPortCreateWithProtocol(client, "in" as CFString, ._1_0, &port) { list, srcRef in
    let src = srcRef.map { Unmanaged<NSString>.fromOpaque($0).takeUnretainedValue() as String } ?? "?"
    lock.lock(); defer { lock.unlock(); fflush(stdout) }
    for p in list.unsafeSequence() {
        let words = Array(MIDIEventPacket.WordCollection(p))
        var bytes: [UInt8] = []
        var i = 0
        while i < words.count {
            let w = words[i]
            let type = w >> 28
            // UMP packet sizes in 32-bit words, by message type.
            let size = [1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4][Int(type)]
            // MIDI 1.0 UMP: type 1 = system (1 word), type 2 = channel voice (1 word), type 3 = sysex7 (2 words)
            if type == 1 || type == 2 {
                let b1 = UInt8((w >> 16) & 0xFF), b2 = UInt8((w >> 8) & 0xFF), b3 = UInt8(w & 0xFF)
                let len: Int
                if type == 2 { len = (b1 & 0xF0 == 0xC0 || b1 & 0xF0 == 0xD0) ? 2 : 3 }
                else { len = b1 == 0xF2 ? 3 : (b1 == 0xF1 || b1 == 0xF3) ? 2 : 1 }
                bytes += [b1, b2, b3].prefix(len)
            } else if type == 3 && i + 1 < words.count {
                if !bytes.isEmpty { handle(bytes, src); bytes = [] }
                sysex7(w, words[i + 1], src)
            }
            i += size
        }
        handle(bytes, src)
    }
}

var found = 0
for i in 0..<MIDIGetNumberOfSources() {
    let s = MIDIGetSource(i)
    let n = name(s)
    if n.contains(match) {
        let ref = Unmanaged.passRetained(n as NSString).toOpaque()
        MIDIPortConnectSource(port, s, ref)
        print("listening: \(n)")
        found += 1
    }
}
fflush(stdout)
guard found > 0 else { print("no source matching \(match) (use --list)"); exit(1) }
if secs > 0 { DispatchQueue.main.asyncAfter(deadline: .now() + secs) { exit(0) } }
RunLoop.main.run()
