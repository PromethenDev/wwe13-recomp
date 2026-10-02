// wwe13 - env-gated wall-clock sampling profiler for named host threads.
//
// WWE13_THREAD_PROFILE="GPU Commands,XThread82FF,@vdswap,@topcpu:2"
//     comma-separated thread-name prefixes (as in /proc/self/task/*/comm) to
//     sample, plus @vdswap for the thread that calls VdSwap and @topcpu:N for
//     the N highest-CPU guest XThread threads; unset = disabled.
// WWE13_THREAD_PROFILE_HZ=500                       samples per second per thread.
// WWE13_THREAD_PROFILE_OUT=path                     default ./thread-profile.bin
// Any other thread that receives SIGPROF is sampled once into the same file
// (the census sends one to a contended global-lock owner when
// WWE13_LOCK_OWNER_SAMPLE=1 and WWE13_RATE_CENSUS=1).
//
// Each sample is an unwound backtrace of the target thread taken from a
// SIGPROF handler driven by a CLOCK_MONOTONIC timer, so blocked (off-CPU)
// time is sampled too. Timestamps are steady_clock ns, the same base as the
// [wwe13-frame-time] t_us lines, so tools/thread-profile.py can split samples
// into slow and fast seconds.

#pragma once

namespace wwe13 {

// Starts the profiler if WWE13_THREAD_PROFILE is set. Threads are looked up by
// name a few times over the first minutes, so late-created threads are found.
void StartThreadProfilerFromEnv();
void StopThreadProfiler();

}  // namespace wwe13
