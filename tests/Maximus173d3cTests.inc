// Round 173d3c: the last 3 of FL's 41 Maximus presets. "Destruction" draws a double-curve segment and "Nawak" hold
// segments (FL's envelope evaluator FUN_007885d0 in SG_x64.dll: mode 1 = the single curve on each half, mode 2 = the
// previous point's level); "Mid-triggered gate" uses Master Mid mode (+0x324: FUN_008417e0 mutes the low and high bands,
// FUN_00843a40 leaves the input unsplit by the mid band and applies the mid band's gain to the full signal).
// Included from tests.cpp after Maximus168xTests.inc (maximusTestState).

namespace
{
std::vector<std::uint8_t> mx173State (int midMode, int curveMode, int midBandMode, int midPost)
{
    std::array<std::array<int, 13>, 4> bands {};
    for (auto& b : bands)
        b = { 0, 1000, 1000, 0, 0, 1000, 1115, 0, 2, 5000, 3, 2882, 0 };
    bands[1][0] = midBandMode;
    bands[1][2] = midPost;
    std::array<std::vector<std::array<double, 4>>, 8> envs;
    for (auto& e : envs)
        e = { { 0.0, 0.0, 0.0, 0 }, { 0.5, 0.5, 0.0, (double) curveMode }, { 0.5, 0.5, 0.0, 0 } };
    auto st = maximusTestState ({ 19800, 1, 48065, 1, 1115, 1000, 32768 }, bands, envs, 0);
    st[0x1fc] = (std::uint8_t) midMode;
    return st;
}

double mx173Rms (const std::vector<std::uint8_t>& state, double hz, float amp)
{
    auto fx = lynx::maximusfx::make (lynx::maximusfx::pack ({ lynx::hlfx::MaximusBuild::shared, -1, state }));
    fx->prepare (44100.0);
    std::vector<float> l (88200), r (88200);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = amp * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / 44100.0);
    lynx::EffectParams params {};
    for (std::size_t at = 0; at < l.size(); at += 512)
        fx->process (l.data() + at, r.data() + at, (int) std::min<std::size_t> (512, l.size() - at), params, 120.0);
    double s = 0.0;
    for (std::size_t i = 44100; i < l.size(); ++i)
        s += (double) l[i] * l[i];
    return std::sqrt (s / 44100.0);
}
} // namespace

TEST (maximus173d3c_double_curve_and_hold_segments)
{
    using namespace lynx;
    for (const int mode : { 0, 1, 2, 7 })
        CHECK (hlfx::makeMaximusPreset (mx173State (0, mode, 0, 1000)) != nullptr);
    CHECK (hlfx::makeMaximusPreset (mx173State (0, 3, 0, 1000)) == nullptr); // (stairs: not modelled yet)
    // hold: the curve stays at the first point's level (0) up to the next point, so a quiet sine is silenced
    const auto straight = mx173Rms (mx173State (0, 0, 0, 1000), 220.0, 0.1f);
    const auto held = mx173Rms (mx173State (0, 2, 0, 1000), 220.0, 0.1f);
    CHECK (straight > 0.02 && held < straight * 0.1);
    // a double curve without tension is a straight segment (FL marks it straight)
    CHECK (std::abs (mx173Rms (mx173State (0, 1, 0, 1000), 220.0, 0.1f) - straight) < straight * 1e-3);
}

TEST (maximus173d3c_master_mid_mode)
{
    using namespace lynx;
    CHECK (hlfx::makeMaximusPreset (mx173State (1, 0, 0, 1000)) != nullptr); // (was rejected)
    // the mid band off in effect (compression off, post 0): normally a 50 Hz tone goes through the low band; in Mid
    // mode the low band is muted and the mid band's (zero) gain acts on the whole signal
    const auto normal = mx173Rms (mx173State (0, 0, 1, 0), 50.0, 0.3f);
    const auto mid = mx173Rms (mx173State (1, 0, 1, 0), 50.0, 0.3f);
    CHECK (normal > 0.1 && mid < normal * 0.01);
    // and with the mid band at unity, the whole signal comes through the mid band (low and high muted)
    CHECK (mx173Rms (mx173State (1, 0, 1, 1000), 50.0, 0.3f) > 0.15);
}

TEST (maximus173d3c_fl_presets)
{
    if (realFlpDir.empty())
        return;
    for (const char* name : { "Destruction.fst", "Nawak.fst", "Mid-triggered gate.fst" })
    {
        std::ifstream in (std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Effects" / "Maximus" / "Fancy" / name, std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const auto fx = lynx::effectFromFlPreset (bytes, 130.0);
        CHECK (fx && fx->kind == lynx::EffectKind::maximus);
    }
}
