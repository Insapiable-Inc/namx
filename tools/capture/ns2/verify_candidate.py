"""Audit the NS-2 candidate against the handoff's numerical acceptance limits.

  python3 tools/capture/ns2/verify_candidate.py

Writes build-ns2/candidate-validation.json. This checks the unchanged refit masks,
calibrations and per-take RMS scores, including every individual history group.
Audio-rate validation and listening renders are separate from these fitting scores.
"""
import json

import numpy as np

import refit as R
import dataset as D


def audit():
    baseline = {s: dict(v) for s, v in R.score(R.baseline, R.BASELINE_P).items()}
    candidate = {s: dict(v) for s, v in R.score(R.candidate, R.CANDIDATE_P).items()}
    summary = {}
    for name, per in candidate.items():
        before = np.array(list(baseline[name].values()))
        after = np.array(list(per.values()))
        summary[name] = {
            "baseline_median_db": float(np.median(before)),
            "candidate_median_db": float(np.median(after)),
            "baseline_mean_db": float(before.mean()),
            "candidate_mean_db": float(after.mean()),
            "max_take_increase_db": max(per[k] - baseline[name][k] for k in per),
        }
    # 'Clearly under 1.9' is made stricter here: both must be below 1.8 dB.
    checks = {
        "reamp_median_under_0p75": summary["reamp"]["candidate_median_db"] < 0.75,
        "notes_5_under_1p8": candidate["reamp"]["reamp/threshold-0500-notes"] < 1.8,
        "dynamics_5_under_1p8": candidate["reamp"]["reamp/threshold-0500-dynamics"] < 1.8,
        "older_take_increase_at_most_0p5": summary["older"]["max_take_increase_db"] <= 0.5,
        "older_median_not_worse": summary["older"]["candidate_median_db"] <= summary["older"]["baseline_median_db"],
        "every_history_group_not_worse": summary["history"]["max_take_increase_db"] <= 1e-10,
    }
    result = {
        "status": "offline candidate; listening decision pending",
        "law": "20 ms rectified mean; 6 dB peak/mean knee; closing sensitivity 2; continuous closing control",
        "summary": summary, "checks": checks, "baseline": baseline, "candidate": candidate,
        "interpretation": "Scores on the measured fitting set, not an independent holdout or a listening verdict. "
                          "The candidate retains the existing slow tail, knob maps, open gain and output shelf. "
                          "It does not solve the history probes' creep or the fast-Decay funk discrepancy.",
    }
    path = D.ROOT / "build-ns2/candidate-validation.json"
    path.parent.mkdir(exist_ok=True)
    path.write_text(json.dumps(result, indent=2) + "\n")
    for name, values in summary.items():
        print(f"{name:8s}: median {values['baseline_median_db']:.4f} -> {values['candidate_median_db']:.4f} dB; "
              f"worst individual increase {values['max_take_increase_db']:.4f} dB")
    for name, passed in checks.items():
        print(f"{'PASS' if passed else 'FAIL'} {name}")
    return all(checks.values())


if __name__ == "__main__":
    raise SystemExit(0 if audit() else 1)
