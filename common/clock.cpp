#include "common/clock.h"
#include "common/regex.h"

ag::SteadyClock::duration ag::SteadyClock::m_time_shift = ag::SteadyClock::duration::zero();
