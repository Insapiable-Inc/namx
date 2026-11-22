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
    capture.py di SESSION OUT.wav [--seconds 60]      # record the guitar (pedal bypassed)
    capture.py plan PLAN                              # the plan's takes and total time
    capture.py run SESSION PLAN                       # walk through the plan (resumable)

SESSION is a folder under captures/, one per pedal and day (e.g.
captures/boss/tr-2/2026-10-26). Its rig.json holds the ports and the reference level.
The WAVs are git-ignored (too large for the repo); the JSON files and fits are committed.
Each take writes TAKE.wav (float32; channel 1 = pedal, channel 2 = loopback),
TAKE.json (knob settings, signal, level, latency, peaks, xruns) and played/TAKE.wav; run
appends its console transcript (answers and problems included) to SESSION/run.log.

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
PLUCK_CUT_DB = -60.0  # how far a pluck must have decayed where the signal stops it
DEFAULT_REF_DBFS = -20.0
CALIBRATION_SECONDS = 2.0
QUIET_DBFS = -75.0  # the pedal input's peak, per 0.1 s, counted as silent (noise peaks ~-85);
# a plan's "quiet_dbfs" overrides it for a pedal whose own hiss is louder (the CE-5's BBD),
# a take's for a setting that raises the hiss (the GE-7 with every slider up)
QUIET_HOLD_S = 0.5
QUIET_WAIT_S = 30.0
SAME_AB = 0.99  # Outputs A and B this correlated are the same (a plan's "outputs_differ")
PROBE_REACH_DB = -60.0  # a plan's loop_probe under this at the pedal's output hasn't reached it (probe_reach)
OCTAVES_HZ = (125, 250, 500, 1000, 2000, 4000, 8000)  # a take's "expect" checks
REPOSITORY = pathlib.Path(__file__).resolve().parents[2]


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
    """One JACK client for the whole command: two outputs, two or three inputs (the pedal,
    the loopback, and a stereo pedal's second output when the rig has `pedal_in_b`),
    play-and-record. Recorded channels: pedal, loopback[, pedal B]."""

    def __init__(self, rig=None, monitor=None):
        jack = import_jack()
        try:
            self.client = jack.Client(CLIENT_NAME, no_start_server=True)
        except jack.JackOpenError:
            sys.exit("No JACK server (is PipeWire running? is pipewire-jack installed?)")
        self.rate = self.client.samplerate
        self.outs = [self.client.outports.register(n) for n in ("pedal_out", "loop_out")]
        # Optional: two ports to the monitoring (the interface's headphones), for a click.
        self.monitor_ports = [self.client.outports.register(n) for n in ("monitor_l", "monitor_r")] if monitor else []
        self.monitor_played = np.zeros(0, np.float32)
        names = ["pedal_in", "loop_in"] + (["pedal_in_b"] if rig and rig.get("pedal_in_b") else [])
        self.ins = [self.client.inports.register(n) for n in names]
        self.played = np.zeros(0, np.float32)
        self.pedal_played = self.played
        self.recorded = np.zeros((0, len(self.ins)), np.float32)
        self.pos = 0
        self.active = False
        self.xruns = 0
        self.done = threading.Event()
        self.client.set_process_callback(self.process)
        self.client.set_xrun_callback(self.on_xrun)
        self.client.activate()
        if rig:
            self.connect(rig)
        for port, destination in zip(self.monitor_ports, monitor or []):
            try:
                self.client.connect(port, destination)
            except Exception as error:
                print(f"No click: can't connect to {destination}: {error}")

    def connect(self, rig):
        pairs = [(self.outs[0], rig["pedal_out"]), (self.outs[1], rig["loop_out"]),
                 (rig["pedal_in"], self.ins[0]), (rig["loop_in"], self.ins[1])]
        if len(self.ins) > 2:
            pairs.append((rig["pedal_in_b"], self.ins[2]))
        for source, destination in pairs:
            try:
                self.client.connect(source, destination)
            except Exception as error:
                # Names first: our own ports are gone (and unprintable) once the client closes.
                names = [p if isinstance(p, str) else p.name for p in (source, destination)]
                self.close()
                sys.exit(f"Can't connect {names[0]} -> {names[1]}: {error}\n"
                         "Is the card on its Pro Audio profile (tools/capture/README.md, Rig)? "
                         "Check the port names with: capture.py ports")

    def close(self):
        self.client.deactivate()
        self.client.close()

    def on_xrun(self, _delay):
        if self.active:
            self.xruns += 1

    def process(self, frames):
        outs = [port.get_array() for port in self.outs]
        monitors = [port.get_array() for port in self.monitor_ports]
        if not self.active:
            for buffer in outs + monitors:
                buffer.fill(0)
            return
        count = min(frames, len(self.played) - self.pos)
        span = slice(self.pos, self.pos + count)
        for buffer, source in zip(outs, (self.pedal_played, self.played)):
            buffer[:count] = source[span]
            buffer[count:] = 0
        for buffer in monitors:
            clicks = self.monitor_played[span] if len(self.monitor_played) else np.zeros(count, np.float32)
            buffer[:len(clicks)] = clicks
            buffer[len(clicks):] = 0
        for channel, port in enumerate(self.ins):
            self.recorded[span, channel] = port.get_array()[:count]
        self.pos += count
        if self.pos >= len(self.played):
            self.active = False
            self.done.set()

    def start(self, played, pedal_played=None, monitor_played=None):
        self.played = played
        self.pedal_played = played if pedal_played is None else pedal_played
        self.monitor_played = np.zeros(0, np.float32) if monitor_played is None else monitor_played
        self.recorded = np.zeros((len(played), len(self.ins)), np.float32)
        self.pos = 0
        self.xruns = 0
        self.done.clear()
        self.active = True

    def play_record(self, played, pedal_played=None):
        """Plays `played` on both outputs (or `pedal_played` on the pedal's) and returns the
        inputs, sample-aligned."""
        self.start(played, pedal_played)
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


