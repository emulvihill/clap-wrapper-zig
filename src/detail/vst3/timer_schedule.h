#pragma once

#include <cstdint>

/*
 * Scheduling for the CLAP timers the VST3 wrapper fires from onIdle().
 *
 * onIdle() is polled, not called on a deadline: every 10 ms on macOS and every
 * 20 ms on Windows. A timer that re-arms from the poll that fired it ("now +
 * period") absorbs each poll's lateness into its period, so a 33 ms timer
 * fires about every 40 ms under either interval. Re-arming from the previous
 * deadline keeps the timer on its own grid, and the poll lateness averages out.
 *
 * Kept in a header with no dependencies beyond the standard library so it can
 * be compiled and exercised without the VST3 SDK.
 */

namespace Clap
{

/*
 * True when a periodic timer is due at `now`; advances `nexttick` when it is.
 * A deadline equal to `now` is due. Re-arms from the previous deadline, so
 * polling lateness does not accumulate. After a stall longer than one period,
 * restarts one period from now instead of firing the missed ticks in a burst.
 * A period of 0 marks a free slot and is never due.
 */
inline bool timerDue(uint64_t &nexttick, uint32_t period, uint64_t now)
{
  if (period == 0 || now < nexttick) return false;
  nexttick += period;
  if (nexttick <= now) nexttick = now + period;
  return true;
}

}  // namespace Clap
