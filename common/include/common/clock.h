#pragma once

#include <chrono>
#include <utility>
#include <optional>


namespace ag {

/**
 * Steady clock with time shifting.
 * Time shifting is not thread-safe and MUST ONLY be used for testing in controlled environments.
 */
class SteadyClock : public std::chrono::steady_clock {
public:
    using Base = std::chrono::steady_clock;

public:
    /**
     * Return (Base::now() + get_time_shift()). Hides now() from Base class.
     * @return the shifted time
     */
    static time_point now() noexcept {
        return Base::now() + m_time_shift;
    }

    static duration get_time_shift() {
        return m_time_shift;
    }

    /**
     * WARNING: not thread-safe, intended only for testing
     */
    static void add_time_shift(duration value) {
        m_time_shift += value;
    }

    /**
     * WARNING: not thread-safe, intended only for testing
     */
    static void reset_time_shift() {
        m_time_shift = duration::zero();
    }

private:
    static duration m_time_shift;
};

template<class T, T DEFAULT_VALUE>
class ExpiringValue {
public:
    using Duration = std::chrono::nanoseconds;

    ExpiringValue(T v, Duration d)
        : m_value(std::move(v))
        , m_expire_timestamp(SteadyClock::now() + d)
        , m_duration(d)
    {}

    explicit ExpiringValue(Duration d)
        : m_value(DEFAULT_VALUE)
        , m_duration(d)
    {}

    ExpiringValue()
        : m_value(DEFAULT_VALUE)
    {}

    ExpiringValue(const ExpiringValue &other) = default;
    ExpiringValue &operator=(const ExpiringValue &other) = default;
    ExpiringValue(ExpiringValue &&other) = default;
    ExpiringValue &operator=(ExpiringValue &&other) = default;

    ExpiringValue &operator=(T v) {
        m_value = std::move(v);
        m_expire_timestamp = SteadyClock::now() + m_duration;
        return *this;
    }

    [[nodiscard]] bool is_timed_out() const {
        return m_expire_timestamp.has_value()
               && SteadyClock::now() > m_expire_timestamp.value();
    }

    const T &get() const {
        if (is_timed_out()) {
            m_value = DEFAULT_VALUE;
            m_expire_timestamp.reset();
        }
        return m_value;
    }

    void reset() {
        m_value = DEFAULT_VALUE;
        m_expire_timestamp.reset();
    }

private:
    mutable T m_value = {};
    mutable std::optional<SteadyClock::time_point> m_expire_timestamp;
    Duration m_duration = {};
};

} // namespace ag