def find_slips(loop, played, latency, rate, window=0.5, search=300):
    """The loopback's offset from where the marker put it, over the whole take: a list of
    (seconds into the take, samples). PipeWire can drop or repeat a block without reporting
    an xrun; both recorded channels then slip together, mid-take (47 samples, twice on
    2026-10-31). A window counts only if its best match clearly beats every other (noise,
    a sweep's upper octaves), not a steady tone or a pluck, which match a period away
    just as well, nor low content, whose broad peak the interface's low end bends (a
    sweep's start read 9 samples off)."""
    n, offsets = int(window * rate), []
    for start in range(0, len(played) - n, n):
        a = played[start:start + n].astype(np.float64)
        lo = start + latency - search
        if dbfs(a) < -60 or lo < 0 or lo + n + 2 * search > len(loop):
            continue
        auto = scipy.signal.correlate(a, a, mode="full", method="fft")[n - 1 - search:n + search]
        if np.count_nonzero(auto > 0.5 * auto[search]) > 9:
            continue
        b = loop[lo:lo + n + 2 * search].astype(np.float64)
        correlation = scipy.signal.correlate(b, a, mode="valid", method="fft")
        peaks, _ = scipy.signal.find_peaks(correlation)  # positive: a half-period match isn't one
        if len(peaks) < 2:
            continue
        best, runner_up = np.sort(correlation[peaks])[-1:-3:-1]
        index = int(peaks[np.argmax(correlation[peaks])])
        energy = np.sqrt(auto[search] * np.sum(b[index:index + n] ** 2))
        if runner_up < 0.9 * best and energy > 0 and best / energy > 0.8:
            offsets.append((start / rate, index - search))
    return offsets


def octave_gains(recorded, played, latency, start, rate):
    """Each pedal output's power gain per octave band over the signal: recorded / played, dB."""
    n = min(len(played), len(recorded) - latency)
    freqs, sent = scipy.signal.welch(played[start:n], rate, nperseg=8192)
    bands = [(freqs >= hz / np.sqrt(2)) & (freqs < hz * np.sqrt(2)) for hz in OCTAVES_HZ]
    gains = []
    for channel in [0] + ([2] if recorded.shape[1] > 2 else []):
        _, got = scipy.signal.welch(recorded[latency + start:latency + n, channel], rate,
                                    nperseg=8192)
        gains.append(np.array([10 * np.log10(got[b].sum() / sent[b].sum()) for b in bands]))
    return gains


