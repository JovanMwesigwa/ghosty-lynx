// --- Round 180d: FL's TIME stretches the region after its Trim, not the whole file (REA FUN_00d26890, FUN_00ff0db0) ----------

TEST (render180d_time_stretches_the_trimmed_region)
{
    using namespace lynx;
    // 1 s of file: 0.1 s of silence, 0.3 s of sound, 0.6 s of silence
    SampleData d;
    d.sampleRate = 1000.0;
    d.left.assign (1000, 0.0f);
    for (int i = 100; i < 400; ++i)
        d.left[(std::size_t) i] = 0.5f;
    SamplerSettings s;
    CHECK (precompute::stretchedFrames (d, s) == 1000);
    s.precompute.trim = 256; // 0.0078: the silence goes
    CHECK (precompute::stretchedFrames (d, s) == 300);
    s.precompute.trim = 0;
    s.regionEnd = 0.5f; // FL's Length: half the file
    CHECK (std::llabs (precompute::stretchedFrames (d, s) - 500) <= 1);
    s.precompute.trim = 256;
    CHECK (precompute::stretchedFrames (d, s) == 300);
    s.regionStart = 0.2f; // starts inside the sound
    s.regionEnd = 1.0f;
    CHECK (precompute::stretchedFrames (d, s) == 200);
}
