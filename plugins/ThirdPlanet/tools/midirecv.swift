// midirecv [port-substring] [--ch N] [--clock] [--secs S]
// Prints every MIDI message from the matching CoreMIDI sources with a millisecond timestamp.
// Default port substring: "MPC Live III". Clock (0xF8) is counted, not printed, unless --clock.
// Build: swiftc -O midirecv.swift -o midirecv
import CoreMIDI
import Foundation

var match = "MPC Live III"
var chFilter = 0
var showClock = false
var secs = 0.0
var it = CommandLine.arguments.dropFirst().makeIterator()
while let a = it.next() {
    switch a {
    case "--ch": chFilter = Int(it.next() ?? "0") ?? 0
    case "--clock": showClock = true
    case "--secs": secs = Double(it.next() ?? "0") ?? 0
    case "--list":
        for i in 0..<MIDIGetNumberOfSources() { print("source: \(name(MIDIGetSource(i)))") }
        exit(0)
    default: match = a
    }
}

func name(_ e: MIDIEndpointRef) -> String {
    var n: Unmanaged<CFString>?
    MIDIObjectGetStringProperty(e, kMIDIPropertyDisplayName, &n)
    return (n?.takeRetainedValue() as String?) ?? "?"
}

let t0 = Date().timeIntervalSince1970
var clocks = 0
var lastClockReport = t0
let lock = NSLock()

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
        case 0xF0: return "sysex (\(b.count) bytes)"
        default: return b.map { String(format: "%02X", $0) }.joined(separator: " ")
        }
    }
}

func handle(_ bytes: [UInt8], _ src: String) {
    // Split a packet into messages (running status not expected from CoreMIDI).
    var i = 0
    while i < bytes.count {
        let s = bytes[i]
        var len = 1
        if s == 0xF0 { len = (bytes[i...].firstIndex(of: 0xF7) ?? bytes.count - 1) - i + 1 }
        else if s < 0xF0 { len = (s & 0xF0 == 0xC0 || s & 0xF0 == 0xD0) ? 2 : 3 }
        else if s == 0xF2 { len = 3 }
        else if s == 0xF1 || s == 0xF3 { len = 2 }
        let msg = Array(bytes[i..<min(i + len, bytes.count)])
        i += len
        let now = Date().timeIntervalSince1970
        lock.lock(); defer { lock.unlock() }
        if s == 0xF8 && !showClock {
            clocks += 1
            if now - lastClockReport >= 1.0 {
                print(String(format: "%9.1f  [%@] clock %d/s", (now - t0) * 1000, src, clocks))
                clocks = 0; lastClockReport = now
            }
            continue
        }
        if chFilter > 0 && s < 0xF0 && Int(s & 0x0F) + 1 != chFilter { continue }
        print(String(format: "%9.1f  [%@] %@", (now - t0) * 1000, src, describe(msg)))
    }
    fflush(stdout)
}

var client = MIDIClientRef()
MIDIClientCreate("midirecv" as CFString, nil, nil, &client)
var port = MIDIPortRef()
MIDIInputPortCreateWithProtocol(client, "in" as CFString, ._1_0, &port) { list, srcRef in
    let src = srcRef.map { Unmanaged<NSString>.fromOpaque($0).takeUnretainedValue() as String } ?? "?"
    for p in list.unsafeSequence() {
        let words = MIDIEventPacket.WordCollection(p)
        var bytes: [UInt8] = []
        for w in words {
            // MIDI 1.0 UMP: type 1 = system (1 word), type 2 = channel voice (1 word)
            let type = w >> 28
            if type == 1 || type == 2 {
                let b1 = UInt8((w >> 16) & 0xFF), b2 = UInt8((w >> 8) & 0xFF), b3 = UInt8(w & 0xFF)
                let len: Int
                if type == 2 { len = (b1 & 0xF0 == 0xC0 || b1 & 0xF0 == 0xD0) ? 2 : 3 }
                else { len = b1 == 0xF2 ? 3 : (b1 == 0xF1 || b1 == 0xF3) ? 2 : 1 }
                bytes += [b1, b2, b3].prefix(len)
            }
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
guard found > 0 else { print("no source matching \(match) (use --list)"); exit(1) }
if secs > 0 { DispatchQueue.main.asyncAfter(deadline: .now() + secs) { exit(0) } }
RunLoop.main.run()