def take_gains(take):
    """octave_gains of an earlier take: SESSION/TAKE, from the repository root."""
    path = REPOSITORY / take
    meta = load_json(path.parent / f"{path.name}.json")
    recorded, rate = soundfile.read(path.parent / f"{path.name}.wav", dtype="float32")
    played, _ = soundfile.read(path.parent / "played" / f"{path.name}.wav", dtype="float32")
    return octave_gains(recorded, played, meta["latency_samples"], meta["signal_start"], rate)


def check_expect(expect, recorded, played, latency, start, rate):
    """A plan take's "expect": what its settings must sound like, so a control set wrong (a
    toggle on the wrong side) is caught at the take, not weeks later in the analysis:
      "like": [TAKE, DB]     every octave band within DB of an earlier take's (SESSION/TAKE
                             from the repository root; its bands 40 dB under its loudest skipped)
      "below": [HZ, RE, DB]  the octave at HZ at least DB under the one at RE (a rotor alone:
                             the other one's band empty)
      "same": true           Outputs A and B the same (the RT-2's drum alone)
    and "hint": what to do when one fails."""
    gains = octave_gains(recorded, played, latency, start, rate)
    failed = []
    if "like" in expect:
        take, tolerance = expect["like"]
        for output, got, ref in zip("AB", gains, take_gains(take)):
            off = [f"{hz} Hz {g - r:+.1f} dB" for hz, g, r in zip(OCTAVES_HZ, got, ref)
                   if r > ref.max() - 40 and abs(g - r) > tolerance]
            if off:
                failed.append(f"Output {output} isn't like {pathlib.Path(take).name} "
                              f"({', '.join(off)})")
    if "below" in expect:
        hz, re_hz, db = expect["below"]
        i, j = OCTAVES_HZ.index(hz), OCTAVES_HZ.index(re_hz)
        for output, got in zip("AB", gains):
            under = got[j] - got[i]
            if under < db:
                failed.append(f"Output {output}'s {hz} Hz octave is {abs(under):.0f} dB "
                              f"{'under' if under >= 0 else 'over'} its {re_hz} Hz one, not "
                              f"{db}+ under")
    if expect.get("same") and recorded.shape[1] > 2:
        n = min(len(played), len(recorded) - latency)
        same = float(np.corrcoef(recorded[latency + start:latency + n, 0],
                                 recorded[latency + start:latency + n, 2])[0, 1])
        if same <= SAME_AB:
            failed.append(f"Outputs A and B differ (correlation {same:.3f})")
    return [f"{text}: {expect.get('hint', 'check the settings')}" for text in failed]


def probe_reach(recorded, played, loop_played, latency, body_start, rate):
    """How much of a plan's loop_probe reached the pedal's output: the strongest it gets, dB
    re what was played, over the 0.1 s windows where the pedal's own signal is silent (its
    content can't pass for the probe there). Through an open gate that is the chain's gain,
    through a closed one some 35 dB less; a cable in the wrong jack leaves 80 dB or more."""
    n = int(0.1 * rate)
    window = np.hanning(n)
    ref = np.abs(np.fft.rfft(loop_played[body_start:body_start + n] * window))
    bins = np.argsort(ref)[-3:]
    best = -np.inf
    for start in range(body_start, len(played) - n, n):
        at = start + latency
        if np.max(np.abs(played[start:start + n])) > 0 or at < 0 or at + n > len(recorded):
            continue
        got = np.abs(np.fft.rfft(recorded[at:at + n, 0] * window))
        best = max(best, 20 * np.log10(np.mean(got[bins]) / np.mean(ref[bins]) + 1e-12))
    return float(best)


