#pragma once

#include <algorithm>
#include <atomic>

#include "Common.h"
#include "EffectBase.h"
#include "Rv6Reverb.h"

namespace namx
{

// Reverb: models of a Boss RV-6's modes (Rv6Reverb.h). The dry stays at unity; Mix is the
// pedal's Level, Decay its Time, Damping its Tone (0–100 % = 7:00–5:00). The pedal's mono
// input (a stereo input summed) gives its A and B outputs: stereo out, or summed at equal
// power for a mono output.
class ReverbProcessor : public EffectBase
{
public:
    // Values are the preset key reverb/type: append only. 0–2 were NAM-X's own Plate, Hall and
    // Room, removed 2026-11-03 for the RV-6: an old preset's Plate, Hall or Room loads as the
    // RV-6's (PresetSerializer::upgrade).
    enum class Type
    {
        RV6_MODULATE = 3,
        RV6_SPRING,
        RV6_PLATE,
        RV6_HALL,
        RV6_ROOM,
        RV6_SHIMMER
    };
    static constexpr int FIRST_TYPE = static_cast<int>(Type::RV6_MODULATE);
    static constexpr int LAST_TYPE = static_cast<int>(Type::RV6_SHIMMER);

    ReverbProcessor();

    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;

    void set_type(int t) {
        type.store(std::clamp(t, FIRST_TYPE, LAST_TYPE));
    }
    // The pedal's Time (the preset key keeps its old name, roomSize).
    void set_room_size_pct(float pct) {
        room_size_pct.store(pct);
    }
    // The pedal's Tone.
    void set_damping_pct(float pct) {
        damping_pct.store(pct);
    }
    // The pedal's Level.
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }

    float get_room_size_pct() const {
        return room_size_pct.load();
    }
    float get_damping_pct() const {
        return damping_pct.load();
    }
    float get_mix_pct() const {
        return mix_pct.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    atomic<int> type{static_cast<int>(Type::RV6_PLATE)};
    atomic<float> room_size_pct{50.0f};
    atomic<float> damping_pct{50.0f};
    atomic<float> mix_pct{30.0f};

    rv6::Engine rv6;
    int active_type = 0; // audio thread: the mode that ran last

    // The pedal's mono input (L and R summed), A and B out.
    void run(float* l, float* r, size_t nframes, bool stereo_in, bool stereo_out);
};

} // namespace namx
