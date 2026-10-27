#!/usr/bin/env python3
"""Guided pedal captures through JACK (PipeWire-JACK or jackd).

The interface sends each test signal to two outputs at once: one feeds the pedal, the
other is cabled straight back into a line input (the loopback). Both inputs are recorded
together, so every take carries its own reference: the loopback gives the round-trip
latency and shows the level that actually left the interface.

    capture.py ports                                  # the JACK ports to pick from
    capture.py init SESSION --pedal-out P --loop-out P --pedal-in P --loop-in P [--note TEXT]
    capture.py init SESSION --from OTHER_SESSION      # same rig as an earlier session
    capture.py meter SESSION [--seconds 15]           # peak on the pedal input (guitar, hum)
    capture.py calibrate SESSION --target-dbfs G      # pedal bypassed: reference level
    capture.py plan PLAN                              # the plan's takes and total time
    capture.py run SESSION PLAN                       # walk through the plan (resumable)

SESSION is a folder under captures/, one per pedal and day (e.g.
captures/boss/tr-2/2026-10-26). Its rig.json holds the ports and the reference level.
The WAVs are git-ignored (too large for the repo); the JSON files and fits are committed.
Each take writes TAKE.wav (float32; channel 1 = pedal, channel 2 = loopback),
TAKE.json (knob settings, signal, level, latency, peaks, xruns) and played/TAKE.wav.

Needs the JACK client for Python (Arch: python-jack-client) plus numpy, scipy and
soundfile.
"""

import argparse
import datetime
import hashlib
import json
import pathlib
import subprocess
import sys
import threading

import numpy as np
import scipy.signal
import soundfile

import signals

CLIENT_NAME = "namx-capture"
EXPECTED_RATE = 48000
CLIP_DBFS = -0.1
SILENT_DBFS = -60.0
DEFAULT_REF_DBFS = -20.0
CALIBRATION_SECONDS = 2.0
QUIET_DBFS = -75.0  # the pedal input's peak, per 0.1 s, counted as silent (noise peaks ~-85)
QUIET_HOLD_S = 0.5
QUIET_WAIT_S = 30.0


def dbfs(x):
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    return 20 * np.log10(peak) if peak > 0 else -float("inf")


def import_jack():
    try:
        import jack
    except ImportError:
        sys.exit("The JACK client for Python is missing: sudo pacman -S python-jack-client")
    return jack


class Engine:
    """One JACK client for the whole command: two outputs, two inputs, play-and-record."""

    def __init__(self, rig=None):
        jack = import_jack()
        try:
            self.client = jack.Client(CLIENT_NAME, no_start_server=True)
        except jack.JackOpenError:
            sys.exit("No JACK server (is PipeWire running? is pipewire-jack installed?)")
        self.rate = self.client.samplerate
        self.outs = [self.client.outports.register(n) for n in ("pedal_out", "loop_out")]
        self.ins = [self.client.inports.register(n) for n in ("pedal_in", "loop_in")]
        self.played = np.zeros(0, np.float32)
        self.recorded = np.zeros((0, 2), np.float32)
        self.pos = 0
        self.active = False
        self.xruns = 0
        self.done = threading.Event()
        self.client.set_process_callback(self.process)
        self.client.set_xrun_callback(self.on_xrun)
        self.client.activate()
        if rig:
            self.connect(rig)

    def connect(self, rig):
        pairs = [(self.outs[0], rig["pedal_out"]), (self.outs[1], rig["loop_out"]),
                 (rig["pedal_in"], self.ins[0]), (rig["loop_in"], self.ins[1])]
        for source, destination in pairs:
            try:
                self.client.connect(source, destination)
            except Exception as error:
                self.close()
                sys.exit(f"Can't connect {source} -> {destination}: {error}\n"
                         "Check the port names with: capture.py ports")

    def close(self):
        self.client.deactivate()
        self.client.close()

    def on_xrun(self, _delay):
        if self.active:
            self.xruns += 1

    def process(self, frames):
        outs = [port.get_array() for port in self.outs]
        if not self.active:
            for buffer in outs:
                buffer.fill(0)
            return
        count = min(frames, len(self.played) - self.pos)
        span = slice(self.pos, self.pos + count)
        for buffer in outs:
            buffer[:count] = self.played[span]
            buffer[count:] = 0
        for channel, port in enumerate(self.ins):
            self.recorded[span, channel] = port.get_array()[:count]
        self.pos += count
        if self.pos >= len(self.played):
            self.active = False
            self.done.set()

    def start(self, played):
        self.played = played
        self.recorded = np.zeros((len(played), 2), np.float32)
        self.pos = 0
        self.xruns = 0
        self.done.clear()
        self.active = True

    def play_record(self, played):
        """Plays `played` on both outputs and returns the two inputs, sample-aligned."""
        self.start(played)
        if not self.done.wait(len(played) / self.rate + 10):
            self.active = False
            sys.exit("The JACK server stopped processing.")
        return self.recorded, self.xruns


