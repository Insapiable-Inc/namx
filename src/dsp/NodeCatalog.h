#pragma once

#include <string>
#include <vector>

#include "Common.h"
#include "SignalGraph.h"

namespace namx
{

// Every node type the graph offers, with its parameters (ui/node-graph). Pure data
// plus a setter per parameter: the UI builds the parameter panel from it and
// presets/sessions save `key → value` per node, so a new parameter is one line here.
//
// Type IDs are the old effect IDs ("nam", "gate", …): never rename one. Parameter
// keys are the v2 preset keys of the same effect, so a v2 preset maps over by name.

struct ParamSpec
{
    enum class Kind
    {
        KNOB,
        SWITCH, // 0 / 1
        CHOICE // 0 … choices.size() − 1
    };
    // How the UI shows the value.
    enum class Format
    {
        NUMBER, // value + unit
        HZ, // "250 Hz", "3.2 kHz"
        HZ_OFF_LOW, // "250 Hz", "3.2 kHz"; "Off" at the minimum
        HZ_OFF_HIGH, // the same; "Off" at the maximum
        SEMITONES, // "+7 st"
        DECAY // reverb Decay: 0–100 shown as seconds
    };

    string key;
    string label;
    Kind kind = Kind::KNOB;
    double min = 0.0;
    double max = 1.0;
    double def = 0.0;
    string unit;
    double step = 1.0; // scroll / arrow step
    Format format = Format::NUMBER;
    bool log_taper = false;
    vector<string> choices;
    bool stereo_only = false; // shown only on the stereo variant
    string tip;
    void (*apply)(EffectBase& fx, double value) = nullptr;
};

struct NodeType
{
    enum class File
    {
        NONE,
        MODEL, // .nam
        IR // .wav
    };

    string id;
    string name;
    string category; // Amp, Cab, Drive, Dynamics, Filter, Pitch, Modulation, Delay, Reverb, Utility
    bool has_mono = true; // offered as a mono node
    bool has_stereo = false; // offered as a stereo node (L/R ports)
    // The variant a request for `stereo` gets: that one when the type has it, else the
    // type's only one.
    bool variant(bool stereo) const {
        return stereo ? has_stereo || !has_mono : !has_mono;
    }
    File file = File::NONE;
    vector<ParamSpec> params;
};

namespace NodeCatalog
{

// All types, in the order the Add menu lists them (Input/Output excluded from it).
const vector<NodeType>& types();
const NodeType* find(const string& type_id);

// A node of `type_id` (mono, or the stereo variant) with every parameter at its
// default, enabled; nullptr for an unknown type or a variant the type lacks.
// GUI thread; set the sample rate before publishing it.
std::shared_ptr<GraphNode> create(int id, const string& type_id, bool stereo);

// The v2 preset / session group of an effect ID ("gate" → "noiseGate"); the ID
// itself where they match.
string legacy_group(const string& type_id);

} // namespace NodeCatalog

} // namespace namx
