#!/usr/bin/env python3
"""Reamps a pedal with the guitar DI dataset (captures/di/: see README.md, Reamp takes).

  tools/capture/reamp.py di                         record the DI dataset, in order: Enter keeps a segment
                                                    already recorded, r records it again; resumes after Ctrl+C
  tools/capture/reamp.py PEDAL [--date YYYY-MM-DD] [--session NAME]   reamp a pedal (NAME: a folder of
                                                    its own, e.g. a redo; default DATE-reamp)
  tools/capture/reamp.py PEDAL --plan focused       the pedal's focused plan (plans/PEDAL-focused.json:
                                                    excerpts chosen for the fit), session DATE-focused
  tools/capture/reamp.py PEDAL --plan stereo        a pedal's stereo plan (plans/PEDAL-stereo.json): both
                                                    outputs recorded whatever the pedal, session DATE-stereo

Creates the day's session on first use — captures/boss/PEDAL/DATE-reamp, from the rig as calibrated
(same interface, cables, input gains and pads), recording Output B on input 1 for the stereo pedals
(CE-5, RV-6, RT-2) — and runs its plan, tools/capture/plans/PEDAL-reamp.json. Run it again to resume:
takes already recorded are skipped.
"""
import argparse, datetime, json, os, pathlib, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
REPOSITORY = HERE.parents[1]
RIG_FROM = REPOSITORY / "captures/boss/rt-2/2026-11-07-followup"
STEREO = {"ce-5", "rv-6", "rt-2"}
PEDALS = sorted(p.name[:-len("-reamp.json")] for p in (HERE / "plans").glob("*-reamp.json"))
# The DI dataset: (file, seconds, click bpm or 0, what to play). Bridge humbucker unless said, as in the
# calibration. The click is in the headphones only.
DATASET = [
    ("noise", 10, 0, "Guitar volume up, hands off the strings: its hum and hiss."),
    ("notes", 90, 60, "Each string, low to high: open, 5th, 7th, 12th fret, picked medium: a note every 3 clicks."),
    ("dynamics", 40, 60, "One note in three registers (low E 3rd fret, D 7th, high E 12th), picked very soft to very "
                         "hard in six steps: a pick every 2 clicks."),
    ("chords", 60, 60, "Open chords (E, A, D, G, C, Em) strummed and let ring, a chord every 4 clicks; then the same "
                       "arpeggiated."),
    ("chugs", 25, 100, "E5 power chord, palm-muted 8th-note chugs for two bars, one open hit on the next bar's first "
                       "click then silence until the bar after; repeat."),
    ("funk", 25, 100, "16th-note strumming on one chord, mostly muted scratches, the chord ringing on a few strokes."),
    ("stabs", 25, 100, "One short chord stab on each accented click, muted at once; silence for the rest of the bar."),
    ("lead", 60, 100, "A lead line across the neck, your style; end on a held note."),
    ("neck", 40, 60, "The NECK pickup: the six open chords strummed, a chord every 4 clicks."),
]


def record_dataset(capture):
    """The dataset in order, each segment through the calibrated rig: one already recorded is kept
    unless you ask to record it again; after each recording, Enter keeps it and r retakes it.
    Every recording is saved as soon as it ends, so a run stopped with Ctrl+C resumes here."""
    for name, seconds, bpm, what in DATASET:
        out = REPOSITORY / "captures/di" / f"{name}.wav"
        if out.exists() and input(f"{name}: recorded already. Enter = keep, r = record again: ").strip().lower() != "r":
            continue
        while True:
            for old in (out, out.with_suffix(".json")):
                old.unlink(missing_ok=True)
            click = f" Click at {bpm} bpm in the headphones." if bpm else ""
            print(f"\n=== {name} ({seconds} s): {what}{click}\n    Start playing after a second of silence.")
            extra = ["--click", str(bpm)] if bpm else []
            subprocess.run(capture + ["di", str(RIG_FROM), str(out), "--seconds", str(seconds), "--note", what] + extra,
                           check=True)
            if input(f"{name}: saved. Enter = keep, r = retake: ").strip().lower() != "r":
                break


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("pedal", choices=PEDALS + ["di"])
    ap.add_argument("--date", default=datetime.date.today().isoformat())
    ap.add_argument("--session", help="the session folder's name (default DATE-reamp)")
    ap.add_argument("--plan", default="reamp", help="which of the pedal's plans: plans/PEDAL-PLAN.json")
    args = ap.parse_args()
    session = REPOSITORY / "captures/boss" / args.pedal / (args.session or f"{args.date}-{args.plan}")
    plan = HERE / "plans" / f"{args.pedal}-{args.plan}.json"
    if args.pedal != "di" and not plan.exists():
        sys.exit(f"no plan {plan.relative_to(REPOSITORY)}")
    capture = [sys.executable, str(HERE / "capture.py")]
    if args.pedal == "di":
        record_dataset(capture)
        return
    if not (session / "rig.json").exists():
        # A stereo pedal's "…-mono" plan records Output A alone (nothing in Output B).
        stereo = ((args.pedal in STEREO and not args.plan.endswith("mono")) or args.plan.startswith("stereo")
                  or json.loads(plan.read_text()).get("both_outputs", False))  # a plan can ask for Output B too
        note = (f"Boss {args.pedal.upper()} on its 9 V adapter: the guitar DI dataset. Input from interface out 3; "
                + ("OUTPUT A -> input 2, OUTPUT B -> input 1" if stereo else "OUTPUT -> input 2")
                + "; INST, AIR off, pad on, gain fully counter-clockwise.")
        subprocess.run(capture + ["init", str(session), "--from", str(RIG_FROM), "--note", note], check=True)
        rig_path = session / "rig.json"
        rig = json.loads(rig_path.read_text())
        for stale in ("capture_tools_source_commit", "capture_plan_sha256"):
            rig.pop(stale, None)
        if not stereo:
            rig.pop("pedal_in_b", None)
        rig_path.write_text(json.dumps(rig, indent=2) + "\n")
        print(f"Session {session.relative_to(REPOSITORY)} ({'stereo' if stereo else 'mono'}).")
    # Paths from the repository root: run.log records them, and it's committed.
    os.chdir(REPOSITORY)
    os.execv(sys.executable, capture + ["run", str(session.relative_to(REPOSITORY)), str(plan.relative_to(REPOSITORY))])


if __name__ == "__main__":
    main()
