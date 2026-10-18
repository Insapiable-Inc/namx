#include "NodeCatalog.h"

#include "ChorusProcessor.h"
#include "CompressorProcessor.h"
#include "Constants.h"
#include "DelayProcessor.h"
#include "DriveProcessor.h"
#include "DynamicWahProcessor.h"
#include "EQProcessor.h"
#include "FlangerProcessor.h"
#include "IRProcessor.h"
#include "NamProcessor.h"
#include "NoiseGateProcessor.h"
#include "PhaserProcessor.h"
#include "ReverbProcessor.h"
#include "TransposeProcessor.h"
#include "TremoloProcessor.h"
#include "VolumeProcessor.h"

namespace namx
{

namespace
{

using namespace NAMConstants;
using Kind = ParamSpec::Kind;
using Format = ParamSpec::Format;

// Setters as plain function pointers: T's float / bool / double member.
template <typename T, auto METHOD>
void set_float(EffectBase& fx, double v) {
    (static_cast<T&>(fx).*METHOD)(static_cast<float>(v));
}
template <typename T, auto METHOD>
void set_bool(EffectBase& fx, double v) {
    (static_cast<T&>(fx).*METHOD)(v > 0.5);
}
template <typename T, auto METHOD>
void set_int(EffectBase& fx, double v) {
    (static_cast<T&>(fx).*METHOD)(static_cast<int>(std::lround(v)));
}
template <EQProcessor::Band BAND>
void eq_gain(EffectBase& fx, double v) {
    static_cast<EQProcessor&>(fx).set_gain(BAND, v);
}
template <EQProcessor::Band BAND>
void eq_freq(EffectBase& fx, double v) {
    static_cast<EQProcessor&>(fx).set_freq(BAND, v);
}

ParamSpec knob(string key, string label, double min, double max, double def, string unit,
               double step, void (*apply)(EffectBase&, double), string tip = {}) {
    ParamSpec p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.min = min;
    p.max = max;
    p.def = def;
    p.unit = std::move(unit);
    p.step = step;
    p.apply = apply;
    p.tip = std::move(tip);
    return p;
}

ParamSpec hz(string key, string label, double min, double max, double def, Format format,
             void (*apply)(EffectBase&, double)) {
    ParamSpec p = knob(std::move(key), std::move(label), min, max, def, "Hz", 10.0, apply);
    p.format = format;
    p.log_taper = true;
    return p;
}

ParamSpec toggle(string key, string label, bool def, void (*apply)(EffectBase&, double),
                 string tip = {}) {
    ParamSpec p = knob(std::move(key), std::move(label), 0.0, 1.0, def ? 1.0 : 0.0, {}, 1.0, apply,
                       std::move(tip));
    p.kind = Kind::SWITCH;
    return p;
}

// Times and rates: equal travel = equal ratio.
ParamSpec log_dial(ParamSpec p) {
    p.log_taper = true;
    return p;
}

// The note values of TempoSync::NOTES (ui/TempoSync.h): the index range.
constexpr double NOTE_LAST = 15.0;

// `p` follows the tempo while `switch_key` is on, as note value `note_key`.
ParamSpec synced(ParamSpec p, string switch_key, string note_key, bool is_time, double max = 0.0) {
    p.sync_switch = std::move(switch_key);
    p.sync_note = std::move(note_key);
    p.sync_time = is_time;
    p.sync_max = max;
    return p;
}

ParamSpec sync_switch(string key, const string& label_for) {
    return toggle(std::move(key), "Sync", false, nullptr, "Sync " + label_for + " to the tempo");
}

ParamSpec note(string key, string label, int def, bool stereo = false) {
    ParamSpec p = knob(std::move(key), std::move(label), 0.0, NOTE_LAST, def, {}, 1.0, nullptr,
                       "As a note value at the global tempo");
    p.kind = Kind::NOTE;
    p.stereo_only = stereo;
    return p;
}

ParamSpec reversed_by(ParamSpec p, string switch_key) {
    p.reverse_switch = std::move(switch_key);
    return p;
}

ParamSpec reverse_switch(const string& knob_label) {
    return toggle("reverse", "Reverse", false, nullptr,
                  "Reverse the expression pedal: heel and toe swap for every MIDI controller "
                  "mapped to " +
                      knob_label);
}

ParamSpec stereo_only(ParamSpec p) {
    p.stereo_only = true;
    return p;
}

void io_gain(EffectBase& fx, double v) {
    static_cast<IoProcessor&>(fx).set_gain_db(static_cast<float>(v));
}
void io_limiter(EffectBase& fx, double v) {
    static_cast<IoProcessor&>(fx).set_limiter(v > 0.5);
}

vector<NodeType> build_types() {
    vector<NodeType> t;

    // Modulation, Delay and Reverb come only as mono in, stereo out (user, 2026-10-18):
    // one node per effect; for a mono signal, wire one of its outputs.
    // Amp + cab first: the Add menu's most-used entries. Mono only (user, 2026-10-18:
    // the most intuitive); a stereo rig uses one per channel.
    t.push_back({"nam",
                 "Amp",
                 "Amp",
                 true,
                 false,
                 NodeType::File::MODEL,
                 {
                     knob("input", "Input", NAM_INPUT_MIN, NAM_INPUT_MAX, NAM_INPUT_DEFAULT, "dB",
                          0.5, &set_float<NamProcessor, &NamProcessor::set_input_db>,
                          "Level into the model: more or less breakup"),
                     knob("output", "Level", NAM_OUTPUT_MIN, NAM_OUTPUT_MAX, NAM_OUTPUT_DEFAULT,
                          "dB", 0.5, &set_float<NamProcessor, &NamProcessor::set_output_db>,
                          "Level after the model, on top of the loudness normalization"),
                 }});
    t.push_back({"ir",
                 "Cab",
                 "Cab",
                 true,
                 false,
                 NodeType::File::IR,
                 {
                     knob(
                         "levelA", "Level", IR_LEVEL_MIN, IR_LEVEL_MAX, IR_LEVEL_DEFAULT, "dB", 0.5,
                         [](EffectBase& fx, double v) {
                             static_cast<IRProcessor&>(fx).set_level(0, static_cast<float>(v));
                         },
                         "Level of the cabinet — for IRs that sit too quiet or loud"),
                 }});
    t.push_back({"drive",
                 "Overdrive",
                 "Drive",
                 true,
                 false,
                 NodeType::File::NONE,
                 {
                     knob("drive", "Drive", DRIVE_MIN, DRIVE_MAX, DRIVE_DEFAULT, "%", 1.0,
                          &set_float<DriveProcessor, &DriveProcessor::set_drive_pct>),
                     knob("tone", "Tone", DRIVE_TONE_MIN, DRIVE_TONE_MAX, DRIVE_TONE_DEFAULT, "%",
                          1.0, &set_float<DriveProcessor, &DriveProcessor::set_tone_pct>),
                     knob("level", "Level", DRIVE_LEVEL_MIN, DRIVE_LEVEL_MAX, DRIVE_LEVEL_DEFAULT,
                          "dB", 0.5, &set_float<DriveProcessor, &DriveProcessor::set_level>),
                 }});
    t.push_back(
        {"gate",
         "Noise Gate",
         "Dynamics",
         true,
         false,
         NodeType::File::NONE,
         {
             knob("threshold", "Threshold", GATE_THRESH_MIN, GATE_THRESH_MAX, GATE_THRESH_DEFAULT,
                  "dB", 1.0, &set_float<NoiseGateProcessor, &NoiseGateProcessor::set_threshold>),
             log_dial(knob("attack", "Attack", GATE_ATTACK_MIN, GATE_ATTACK_MAX,
                           GATE_ATTACK_DEFAULT, "ms", 1.0,
                           &set_float<NoiseGateProcessor, &NoiseGateProcessor::set_attack>)),
             log_dial(knob("release", "Release", GATE_RELEASE_MIN, GATE_RELEASE_MAX,
                           GATE_RELEASE_DEFAULT, "ms", 10.0,
                           &set_float<NoiseGateProcessor, &NoiseGateProcessor::set_release>)),
         }});
    t.push_back(
        {"comp",
         "Compressor",
         "Dynamics",
         true,
         false,
         NodeType::File::NONE,
         {
             knob("threshold", "Threshold", COMP_THRESH_MIN, COMP_THRESH_MAX, COMP_THRESH_DEFAULT,
                  "dB", 1.0, &set_float<CompressorProcessor, &CompressorProcessor::set_threshold>),
             knob("ratio", "Ratio", COMP_RATIO_MIN, COMP_RATIO_MAX, COMP_RATIO_DEFAULT, ":1", 0.5,
                  &set_float<CompressorProcessor, &CompressorProcessor::set_ratio>),
             log_dial(knob("attack", "Attack", COMP_ATTACK_MIN, COMP_ATTACK_MAX,
                           COMP_ATTACK_DEFAULT, "ms", 1.0,
                           &set_float<CompressorProcessor, &CompressorProcessor::set_attack>)),
             log_dial(knob("release", "Release", COMP_RELEASE_MIN, COMP_RELEASE_MAX,
                           COMP_RELEASE_DEFAULT, "ms", 10.0,
                           &set_float<CompressorProcessor, &CompressorProcessor::set_release>)),
             knob("makeup", "Level", COMP_MAKEUP_MIN, COMP_MAKEUP_MAX, COMP_MAKEUP_DEFAULT, "dB",
                  0.5, &set_float<CompressorProcessor, &CompressorProcessor::set_makeup>),
         }});
    t.push_back(
        {"wah",
         "Wah",
         "Filter",
         true,
         false,
         NodeType::File::NONE,
         {
             toggle("mode", "Manual", false,
                    &set_bool<DynamicWahProcessor, &DynamicWahProcessor::set_pedal_mode>,
                    "Manual: Position sweeps the wah · off = Auto: your playing sweeps it"),
             knob("mix", "Mix", WAH_MIX_MIN, WAH_MIX_MAX, WAH_MIX_DEFAULT, "%", 1.0,
                  &set_float<DynamicWahProcessor, &DynamicWahProcessor::set_mix_pct>),
             knob("sensitivity", "Sensitivity", WAH_SENS_MIN, WAH_SENS_MAX, WAH_SENS_DEFAULT, "%",
                  1.0, &set_float<DynamicWahProcessor, &DynamicWahProcessor::set_sensitivity_pct>),
             reversed_by(
                 knob("position", "Position", WAH_POSITION_MIN, WAH_POSITION_MAX,
                      WAH_POSITION_DEFAULT, "%", 1.0,
                      &set_float<DynamicWahProcessor, &DynamicWahProcessor::set_position_pct>),
                 "reverse"),
             reverse_switch("Position"),
             knob("resonance", "Resonance", WAH_RES_MIN, WAH_RES_MAX, WAH_RES_DEFAULT, "%", 1.0,
                  &set_float<DynamicWahProcessor, &DynamicWahProcessor::set_resonance_pct>),
         }});
    t.push_back({"eq",
                 "Equalizer",
                 "Filter",
                 true,
                 false,
                 NodeType::File::NONE,
                 {
                     knob("low", "Bass", EQ_BAND_MIN, EQ_BAND_MAX, EQ_BAND_DEFAULT, "dB", 0.1,
                          &eq_gain<EQProcessor::Band::BASS>),
                     knob("mid", "Low-Mid", EQ_BAND_MIN, EQ_BAND_MAX, EQ_BAND_DEFAULT, "dB", 0.1,
                          &eq_gain<EQProcessor::Band::MIDDLE>),
                     knob("highMid", "High-Mid", EQ_BAND_MIN, EQ_BAND_MAX, EQ_BAND_DEFAULT, "dB",
                          0.1, &eq_gain<EQProcessor::Band::HIGH_MID>),
                     knob("treble", "Treble", EQ_BAND_MIN, EQ_BAND_MAX, EQ_BAND_DEFAULT, "dB", 0.1,
                          &eq_gain<EQProcessor::Band::TREBLE>),
                     hz("lowFreq", "Bass Freq", EQ_BASS_FREQ_MIN, EQ_BASS_FREQ_MAX,
                        EQ_BASS_FREQ_DEFAULT, Format::HZ, &eq_freq<EQProcessor::Band::BASS>),
                     hz("midFreq", "Low-Mid Freq", EQ_MID_FREQ_MIN, EQ_MID_FREQ_MAX,
                        EQ_MID_FREQ_DEFAULT, Format::HZ, &eq_freq<EQProcessor::Band::MIDDLE>),
                     hz("highMidFreq", "High-Mid Freq", EQ_HI_MID_FREQ_MIN, EQ_HI_MID_FREQ_MAX,
                        EQ_HI_MID_FREQ_DEFAULT, Format::HZ, &eq_freq<EQProcessor::Band::HIGH_MID>),
                     hz("trebleFreq", "Treble Freq", EQ_TREBLE_FREQ_MIN, EQ_TREBLE_FREQ_MAX,
                        EQ_TREBLE_FREQ_DEFAULT, Format::HZ, &eq_freq<EQProcessor::Band::TREBLE>),
                     hz("lowCut", "Low Cut", LOW_CUT_MIN, LOW_CUT_MAX, LOW_CUT_DEFAULT,
                        Format::HZ_OFF_LOW, &eq_freq<EQProcessor::Band::LOW_CUT>),
                     hz("highCut", "High Cut", HIGH_CUT_MIN, HIGH_CUT_MAX, HIGH_CUT_DEFAULT,
                        Format::HZ_OFF_HIGH, &eq_freq<EQProcessor::Band::HIGH_CUT>),
                 }});
    {
        ParamSpec semis = knob("semitones", "Semitones", -12.0, 12.0, 0.0, "st", 1.0,
                               &set_float<TransposeProcessor, &TransposeProcessor::set_semitones>,
                               "Pitch shift: −1 / −2 for drop tunings, ±12 for octaves");
        semis.format = Format::SEMITONES;
        semis.reverse_switch = "reverse";
        t.push_back({"transpose",
                     "Pitch Shift",
                     "Pitch",
                     true,
                     false,
                     NodeType::File::NONE,
                     {
                         semis,
                         knob("mix", "Mix", 0.0, 100.0, 100.0, "%", 1.0,
                              &set_float<TransposeProcessor, &TransposeProcessor::set_mix_pct>),
                         toggle("continuous", "Continuous", false,
                                &set_bool<TransposeProcessor, &TransposeProcessor::set_continuous>,
                                "Semitones glides smoothly (whammy-style bends) · off = whole "
                                "semitones"),
                         reverse_switch("Semitones"),
                     }});
    }
    t.push_back(
        {"flanger",
         "Flanger",
         "Modulation",
         false,
         true,
         NodeType::File::NONE,
         {
             knob("mix", "Mix", FLANGER_MIX_MIN, FLANGER_MIX_MAX, FLANGER_MIX_DEFAULT, "%", 1.0,
                  &set_float<FlangerProcessor, &FlangerProcessor::set_mix_pct>),
             synced(log_dial(knob("rate", "Rate", FLANGER_RATE_MIN, FLANGER_RATE_MAX,
                                  FLANGER_RATE_DEFAULT, "Hz", 0.1,
                                  &set_float<FlangerProcessor, &FlangerProcessor::set_rate_hz>)),
                    "sync", "syncNote", false),
             sync_switch("sync", "Rate"),
             note("syncNote", "Rate", 1),
             knob("depth", "Depth", FLANGER_DEPTH_MIN, FLANGER_DEPTH_MAX, FLANGER_DEPTH_DEFAULT,
                  "%", 1.0, &set_float<FlangerProcessor, &FlangerProcessor::set_depth_pct>),
             knob("feedback", "Feedback", FLANGER_FEEDBACK_MIN, FLANGER_FEEDBACK_MAX,
                  FLANGER_FEEDBACK_DEFAULT, "%", 1.0,
                  &set_float<FlangerProcessor, &FlangerProcessor::set_feedback_pct>),
             knob("range", "Range", FLANGER_RANGE_MIN, FLANGER_RANGE_MAX, FLANGER_RANGE_DEFAULT,
                  "%", 1.0, &set_float<FlangerProcessor, &FlangerProcessor::set_range_pct>,
                  "Sweep length: low = short, metallic · high = long jet sweep"),
         }});
    t.push_back(
        {"phaser",
         "Phaser",
         "Modulation",
         false,
         true,
         NodeType::File::NONE,
         {
             knob("mix", "Mix", PHASER_MIX_MIN, PHASER_MIX_MAX, PHASER_MIX_DEFAULT, "%", 1.0,
                  &set_float<PhaserProcessor, &PhaserProcessor::set_mix_pct>),
             synced(log_dial(knob("rate", "Rate", PHASER_RATE_MIN, PHASER_RATE_MAX,
                                  PHASER_RATE_DEFAULT, "Hz", 0.1,
                                  &set_float<PhaserProcessor, &PhaserProcessor::set_rate_hz>)),
                    "sync", "syncNote", false),
             sync_switch("sync", "Rate"),
             note("syncNote", "Rate", 4),
             knob("depth", "Depth", PHASER_DEPTH_MIN, PHASER_DEPTH_MAX, PHASER_DEPTH_DEFAULT, "%",
                  1.0, &set_float<PhaserProcessor, &PhaserProcessor::set_depth_pct>),
             knob("feedback", "Feedback", PHASER_FEEDBACK_MIN, PHASER_FEEDBACK_MAX,
                  PHASER_FEEDBACK_DEFAULT, "%", 1.0,
                  &set_float<PhaserProcessor, &PhaserProcessor::set_feedback_pct>),
         }});
    t.push_back(
        {"chorus",
         "Chorus",
         "Modulation",
         false,
         true,
         NodeType::File::NONE,
         {
             knob("mix", "Mix", CHORUS_MIX_MIN, CHORUS_MIX_MAX, CHORUS_MIX_DEFAULT, "%", 1.0,
                  &set_float<ChorusProcessor, &ChorusProcessor::set_mix_pct>),
             synced(log_dial(knob("rate", "Rate", CHORUS_RATE_MIN, CHORUS_RATE_MAX,
                                  CHORUS_RATE_DEFAULT, "Hz", 0.1,
                                  &set_float<ChorusProcessor, &ChorusProcessor::set_rate_hz>)),
                    "sync", "syncNote", false),
             sync_switch("sync", "Rate"),
             note("syncNote", "Rate", 2),
             knob("depth", "Depth", CHORUS_DEPTH_MIN, CHORUS_DEPTH_MAX, CHORUS_DEPTH_DEFAULT, "%",
                  1.0, &set_float<ChorusProcessor, &ChorusProcessor::set_depth_pct>),
         }});
    t.push_back(
        {"tremolo",
         "Tremolo",
         "Modulation",
         false,
         true,
         NodeType::File::NONE,
         {
             knob("depth", "Depth", TREMOLO_DEPTH_MIN, TREMOLO_DEPTH_MAX, TREMOLO_DEPTH_DEFAULT,
                  "%", 1.0, &set_float<TremoloProcessor, &TremoloProcessor::set_depth_pct>),
             synced(log_dial(knob("rate", "Rate", TREMOLO_RATE_MIN, TREMOLO_RATE_MAX,
                                  TREMOLO_RATE_DEFAULT, "Hz", 0.1,
                                  &set_float<TremoloProcessor, &TremoloProcessor::set_rate_hz>)),
                    "sync", "syncNote", false),
             sync_switch("sync", "Rate"),
             note("syncNote", "Rate", 10),
         }});
    t.push_back(
        {"delay",
         "Delay",
         "Delay",
         false,
         true,
         NodeType::File::NONE,
         {
             knob("mix", "Mix", DELAY_MIX_MIN, DELAY_MIX_MAX, DELAY_MIX_DEFAULT, "%", 1.0,
                  &set_float<DelayProcessor, &DelayProcessor::set_mix_pct>),
             synced(
                 log_dial(knob("time", "Time", DELAY_TIME_MIN, DELAY_TIME_MAX, DELAY_TIME_DEFAULT,
                               "ms", 1.0, &set_float<DelayProcessor, &DelayProcessor::set_time>)),
                 "sync", "syncNote", true),
             sync_switch("sync", "the repeat times"),
             note("syncNote", "Time", 8),
             stereo_only(
                 synced(log_dial(knob("rightTime", "R Time", DELAY_TIME_MIN, DELAY_TIME_MAX,
                                      DELAY_RIGHT_TIME_DEFAULT, "ms", 1.0,
                                      &set_float<DelayProcessor, &DelayProcessor::set_right_time>,
                                      "Right repeat time: different from Time for a wide bounce")),
                        "sync", "rightSyncNote", true)),
             note("rightSyncNote", "R Time", 7, true),
             stereo_only(knob("crossFeed", "Ping-Pong", 0.0, 100.0, 0.0, "%", 1.0,
                              &set_float<DelayProcessor, &DelayProcessor::set_cross_feed>,
                              "100 % = repeats bounce between the sides · 0 % = two separate "
                              "echoes")),
             knob("feedback", "Feedback", DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX,
                  DELAY_FEEDBACK_DEFAULT, "%", 1.0,
                  &set_float<DelayProcessor, &DelayProcessor::set_feedback_pct>),
             hz("lowCut", "Low Cut", LOW_CUT_MIN, DELAY_LOW_CUT_MAX, LOW_CUT_DEFAULT,
                Format::HZ_OFF_LOW, &set_float<DelayProcessor, &DelayProcessor::set_low_cut>),
             hz("highCut", "High Cut", DELAY_HIGH_CUT_MIN, HIGH_CUT_MAX, HIGH_CUT_DEFAULT,
                Format::HZ_OFF_HIGH, &set_float<DelayProcessor, &DelayProcessor::set_high_cut>),
             knob("saturation", "Saturation", DELAY_SAT_MIN, DELAY_SAT_MAX, DELAY_SAT_DEFAULT, "%",
                  1.0, &set_float<DelayProcessor, &DelayProcessor::set_saturation>,
                  "Tape-style rounding of the repeats as they build up"),
             knob("shimmer", "Shimmer", 0.0, 100.0, 0.0, "%", 1.0,
                  &set_float<DelayProcessor, &DelayProcessor::set_shimmer_pct>,
                  "Octave-up repeats: each echo climbs higher"),
         }});
    {
        ParamSpec type = knob("type", "Type", 0.0, 2.0, 0.0, {}, 1.0,
                              &set_int<ReverbProcessor, &ReverbProcessor::set_type>);
        type.kind = Kind::CHOICE;
        type.choices = {"Plate", "Hall", "Room"};
        ParamSpec decay =
            knob("roomSize", "Decay", REVERB_ROOM_MIN, REVERB_ROOM_MAX, REVERB_ROOM_DEFAULT, "%",
                 1.0, &set_float<ReverbProcessor, &ReverbProcessor::set_room_size_pct>,
                 "How long the tail takes to fade by 60 dB");
        decay.format = Format::DECAY;
        t.push_back(
            {"reverb",
             "Reverb",
             "Reverb",
             false,
             true,
             NodeType::File::NONE,
             {
                 type,
                 knob("mix", "Mix", REVERB_MIX_MIN, REVERB_MIX_MAX, REVERB_MIX_DEFAULT, "%", 1.0,
                      &set_float<ReverbProcessor, &ReverbProcessor::set_mix_pct>),
                 decay,
                 knob("damping", "Damping", REVERB_DAMPING_MIN, REVERB_DAMPING_MAX,
                      REVERB_DAMPING_DEFAULT, "%", 1.0,
                      &set_float<ReverbProcessor, &ReverbProcessor::set_damping_pct>),
                 stereo_only(knob("width", "Width", REVERB_WIDTH_MIN, REVERB_WIDTH_MAX,
                                  REVERB_WIDTH_DEFAULT, "%", 1.0,
                                  &set_float<ReverbProcessor, &ReverbProcessor::set_width_pct>)),
                 synced(knob("preDelay", "Pre-delay", REVERB_PRE_DELAY_MIN, REVERB_PRE_DELAY_MAX,
                             REVERB_PRE_DELAY_DEFAULT, "ms", 5.0,
                             &set_float<ReverbProcessor, &ReverbProcessor::set_pre_delay>,
                             "Gap before the tail starts: keeps the note clear of the room"),
                        "preDelaySync", "preDelaySyncNote", true, REVERB_PRE_DELAY_SYNC_MAX),
                 sync_switch("preDelaySync", "Pre-delay"),
                 note("preDelaySyncNote", "Pre-delay", 13),
                 hz("lowCut", "Low Cut", LOW_CUT_MIN, REVERB_LOW_CUT_MAX, LOW_CUT_DEFAULT,
                    Format::HZ_OFF_LOW, &set_float<ReverbProcessor, &ReverbProcessor::set_low_cut>),
                 hz("highCut", "High Cut", REVERB_HIGH_CUT_MIN, HIGH_CUT_MAX, HIGH_CUT_DEFAULT,
                    Format::HZ_OFF_HIGH,
                    &set_float<ReverbProcessor, &ReverbProcessor::set_high_cut>),
                 knob("shimmer", "Shimmer", 0.0, 100.0, 0.0, "%", 1.0,
                      &set_float<ReverbProcessor, &ReverbProcessor::set_shimmer_pct>,
                      "Octave-up feedback into the tail — the shimmer reverb"),
             }});
    }
    t.push_back(
        {"volume",
         "Volume Pedal",
         "Utility",
         true,
         false,
         NodeType::File::NONE,
         {
             reversed_by(knob("position", "Volume", VOLUME_POS_MIN, VOLUME_POS_MAX,
                              VOLUME_POS_DEFAULT, "%", 1.0,
                              &set_float<VolumeProcessor, &VolumeProcessor::set_position_pct>),
                         "reverse"),
             reverse_switch("Volume"),
             knob("mid", "Mid", VOLUME_MID_MIN, VOLUME_MID_MAX, VOLUME_MID_DEFAULT, "dB", 1.0,
                  &set_float<VolumeProcessor, &VolumeProcessor::set_mid>,
                  "Level at half travel: lower = swells stay quiet longer"),
         }});
    t.push_back({"merge",
                 "Merge",
                 "Utility",
                 true,
                 false,
                 NodeType::File::NONE,
                 {
                     knob("mix", "Mix A / B", 0.0, 100.0, 50.0, "%", 1.0,
                          &set_float<MergeProcessor, &MergeProcessor::set_mix_pct>,
                          "0 % = input A only · 50 % = both, half each · 100 % = input B only"),
                     toggle("invertB", "Invert B", false,
                            &set_bool<MergeProcessor, &MergeProcessor::set_invert_b>,
                            "Flips input B's polarity: two branches that thin each other out "
                            "when blended can fill out instead"),
                 }});
    // Not in the Add menu: every graph has exactly one of each.
    t.push_back({"input",
                 "Input",
                 "IO",
                 false,
                 false,
                 NodeType::File::NONE,
                 {knob("gain", "Gain", GAIN_MIN, GAIN_MAX, GAIN_DEFAULT, "dB", 0.5, &io_gain)}});
    t.push_back({"output",
                 "Output",
                 "IO",
                 false,
                 false,
                 NodeType::File::NONE,
                 {
                     knob("gain", "Gain", GAIN_MIN, GAIN_MAX, GAIN_DEFAULT, "dB", 0.5, &io_gain),
                     toggle("limiter", "Limiter", false, &io_limiter,
                            "Look-ahead limiter on the output: catches peaks before they clip"),
                 }});
    return t;
}

unique_ptr<EffectBase> make_processor(const string& id) {
    if (id == "nam") {
        return make_unique<NamProcessor>();
    }
    if (id == "ir") {
        return make_unique<IRProcessor>();
    }
    if (id == "drive") {
        return make_unique<DriveProcessor>();
    }
    if (id == "gate") {
        return make_unique<NoiseGateProcessor>();
    }
    if (id == "comp") {
        return make_unique<CompressorProcessor>();
    }
    if (id == "wah") {
        return make_unique<DynamicWahProcessor>();
    }
    if (id == "eq") {
        return make_unique<EQProcessor>();
    }
    if (id == "transpose") {
        return make_unique<TransposeProcessor>();
    }
    if (id == "flanger") {
        return make_unique<FlangerProcessor>();
    }
    if (id == "phaser") {
        return make_unique<PhaserProcessor>();
    }
    if (id == "chorus") {
        return make_unique<ChorusProcessor>();
    }
    if (id == "tremolo") {
        return make_unique<TremoloProcessor>();
    }
    if (id == "delay") {
        return make_unique<DelayProcessor>();
    }
    if (id == "reverb") {
        return make_unique<ReverbProcessor>();
    }
    if (id == "volume") {
        return make_unique<VolumeProcessor>();
    }
    if (id == "merge") {
        return make_unique<MergeProcessor>();
    }
    if (id == "input" || id == "output") {
        return make_unique<IoProcessor>();
    }
    return nullptr;
}

} // namespace

namespace NodeCatalog
{

const vector<NodeType>& types() {
    static const vector<NodeType> all = build_types();
    return all;
}

const NodeType* find(const string& type_id) {
    for (const auto& t : types()) {
        if (t.id == type_id) {
            return &t;
        }
    }
    return nullptr;
}

std::shared_ptr<GraphNode> create(int id, const string& type_id, bool stereo) {
    const NodeType* type = find(type_id);
    if (!type) {
        return nullptr;
    }
    NodeKind kind = stereo ? NodeKind::STEREO : NodeKind::MONO;
    if (type_id == "input") {
        kind = NodeKind::INPUT;
    } else if (type_id == "output") {
        kind = NodeKind::OUTPUT;
    } else if (type_id == "merge") {
        kind = NodeKind::MERGE;
    } else if (stereo ? !type->has_stereo : !type->has_mono) {
        return nullptr;
    }
    unique_ptr<EffectBase> fx = make_processor(type_id);
    if (!fx) {
        return nullptr;
    }
    fx->set_stereo_mode(kind == NodeKind::STEREO);
    fx->set_enabled(true);
    for (const auto& p : type->params) {
        if (p.apply) { // Sync, note values and Reverse are read by the UI only
            p.apply(*fx, p.def);
        }
    }
    return std::make_shared<GraphNode>(id, type_id, kind, std::move(fx));
}

string legacy_group(const string& type_id) {
    if (type_id == "gate") {
        return "noiseGate";
    }
    if (type_id == "comp") {
        return "compressor";
    }
    if (type_id == "wah") {
        return "dynamicWah";
    }
    return type_id;
}

} // namespace NodeCatalog

} // namespace namx
