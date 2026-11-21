// midisend <port-substring> token...
// tokens: cc:CH:NUM:VAL  on:CH:NOTE:VEL  off:CH:NOTE  wait:MS   (CH is 1..16)
import CoreMIDI
import Foundation

let args = CommandLine.arguments
guard args.count >= 2 else { print("usage: midisend <port> tokens..."); exit(2) }
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

func send(_ bytes: [UInt8]) {
    var list = MIDIPacketList()
    let p = MIDIPacketListInit(&list)
    _ = MIDIPacketListAdd(&list, MemoryLayout<MIDIPacketList>.size, p, 0, bytes.count, bytes)
    MIDISend(out, dest, &list)
}
for t in args.dropFirst(2) {
    let f = t.split(separator: ":").map(String.init)
    let v = f.dropFirst().compactMap { Int($0) }
    switch f[0] {
    case "cc": send([UInt8(0xB0 + v[0] - 1), UInt8(v[1]), UInt8(v[2])])
    case "on": send([UInt8(0x90 + v[0] - 1), UInt8(v[1]), UInt8(v[2])])
    case "off": send([UInt8(0x80 + v[0] - 1), UInt8(v[1]), 0])
    case "wait": usleep(useconds_t(v[0] * 1000))
    case "start": send([0xFA])
    case "stop": send([0xFC])
    case "cont": send([0xFB])
    case "spp": send([0xF2, UInt8(v[0] & 0x7F), UInt8((v[0] >> 7) & 0x7F)])  // in 16ths
    case "clock":  // clock:BPM:MS — 24 ppq clocks for MS milliseconds, drift-free
        let period = 60.0 / Double(v[0]) / 24.0
        let t0 = Date().timeIntervalSince1970
        var n = 0
        while Double(n) * period * 1000 < Double(v[1]) {
            let due = t0 + Double(n) * period
            let now = Date().timeIntervalSince1970
            if due > now { usleep(useconds_t((due - now) * 1_000_000)) }
            send([0xF8]); n += 1
        }
    default: print("bad token \(t)"); exit(2)
    }
}
usleep(50_000)
print("sent")