def check_take(recorded, marker_start, body_start, spec, rate, bypass, played=None,
               outputs_differ=None, expect=None, loop_played=None):
    """Measurements for the sidecar, and the problems worth redoing a take for. Peaks are
    over the signal and tail, after the marker, which always plays at the reference. A
    bypass take must fall silent once the signal ends: anything there is the pedal's own
    repeats or tail (a DD-8 left on once spoiled a reference take that way). A stereo
    pedal's plan can say its outputs must differ with the effect on (`outputs_differ`: what
    it means when they don't — the RT-2's horn off, a toggle on the wrong side, twice), and a
    take what it must sound like (`expect`: check_expect). With a plan's `loop_probe` the
    loopback output played `loop_played`, not the pedal's signal, and the pedal channel is
    never silent."""
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
    elif played is not None:
        offsets = find_slips(recorded[:, 1], played if loop_played is None else loop_played,
                             latency, rate)
        slips = [i for i, (_, k) in enumerate(offsets) if abs(k) > 2]
        if slips:
            i = slips[0]
            after = f"{offsets[i - 1][0]:.1f}" if i else "0"
            problems.append(f"the recording slipped {offsets[i][1]:+d} samples between {after} and "
                            f"{offsets[i][0]:.1f} s into the take (no xrun reported): redo it")
    if expect_signal and pedal_peak < SILENT_DBFS:
        problems.append("the pedal channel is silent" + ("" if bypass else ": is it on?"))
    peaks = {"pedal": round(pedal_peak, 2), "loop": round(loop_peak, 2)}
    if recorded.shape[1] > 2:
        b_peak = dbfs(recorded[body_start:, 2])
        peaks["pedal_b"] = round(b_peak, 2)
        if b_peak >= CLIP_DBFS:
            problems.append("the pedal's B input clipped: lower the pedal's Level or the input gain")
        if expect_signal and b_peak < SILENT_DBFS:
            problems.append("the pedal's B channel is silent: check Output B's cable")
        elif outputs_differ and not bypass and pedal_peak >= SILENT_DBFS:
            same = float(np.corrcoef(recorded[body_start:, 0], recorded[body_start:, 2])[0, 1])
            if same > SAME_AB:
                problems.append(f"Outputs A and B are the same (correlation {same:.3f}): "
                                f"{outputs_differ}")
    if expect and played is not None and not bypass and pedal_peak >= SILENT_DBFS:
        problems += check_expect(expect, recorded, played, latency, body_start, rate)
    if loop_played is not None and played is not None:
        reach = probe_reach(recorded, played, loop_played, latency, body_start, rate)
        peaks["probe_reach_db"] = round(reach, 1)
        if reach < PROBE_REACH_DB:
            problems.insert(0, f"THE PROBE ISN'T AT THE PEDAL'S OUTPUT ({reach:.0f} dB re what was played; "
                            "about -10 to -45 expected). The cable from the interface's loopback output "
                            "must be in the pedal's RETURN: not SEND, and not back in the interface. "
                            "Fix it and redo this take; nothing recorded without the probe is of use")
    if bypass and loop_played is None:
        start = body_start + latency
        quiet = [recorded[start + int(a * rate):start + int(b * rate), 0]
                 for a, b in signals.silent_windows(spec)]
        leak = max((dbfs(q) for q in quiet if len(q)), default=-float("inf"))
        if leak >= SILENT_DBFS:
            problems.append(f"sound where nothing was played ({leak:.0f} dBFS): is the pedal "
                            "really bypassed?")
    return {
        "peaks_dbfs": peaks,
        "latency_samples": latency,
        "latency_quality": round(quality, 3),
    }, problems


