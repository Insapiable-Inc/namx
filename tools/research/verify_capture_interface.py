"""Read-only check of the established Scarlett 8i6 pedal-capture configuration.

python tools/research/verify_capture_interface.py SESSION
Writes interface-ready.json in SESSION; fails on a mismatch. Does not play audio,
change routing, set gains, or infer the physical input-gain knob position.
Requires access to the running user's Pulse/PipeWire, ALSA and JACK services.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess


def command(*args):
    return subprocess.check_output(args, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    args = parser.parse_args()
    rig = json.loads((args.session/'rig.json').read_text())
    cards = json.loads(command('pactl', '--format=json', 'list', 'cards'))
    matches = [c for c in cards if 'Focusrite_Scarlett_8i6' in c['name']]
    assert len(matches) == 1, 'Expected exactly one Scarlett 8i6 card'
    card = matches[0]
    controls = {5: '0', 12: '127', 13: 'off', 14: '127', 15: 'off',
                17: '1', 19: 'on', 21: 'off', 26: '19', 27: '20', 39: '2', 42: '5'}
    readings = {}
    for numid in controls:
        raw = command('amixer', '-c', 'USB', 'cget', f'numid={numid}')
        match = re.search(r': values=([^\n]+)', raw)
        assert match, f'No value for ALSA control {numid}'
        readings[str(numid)] = {'value': match.group(1).strip(), 'raw': raw}
    nodes = []
    for kind in ['sinks', 'sources']:
        for item in json.loads(command('pactl', '--format=json', 'list', kind)):
            if 'Focusrite_Scarlett_8i6' in item['name']:
                nodes.append({k: item[k] for k in ['name', 'mute', 'volume', 'sample_specification']} | {'kind': kind})
    import jack
    with jack.Client('namx-capture-profile-check', no_start_server=True) as client:
        rate, blocksize = client.samplerate, client.blocksize
        ports = [p.name for p in client.get_ports()]
    problems = []
    if card['active_profile'] != 'pro-audio':
        problems.append('Scarlett profile is not pro-audio')
    if rate != 48000:
        problems.append(f'JACK rate is {rate}, expected 48000')
    for n, expected in controls.items():
        if readings[str(n)]['value'] != expected:
            problems.append(f'ALSA control {n}: expected {expected}, found {readings[str(n)]["value"]}')
    for key in ['pedal_out', 'loop_out', 'pedal_in', 'loop_in']:
        if rig[key] not in ports:
            problems.append('Missing JACK port: '+rig[key])
    if not any(n['kind'] == 'sinks' for n in nodes) or not any(n['kind'] == 'sources' and not n['name'].endswith('.monitor') for n in nodes):
        problems.append('Missing Scarlett sink/source')
    for node in nodes:
        if node['mute'] or any(v['value'] != 65536 for v in node['volume'].values()):
            problems.append('Muted or non-unity PipeWire node: '+node['name'])
    result = {'verified_at_utc': datetime.now(timezone.utc).isoformat(),
              'card': card['name'], 'profile': card['active_profile'], 'rate': rate,
              'blocksize': blocksize, 'alsa_controls': readings, 'pipewire_nodes': nodes,
              'required_jack_ports': {k: rig[k] for k in ['pedal_out', 'loop_out', 'pedal_in', 'loop_in']},
              'problems': problems, 'physical_input_gain': 'Not software-readable; input 2 gain must remain fully counter-clockwise.',
              'calibration': 'Existing reference retained; no new audio calibration performed.'}
    filename = 'interface-ready.json' if not problems else 'interface-check-failed.json'
    (args.session/filename).write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k: result[k] for k in ['profile', 'rate', 'blocksize', 'problems']}, indent=2))
    if problems:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