def load_json(path):
    with open(path, encoding="utf-8") as file:
        return json.load(file)


def save_json(path, data):
    with open(path, "w", encoding="utf-8") as file:
        json.dump(data, file, indent=2)
        file.write("\n")


def load_rig(session):
    path = session / "rig.json"
    if not path.exists():
        sys.exit(f"{path} is missing: run 'capture.py init {session} ...' first")
    return load_json(path)


def tool_commit():
    here = pathlib.Path(__file__).resolve().parent
    try:
        commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=here, capture_output=True,
                                text=True, check=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--", "."], cwd=here,
                               capture_output=True, text=True, check=True).stdout.strip()
        return commit + ("-dirty" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def check_rate(engine):
    if engine.rate != EXPECTED_RATE:
        print(f"Warning: the JACK server runs at {engine.rate} Hz, not {EXPECTED_RATE} Hz.")


def find_latency(loop, marker_start, rate):
    """Round-trip latency in samples from the marker in the loopback, and how clearly it
    was found (normalized correlation, 1 = exact copy)."""
    marker = signals.marker(rate)
    search = loop[:marker_start + len(marker) + int(0.5 * rate)].astype(np.float64)
    correlation = scipy.signal.correlate(search, marker, mode="valid", method="fft")
    index = int(np.argmax(np.abs(correlation)))
    energy = np.sqrt(np.sum(marker ** 2) * np.sum(search[index:index + len(marker)] ** 2))
    quality = float(np.abs(correlation[index]) / energy) if energy > 0 else 0.0
    return index - marker_start, quality


def check_take(recorded, marker_start, body_start, spec, rate, bypass):
    """Measurements for the sidecar, and the problems worth redoing a take for. Peaks are
    over the signal and tail, after the marker, which always plays at the reference. A
    bypass take must fall silent once the signal ends: anything there is the pedal's own
    repeats or tail (a DD-8 left on once spoiled a reference take that way)."""
    expect_signal = spec["type"] != "silence"
    pedal_peak, loop_peak = dbfs(recorded[body_start:, 0]), dbfs(recorded[body_start:, 1])
    latency, quality = find_latency(recorded[:, 1], marker_start, rate)
    problems = []
    if pedal_peak >= CLIP_DBFS:
        problems.append("the pedal input clipped: lower the pedal's Level or the input gain")
    if loop_peak >= CLIP_DBFS:
        problems.append("the loopback input clipped")
    if dbfs(recorded[:, 1]) < SILENT_DBFS:  # the marker included: silence takes have one
        problems.append("the loopback is silent: check its cable and ports")
    elif quality < 0.5:
        problems.append(f"the marker wasn't found clearly in the loopback ({quality:.2f})")
    if expect_signal and pedal_peak < SILENT_DBFS:
        problems.append("the pedal channel is silent" + ("" if bypass else ": is it on?"))
    if bypass:
        start = body_start + latency
        quiet = [recorded[start + int(a * rate):start + int(b * rate), 0]
                 for a, b in signals.silent_windows(spec)]
        leak = max((dbfs(q) for q in quiet if len(q)), default=-float("inf"))
        if leak >= SILENT_DBFS:
            problems.append(f"sound where nothing was played ({leak:.0f} dBFS): is the pedal "
                            "really bypassed?")
    return {
        "peaks_dbfs": {"pedal": round(pedal_peak, 2), "loop": round(loop_peak, 2)},
        "latency_samples": latency,
        "latency_quality": round(quality, 3),
    }, problems


def record_take(engine, rig, session, name, spec, extra):
    played, marker_start, body_start = signals.build(spec, engine.rate, rig["ref_dbfs"])
    recorded, xruns = engine.play_record(played)
    measured, problems = check_take(recorded, marker_start, body_start, spec, engine.rate,
                                    extra.get("bypass", False))
    if xruns:
        problems.append(f"{xruns} xrun(s) during the take: redo it, or raise the quantum")
    (session / "played").mkdir(exist_ok=True)
    soundfile.write(session / f"{name}.wav", recorded, engine.rate, subtype="FLOAT")
    soundfile.write(session / "played" / f"{name}.wav", played, engine.rate, subtype="FLOAT")
    sidecar = {
        "take": name,
        **extra,
        "signal": spec,
        "rate": engine.rate,
        "ref_dbfs": rig["ref_dbfs"],
        "marker_start": marker_start,
        "signal_start": body_start,
        **measured,
        "xruns": xruns,
        "played_sha256": hashlib.sha256(played.tobytes()).hexdigest(),
        "recorded_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "tool_commit": tool_commit(),
        "rig": rig,
    }
    save_json(session / f"{name}.json", sidecar)
    print(f"  pedal peak {measured['peaks_dbfs']['pedal']:.1f} dBFS · loopback "
          f"{measured['peaks_dbfs']['loop']:.1f} dBFS · latency {measured['latency_samples']} "
          f"samples ({1000 * measured['latency_samples'] / engine.rate:.2f} ms) · xruns {xruns}")
    for problem in problems:
        print(f"  PROBLEM: {problem}")
    return problems


def wait_for_silence(engine):
    """Listens to the pedal input until it stays quiet for QUIET_HOLD_S: a delay with high
    Feedback keeps repeating after a take, and those repeats would land in the next one.
    Returns False if it is still sounding after QUIET_WAIT_S."""
    rate = engine.rate
    engine.start(np.zeros(int(QUIET_WAIT_S * rate), np.float32))
    quiet = 0
    while not engine.done.wait(0.1):
        now = engine.pos
        level = dbfs(engine.recorded[max(0, now - rate // 10):now, 0])
        quiet = quiet + 1 if level < QUIET_DBFS else 0
        print(f"\r  waiting for the pedal to fall silent: {level:6.1f} dBFS ", end="", flush=True)
        if quiet * 0.1 >= QUIET_HOLD_S:
            engine.active = False
            print("\r  the pedal is silent." + " " * 30)
            return True
    print()
    return False


def cmd_ports(_args):
    jack = import_jack()
    client = jack.Client(CLIENT_NAME + "-ports", no_start_server=True)
    print(f"JACK server at {client.samplerate} Hz, {client.blocksize} frames per period.")
    for title, outputs in (("Outputs (to the interface: --pedal-out, --loop-out)", False),
                           ("Inputs (from the interface: --pedal-in, --loop-in)", True)):
        print(f"\n{title}:")
        for port in client.get_ports(is_audio=True, is_physical=True, is_output=outputs,
                                     is_input=not outputs):
            print(f"  {port.name}")
    client.close()


def cmd_init(args):
    session = pathlib.Path(args.session).expanduser()
    path = session / "rig.json"
    if path.exists():
        sys.exit(f"{path} already exists")
    if args.from_session:
        rig = load_rig(pathlib.Path(args.from_session).expanduser())
    else:
        ports = [args.pedal_out, args.loop_out, args.pedal_in, args.loop_in]
        if None in ports:
            sys.exit("init needs --pedal-out, --loop-out, --pedal-in and --loop-in (or --from)")
        rig = {"pedal_out": args.pedal_out, "loop_out": args.loop_out,
               "pedal_in": args.pedal_in, "loop_in": args.loop_in,
               "ref_dbfs": DEFAULT_REF_DBFS, "calibration": None, "note": ""}
    if args.note is not None:
        rig["note"] = args.note
    session.mkdir(parents=True, exist_ok=True)
    save_json(path, rig)
    print(f"Wrote {path}")
    if not rig["calibration"]:
        print("Next: 'meter' with your guitar on the pedal input, then 'calibrate'.")


def cmd_meter(args):
    session = pathlib.Path(args.session).expanduser()
    rig = load_rig(session)
    engine = Engine(rig)
    check_rate(engine)
    print(f"Peak on the pedal input ({rig['pedal_in']}) for {args.seconds:g} s; play hard:")
    engine.start(np.zeros(int(args.seconds * engine.rate), np.float32))
    shown, hold = 0, -float("inf")
    while not engine.done.wait(0.25):
        now = engine.pos
        current = dbfs(engine.recorded[shown:now, 0])
        hold = max(hold, current)
        shown = now
        print(f"\r  now {current:7.1f} dBFS   hold {hold:7.1f} dBFS ", end="", flush=True)
    hold = max(hold, dbfs(engine.recorded[:, 0]))
    print(f"\r  hold {hold:.1f} dBFS" + " " * 30)
    engine.close()


def steady_peak(x, rate):
    """The peak of a steady sine, ignoring fades: the median of 50 ms peaks."""
    block = int(0.05 * rate)
    blocks = len(x) // block
    peaks = np.max(np.abs(x[:blocks * block].reshape(blocks, block)), axis=1)
    return 20 * np.log10(max(float(np.median(peaks)), 1e-12))


def calibration_take(engine, ref_dbfs):
    spec = {"type": "sine", "freq": 1000, "seconds": CALIBRATION_SECONDS, "tail": 0.2}
    played, _, body_start = signals.build(spec, engine.rate, ref_dbfs)
    recorded, _ = engine.play_record(played)
    body = slice(body_start + int(0.2 * engine.rate),
                 body_start + int((CALIBRATION_SECONDS - 0.2) * engine.rate))
    return steady_peak(recorded[body, 0], engine.rate), steady_peak(recorded[body, 1], engine.rate)


def cmd_calibrate(args):
    session = pathlib.Path(args.session).expanduser()
    rig = load_rig(session)
    engine = Engine(rig)
    check_rate(engine)
    print("The pedal must be OFF (bypassed) and the input gain where it stays for every take.")
    ref = rig["ref_dbfs"]
    for _ in range(3):
        pedal, loop = calibration_take(engine, ref)
        print(f"  sent {ref:.2f} dBFS -> pedal input {pedal:.2f} dBFS, loopback {loop:.2f} dBFS")
        if pedal < SILENT_DBFS:
            engine.close()
            sys.exit("Nothing reaches the pedal input: check the cables, ports and bypass.")
        if abs(pedal - args.target_dbfs) < 0.1:
            break
        ref += args.target_dbfs - pedal
        if ref > -1.0:
            engine.close()
            sys.exit(f"The target needs {ref:.1f} dBFS out: raise the input gain or check the "
                     "feed into the pedal (a reamp box's level, a pad).")
    else:
        engine.close()
        sys.exit("The level didn't settle: is something in the chain compressing?")
    engine.close()
    rig["ref_dbfs"] = round(ref, 2)
    rig["calibration"] = {
        "target_dbfs": args.target_dbfs,
        "pedal_dbfs": round(pedal, 2),
        "loop_dbfs": round(loop, 2),
        "at": datetime.datetime.now().isoformat(timespec="seconds"),
    }
    save_json(session / "rig.json", rig)
    print(f"Reference level {ref:.2f} dBFS saved in {session / 'rig.json'}")


def load_plan(path):
    plan = load_json(path)
    knobs = plan.get("knobs", [])
    names = set()
    for take in plan["takes"]:
        name = take["name"]
        if name in names:
            sys.exit(f"{path}: take {name!r} appears twice")
        names.add(name)
        try:
            signals.validate(take["signal"])
        except ValueError as error:
            sys.exit(f"{path}: take {name!r}: {error}")
        if not take.get("bypass"):
            settings = take.get("settings", {})
            if set(settings) != set(knobs):
                sys.exit(f"{path}: take {name!r} must set exactly these knobs: {', '.join(knobs)}")
    return plan


def settings_text(take, previous, knobs):
    if take.get("bypass"):
        return "pedal OFF (bypassed)"
    settings = take["settings"]
    old = {} if not previous or previous.get("bypass") else previous["settings"]
    parts = [f"{knob} {settings[knob]}" + (" (changed)" if old and old.get(knob) != settings[knob]
                                           else "") for knob in knobs]
    switched_on = previous is None or previous.get("bypass")
    return "pedal ON · " + " · ".join(parts) + (" (switch it on)" if switched_on else "")


def cmd_plan(args):
    plan = load_plan(args.plan)
    seconds = 0.0
    previous = None
    for index, take in enumerate(plan["takes"], 1):
        seconds += signals.total_seconds(take["signal"])
        print(f"{index:3}. {take['name']:<20} {signals.describe(take['signal']):<36} "
              f"{settings_text(take, previous, plan['knobs'])}")
        previous = take
    print(f"\n{len(plan['takes'])} takes, {seconds / 60:.1f} min of audio.")


def ask(prompt, choices):
    while True:
        answer = input(prompt).strip().lower()
        if answer in choices:
            return answer


def cmd_run(args):
    session = pathlib.Path(args.session).expanduser()
    rig = load_rig(session)
    plan = load_plan(args.plan)
    if not rig["calibration"]:
        print("Warning: this rig isn't calibrated; levels use the default reference.")
    engine = Engine(rig)
    check_rate(engine)
    takes = plan["takes"]
    previous = None
    try:
        for index, take in enumerate(takes, 1):
            name = take["name"]
            if (session / f"{name}.wav").exists():
                previous = take
                continue
            print(f"\n[{index}/{len(takes)}] {name}: {signals.describe(take['signal'])}")
            print(f"  {settings_text(take, previous, plan['knobs'])}")
            if take.get("note"):
                print(f"  {take['note']}")
            if ask("  Enter = record, s = skip, q = quit: ", {"", "s", "q"}) == "q":
                break
            # A skipped take stays unrecorded; the next run offers it again.
            while True:
                while not wait_for_silence(engine):
                    answer = ask("  Still sounding: turn Feedback down. Enter = wait again, "
                                 "r = record anyway: ", {"", "r"})
                    if answer == "r":
                        break
                extra = {"pedal": plan["pedal"], "bypass": bool(take.get("bypass")),
                         "settings": take.get("settings", {}), "note": take.get("note", "")}
                problems = record_take(engine, rig, session, name, take["signal"], extra)
                prompt = "  Enter = keep, r = redo: " if problems else "  Enter = next, r = redo: "
                if ask(prompt, {"", "r"}) == "":
                    break
            previous = take
        else:
            print(f"\nAll {len(takes)} takes are in {session}.")
    except KeyboardInterrupt:
        print("\nStopped; the takes recorded so far are kept.")
    finally:
        engine.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)

    ports = commands.add_parser("ports", help="list the interface's JACK ports")
    ports.set_defaults(func=cmd_ports)

    init = commands.add_parser("init", help="create a session folder and its rig.json")
    init.add_argument("session")
    init.add_argument("--from", dest="from_session", help="copy the rig of an earlier session")
    for port in ("pedal-out", "loop-out", "pedal-in", "loop-in"):
        init.add_argument(f"--{port}")
    init.add_argument("--note", help="free text: input gain, pad, power supply, cables…")
    init.set_defaults(func=cmd_init)

    meter = commands.add_parser("meter", help="show the peak on the pedal input")
    meter.add_argument("session")
    meter.add_argument("--seconds", type=float, default=15.0)
    meter.set_defaults(func=cmd_meter)

    calibrate = commands.add_parser("calibrate", help="set the reference level (pedal bypassed)")
    calibrate.add_argument("session")
    calibrate.add_argument("--target-dbfs", type=float, required=True,
                           help="the guitar's peak on the pedal input, from 'meter'")
    calibrate.set_defaults(func=cmd_calibrate)

    plan = commands.add_parser("plan", help="list a plan's takes")
    plan.add_argument("plan")
    plan.set_defaults(func=cmd_plan)

    run = commands.add_parser("run", help="record a plan's takes")
    run.add_argument("session")
    run.add_argument("plan")
    run.set_defaults(func=cmd_run)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