def record_take(engine, rig, session, name, spec, extra, outputs_differ=None, expect=None):
    played, marker_start, body_start = signals.build(spec, engine.rate, rig["ref_dbfs"])
    pedal_played = None
    if extra.get("marker_to_pedal", True) is False:
        # The marker on the loopback only: a long-tail pedal (a reverb's Shimmer) would
        # otherwise carry the marker's own tail into the signal.
        pedal_played = played.copy()
        pedal_played[:body_start] = 0.0
    loop_played = None
    if extra.get("loop_probe"):
        # The loopback output as a second feed (cmd_run): the marker, then a steady tone to
        # the take's end, starting 0.1 s before the signal.
        probe = extra["loop_probe"]
        first = body_start - int(0.1 * engine.rate)
        loop_played = np.zeros_like(played)
        loop_played[:first] = played[:first]
        tone = signals.body({"type": "sine", "freq": probe["freq"],
                             "seconds": (len(played) - first) / engine.rate}, engine.rate)
        gain = 10 ** ((rig["ref_dbfs"] + probe["level_db"]) / 20)
        loop_played[first:first + len(tone)] = tone * gain
    recorded, xruns = engine.play_record(played if loop_played is None else loop_played,
                                         played if loop_played is not None and pedal_played is None
                                         else pedal_played)
    measured, problems = check_take(recorded, marker_start, body_start, spec, engine.rate,
                                    extra.get("bypass", False), played, outputs_differ, expect,
                                    loop_played)
    if xruns:
        problems.append(f"{xruns} xrun(s) during the take: redo it, or raise the quantum")
    (session / "played").mkdir(exist_ok=True)
    soundfile.write(session / f"{name}.wav", recorded, engine.rate, subtype="FLOAT")
    soundfile.write(session / "played" / f"{name}.wav", played, engine.rate, subtype="FLOAT")
    if loop_played is not None:
        soundfile.write(session / "played" / f"{name}.loop.wav", loop_played, engine.rate,
                        subtype="FLOAT")
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
    b_peak = measured["peaks_dbfs"].get("pedal_b")
    b_text = "" if b_peak is None else f" · B {b_peak:.1f} dBFS"
    if "probe_reach_db" in measured["peaks_dbfs"]:
        b_text += f" · the probe at the output {measured['peaks_dbfs']['probe_reach_db']:.0f} dB re played"
    print(f"  pedal peak {measured['peaks_dbfs']['pedal']:.1f} dBFS{b_text} · loopback "
          f"{measured['peaks_dbfs']['loop']:.1f} dBFS · latency {measured['latency_samples']} "
          f"samples ({1000 * measured['latency_samples'] / engine.rate:.2f} ms) · xruns {xruns}")
    for problem in problems:
        print(f"  PROBLEM: {problem}")
    return problems


