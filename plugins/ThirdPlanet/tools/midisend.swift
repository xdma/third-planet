// midisend <port-substring> token...
// tokens: cc:CH:NUM:VAL  on:CH:NOTE:VEL  off:CH:NOTE  wait:MS   (CH is 1..16)
//         start  stop  cont  spp:16THS  clock:BPM:MS
//         sx:HEX...        raw SysEx, F0 to F7 included ("sx:F07D5450011100 01F7"; spaces, commas allowed)
//         fade:LEVEL:BARS  Harmony Link v1 FADE: level 0..127, bars 0..16383 (plugins/ThirdPlanet/docs/harmony-link.md)
//         hold:0|1         Harmony Link v1 HOLD off or on
// FADE and HOLD carry a running seq, 0 for the first one of a run. Tokens are sent in order.
// Build: swiftc -O midisend.swift -o midisend
import CoreMIDI
import Foundation

let args = CommandLine.arguments
guard args.count >= 2 else { print("usage: midisend <port> tokens..."); exit(2) }

func bad(_ t: String, _ why: String) -> Never { print("bad token \(t): \(why)"); exit(2) }
func hex(_ b: [UInt8]) -> String { b.map { String(format: "%02X", $0) }.joined(separator: " ") }

// Parse every token before sending anything, so a typo sends nothing.
enum Step { case bytes([UInt8], String?), wait(Int), clock(Int, Int) }
var steps: [Step] = []
var seq = 0
func linkMsg(_ type: UInt8, _ payload: [UInt8]) -> [UInt8] {
    defer { seq = (seq + 1) & 0x7F }
    return [0xF0, 0x7D, 0x54, 0x50, 0x01, type, UInt8(seq)] + payload + [0xF7]
}
for t in args.dropFirst(2) {
    let f = t.split(separator: ":", omittingEmptySubsequences: false).map(String.init)
    func ints(_ n: Int) -> [Int] {
        let v = f.dropFirst().compactMap { Int($0) }
        guard f.count == n + 1, v.count == n else { bad(t, "want \(n) number(s)") }
        return v
    }
    func ch(_ c: Int) -> UInt8 {
        guard (1...16).contains(c) else { bad(t, "channel \(c) not 1..16") }
        return UInt8(c - 1)
    }
    func b7(_ x: Int) -> UInt8 {
        guard (0...127).contains(x) else { bad(t, "\(x) not 0..127") }
        return UInt8(x)
    }
    switch f[0] {
    case "cc": let v = ints(3); steps.append(.bytes([0xB0 + ch(v[0]), b7(v[1]), b7(v[2])], nil))
    case "on": let v = ints(3); steps.append(.bytes([0x90 + ch(v[0]), b7(v[1]), b7(v[2])], nil))
    case "off": let v = ints(2); steps.append(.bytes([0x80 + ch(v[0]), b7(v[1]), 0], nil))
    case "wait": steps.append(.wait(max(0, ints(1)[0])))
    case "start": steps.append(.bytes([0xFA], nil))
    case "stop": steps.append(.bytes([0xFC], nil))
    case "cont": steps.append(.bytes([0xFB], nil))
    case "spp":  // in 16ths
        let v = ints(1)[0]
        steps.append(.bytes([0xF2, UInt8(v & 0x7F), UInt8((v >> 7) & 0x7F)], nil))
    case "clock":  // clock:BPM:MS: 24 ppq clocks for MS milliseconds, drift-free
        let v = ints(2)
        guard v[0] > 0 else { bad(t, "BPM must be over 0") }
        steps.append(.clock(v[0], v[1]))
    case "sx":
        let digits = String(t.dropFirst(3)).filter { !" ,\t".contains($0) }
        guard digits.count >= 4, digits.count % 2 == 0 else { bad(t, "want an even number of hex digits") }
        var b: [UInt8] = []
        var i = digits.startIndex
        while i < digits.endIndex {
            let j = digits.index(i, offsetBy: 2)
            guard let v = UInt8(digits[i..<j], radix: 16) else { bad(t, "\(digits[i..<j]) is not hex") }
            b.append(v); i = j
        }
        guard b.first == 0xF0, b.last == 0xF7 else { bad(t, "SysEx must start F0 and end F7") }
        guard b.count <= 1024 else { bad(t, "over 1024 bytes") }
        if b.dropFirst().dropLast().contains(where: { $0 > 0x7F }) {
            print("warning: \(t) has a byte over 7F inside; CoreMIDI may cut the message there")
        }
        steps.append(.bytes(b, nil))
    case "fade":
        let v = ints(2)
        guard (0...127).contains(v[0]) else { bad(t, "level \(v[0]) not 0..127") }
        guard (0...16383).contains(v[1]) else { bad(t, "bars \(v[1]) not 0..16383") }
        let s = seq
        steps.append(.bytes(linkMsg(0x10, [UInt8(v[0]), UInt8(v[1] & 0x7F), UInt8(v[1] >> 7)]),
                            "FADE level \(v[0]) bars \(v[1]) (seq \(s))"))
    case "hold":
        let v = ints(1)
        guard v[0] == 0 || v[0] == 1 else { bad(t, "want 0 or 1") }
        let s = seq
        steps.append(.bytes(linkMsg(0x11, [UInt8(v[0])]), "HOLD \(v[0] == 1 ? "on" : "off") (seq \(s))"))
    default: bad(t, "unknown")
    }
}

var client = MIDIClientRef(); MIDIClientCreate("midisend" as CFString, nil, nil, &client)
var out = MIDIPortRef(); MIDIOutputPortCreate(client, "out" as CFString, &out)
var dest: MIDIEndpointRef = 0
for i in 0..<MIDIGetNumberOfDestinations() {
    let d = MIDIGetDestination(i)
    var name: Unmanaged<CFString>?
    MIDIObjectGetStringProperty(d, kMIDIPropertyDisplayName, &name)
    let n = (name?.takeRetainedValue() as String?) ?? ""
    if n.contains(args[1]) { dest = d; print("dest: \(n)"); break }
}
guard dest != 0 else { print("no destination matching \(args[1])"); exit(1) }

// One whole message per packet (a SysEx included, F0 to F7), sent synchronously so tokens keep their order.
let listSize = 2048
let listBuf = UnsafeMutableRawPointer.allocate(byteCount: listSize, alignment: 8)
func send(_ bytes: [UInt8]) {
    let list = listBuf.bindMemory(to: MIDIPacketList.self, capacity: 1)
    let p = MIDIPacketListInit(list)
    _ = MIDIPacketListAdd(list, listSize, p, 0, bytes.count, bytes)   // fits: tokens cap a message at 1024 bytes
    let err = MIDISend(out, dest, list)
    if err != noErr { print("MIDISend error \(err)"); exit(1) }
}
for s in steps {
    switch s {
    case .bytes(let b, let label):
        send(b)
        if let label { print("\(label): \(hex(b))") } else if b.first == 0xF0 { print("sysex (\(b.count) bytes)") }
    case .wait(let ms): usleep(useconds_t(ms * 1000))
    case .clock(let bpm, let ms):
        let period = 60.0 / Double(bpm) / 24.0
        let t0 = Date().timeIntervalSince1970
        var n = 0
        while Double(n) * period * 1000 < Double(ms) {
            let due = t0 + Double(n) * period
            let now = Date().timeIntervalSince1970
            if due > now { usleep(useconds_t((due - now) * 1_000_000)) }
            send([0xF8]); n += 1
        }
    }
}
usleep(50_000)
print("sent")