def wait_for_silence(engine, quiet_dbfs):
    """Listens to the pedal input until it stays quiet for QUIET_HOLD_S: a delay with high
    Feedback keeps repeating after a take, and those repeats would land in the next one.
    "Quiet" is a peak under quiet_dbfs. Returns whether it fell silent, and how long that
    took (s)."""
    rate = engine.rate
    engine.start(np.zeros(int(QUIET_WAIT_S * rate), np.float32))
    quiet = 0
    while not engine.done.wait(0.1):
        now = engine.pos
        level = dbfs(engine.recorded[max(0, now - rate // 10):now, 0])
        quiet = quiet + 1 if level < quiet_dbfs else 0
        print(f"\r  waiting for the pedal to fall silent: {level:6.1f} dBFS ", end="", flush=True)
        if quiet * 0.1 >= QUIET_HOLD_S:
            engine.active = False
            print("\r  the pedal is silent." + " " * 30)
            return True, now / rate
    print()
    return False, QUIET_WAIT_S


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
    if args.pedal_in_b:
        rig["pedal_in_b"] = args.pedal_in_b  # a stereo pedal's Output B: recorded as channel 3
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


def cmd_di(args):
    """Records the guitar as it reaches the pedal (guitar → bypassed pedal → the pedal input),
    for "file" signals: scaled so 1.0 is the rig's reference (the guitar's peak), so a plan
    plays it back into a pedal exactly as hard as it was played."""
    session = pathlib.Path(args.session).expanduser()
    rig = load_rig(session)
    if not rig["calibration"]:
        sys.exit("this rig isn't calibrated: 'meter' and 'calibrate' first")
    out = pathlib.Path(args.out)
    if out.exists():
        sys.exit(f"{out} already exists")
    monitor = None
    if args.click:
        # The interface's first two playback channels: the headphones (never one of the inputs).
        device = rig["pedal_out"].split(":")[0]
        monitor = rig.get("monitor", [f"{device}:playback_AUX0", f"{device}:playback_AUX1"])
    engine = Engine(rig, monitor)
    check_rate(engine)
    input("Guitar into the bypassed pedal; Enter starts the recording, then play: ")
    length = int(args.seconds * engine.rate)
    engine.start(np.zeros(length, np.float32), monitor_played=click_track(args.click, length, engine.rate) if args.click else None)
    shown = 0
    while not engine.done.wait(0.25):
        now = engine.pos
        print(f"\r  {now / engine.rate:5.1f} / {args.seconds:g} s   "
              f"peak {dbfs(engine.recorded[shown:now, 0]):7.1f} dBFS ", end="", flush=True)
        shown = now
    engine.close()
    target = rig["calibration"]["target_dbfs"]
    guitar = engine.recorded[:, 0] / 10 ** (target / 20)
    print(f"\r  peak {dbfs(guitar):+.1f} dB re the guitar's reference ({target:g} dBFS)"
          + " " * 20)
    out.parent.mkdir(parents=True, exist_ok=True)
    soundfile.write(out, guitar, engine.rate, subtype="FLOAT")
    save_json(out.with_suffix(".json"), {
        "seconds": args.seconds, "rate": engine.rate, "peak_db_re_reference": round(dbfs(guitar), 2),
        "reference_dbfs": target, "note": args.note or "",
        "recorded_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "tool_commit": tool_commit(), "rig": rig})
    print(f"Wrote {out} (and its .json); play it with {{\"type\": \"file\", \"path\": ...}}")


def click_track(bpm, length, rate):
    """A metronome for the headphones: a short 1 kHz tick per beat, 1.5 kHz and louder on the
    first of every four."""
    out = np.zeros(length, np.float32)
    tick = int(0.02 * rate)
    t = np.arange(tick) / rate
    shape = np.exp(-t / 0.004)
    beat = 0
    for start in np.arange(0, length, 60.0 / bpm * rate).astype(int):
        accent = beat % 4 == 0
        n = min(tick, length - start)
        out[start:start + n] = (0.25 if accent else 0.12) * np.sin(2 * np.pi * (1500 if accent else 1000) * t[:n]) * shape[:n]
        beat += 1
    return out


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
    if "loop_probe" in plan and plan["loop_probe"].keys() != {"freq", "level_db"}:
        sys.exit(f"{path}: loop_probe needs freq (one or a list) and level_db")
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
        # Here, not in signals.validate: an old session's signal must still regenerate as played.
        cut = signals.pluck_cut_db(take["signal"])
        if cut is not None and cut > PLUCK_CUT_DB:
            sys.exit(f"{path}: take {name!r}: a pluck is cut at {cut:.0f} dB (rv-6-shimmer.json's "
                     f"last pluck, cut at -11 dB, rang in the reverb); let it decay to "
                     f"{PLUCK_CUT_DB:.0f} dB: more seconds or period, or a shorter decay")
        if not take.get("bypass"):
            settings = take.get("settings", {})
            if set(settings) != set(knobs):
                sys.exit(f"{path}: take {name!r} must set exactly these knobs: {', '.join(knobs)}")
        expect = take.get("expect", {})
        if "like" in expect and not (REPOSITORY / f"{expect['like'][0]}.wav").exists():
            sys.exit(f"{path}: take {name!r}: its reference {expect['like'][0]} isn't on this machine")
        if "below" in expect and not set(expect["below"][:2]) <= set(OCTAVES_HZ):
            sys.exit(f"{path}: take {name!r}: 'below' takes octave bands: {OCTAVES_HZ}")
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
        try:
            seconds += signals.total_seconds(take["signal"])
        except ValueError as error:
            sys.exit(f"{take['name']}: {error}")
        print(f"{index:3}. {take['name']:<20} {signals.describe(take['signal']):<36} "
              f"{settings_text(take, previous, plan['knobs'])}")
        previous = take
    print(f"\n{len(plan['takes'])} takes, {seconds / 60:.1f} min of audio.")


class RunLog:
    """Copies the console output to a file as well, so a session's transcript stays with it.
    A line rewritten in place with \\r (the silence meter) keeps only its last state."""

    def __init__(self, path, console):
        self.file = open(path, "a", encoding="utf-8")
        self.console = console
        self.line = ""

    def write(self, text):
        self.console.write(text)
        self.line += text
        while "\n" in self.line:
            line, self.line = self.line.split("\n", 1)
            self.file.write(line.rsplit("\r", 1)[-1].rstrip() + "\n")
        self.file.flush()
        return len(text)

    def typed(self, answer):
        """What was typed at a prompt: the terminal echoes it, the file needs it."""
        self.line += (answer or "(Enter)") + "\n"
        self.write("")

    def flush(self):
        self.console.flush()

    def close(self):
        if self.line:
            self.write("\n")
        self.file.close()


def ask(prompt, choices):
    while True:
        answer = input(prompt).strip().lower()
        if isinstance(sys.stdout, RunLog):
            sys.stdout.typed(answer)
        if answer in choices:
            return answer


def cmd_run(args):
    session = pathlib.Path(args.session).expanduser()
    rig = load_rig(session)
    plan = load_plan(args.plan)
    sys.stdout = RunLog(session / "run.log", sys.stdout)
    print(f"\n=== {datetime.datetime.now().isoformat(timespec='seconds')} · run {args.plan} "
          f"· tool {tool_commit()}")
    if not rig["calibration"]:
        print("Warning: this rig isn't calibrated; levels use the default reference.")
    if plan.get("loop_probe"):
        # A second feed for a pedal with two inputs (the NS-2: INPUT to its detector, RETURN
        # to its gain): the loopback output plays the marker and then a steady tone, and goes
        # to the pedal instead of back to the interface. The latency is then read from the
        # marker in the pedal's own output: the "loopback" channel records that input too.
        rig = dict(rig, loop_in=rig["pedal_in"])
        print("This plan plays a probe on the loopback output: that cable goes to the pedal, "
              "not back to the interface.")
    engine = Engine(rig)
    check_rate(engine)
    takes = plan["takes"]
    for take in takes:  # a take that would clip stops the run now, not half-way through it
        try:
            signals.build(take["signal"], engine.rate, rig["ref_dbfs"])
        except ValueError as error:
            engine.close()
            sys.exit(f"{take['name']}: {error}")
    previous = None
    try:
        for index, take in enumerate(takes, 1):
            name = take["name"]
            if (session / f"{name}.wav").exists():
                previous = take
                continue
            what = signals.describe(take["signal"])  # a long sequence's listing would bury the settings
            print(f"\n[{index}/{len(takes)}] {name}: {what if len(what) <= 240 else what[:237] + '…'}")
            print(f"  {settings_text(take, previous, plan['knobs'])}")
            if take.get("note"):
                print(f"  {take['note']}")
            answer = None
            while True:
                answer = ask("  Enter = record, s = skip, q = quit: ", {"", "s", "q"})
                if answer != "":
                    break
                # Checked right before recording, so nothing done after the check can leak
                # in; and a recording only starts if the pedal was already quiet at Enter,
                # never when it falls silent while knobs are still being set.
                quiet, waited = wait_for_silence(
                    engine, take.get("quiet_dbfs", plan.get("quiet_dbfs", QUIET_DBFS)))
                if not quiet:
                    print("  Still sounding after 30 s: turn Feedback down, or switch the "
                          "pedal off and on; then set the knobs and press Enter.")
                    continue
                if waited > 1.0:
                    print(f"  Silent now (after {waited:.0f} s): set the knobs, then Enter.")
                    continue
                extra = {"pedal": plan["pedal"], "bypass": bool(take.get("bypass")),
                         "settings": take.get("settings", {}), "note": take.get("note", "")}
                if not take.get("marker_to_pedal", plan.get("marker_to_pedal", True)):
                    extra["marker_to_pedal"] = False
                if plan.get("loop_probe"):
                    extra["loop_probe"] = plan["loop_probe"]
                problems = record_take(engine, rig, session, name, take["signal"], extra,
                                       take.get("outputs_differ", plan.get("outputs_differ")),
                                       take.get("expect"))
                prompt = "  Enter = keep, r = redo: " if problems else "  Enter = next, r = redo: "
                if ask(prompt, {"", "r"}) == "":
                    break
            if answer == "q":
                break
            if answer == "s":
                continue  # a skipped take stays unrecorded; the next run offers it again
            previous = take
        else:
            print(f"\nAll {len(takes)} takes are in {session}.")
    except KeyboardInterrupt:
        print("\nStopped; the takes recorded so far are kept.")
    finally:
        engine.close()
        sys.stdout.close()
        sys.stdout = sys.stdout.console


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
    init.add_argument("--pedal-in-b", help="a stereo pedal's second output (recorded as channel 3)")
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

    di = commands.add_parser("di", help="record the guitar (pedal bypassed) for file signals")
    di.add_argument("session", help="a calibrated session (its rig and reference)")
    di.add_argument("out", help="the WAV to write (git-ignored under captures/)")
    di.add_argument("--seconds", type=float, default=60.0)
    di.add_argument("--note", help="free text: guitar, pickup, what was played")
    di.add_argument("--click", type=float, metavar="BPM",
                    help="a metronome in the headphones (the interface's first two playback channels, or the rig's "
                         "\"monitor\" ports): never recorded")
    di.set_defaults(func=cmd_di)

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
