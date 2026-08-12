#pragma once
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>
#include <version>

//might not need based on version:
#include <ratio>
#include <cstdint>

//toolchain check:
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    #define BSTD_CLOCK_HAS_TZDB 1
#else
    #define BSTD_CLOCK_HAS_TZDB 0
#endif

namespace bstd{
namespace system{
/**
* Which face the clock reads off. Scoped so an integer or a stray pointer can
* never silently pick a format for you.
*/
enum class time{
    hour12,
    hour24
};

/**
* @author Bryce Hart @date Aug 2026
* A stopwatch over std::chrono::high_resolution_clock plus a few calendar
* readers off the system clock. Elapsed time is measured from start() (or
* reset()) up to stop(), or up to "now" while the clock is still running.
*
* Calendar readers use the local zone when the platform ships a tzdb; they fall
* back to UTC if the zone lookup fails rather than throwing.
*/
class clock{
    using u8 = u_int8_t; //only for use inside implementation
    using highResClock = std::chrono::time_point<std::chrono::high_resolution_clock>;

    private:
    highResClock _startTime;
    highResClock _endTime;
    bool _running;

    private:
    /**
    * A wall-clock instant already broken down into fields, so the UTC and local
    * paths can share every formatter below. Hour is always 0-23 here; the
    * 12-hour split happens at format time.
    */
    struct calendarParts{
        int year;
        unsigned month;
        unsigned day;
        unsigned hour;
        unsigned minute;
        unsigned second;
    };

    /** Splits any days-based chrono time point (sys_time or local_time). */
    template<class TimePoint>
    static calendarParts split(TimePoint tp){
        const auto dayPoint = std::chrono::floor<std::chrono::days>(tp);
        const std::chrono::year_month_day ymd{dayPoint};
        const std::chrono::hh_mm_ss hms{
            std::chrono::floor<std::chrono::seconds>(tp - dayPoint)};

        return calendarParts{
            static_cast<int>(ymd.year()),
            static_cast<unsigned>(ymd.month()),
            static_cast<unsigned>(ymd.day()),
            static_cast<unsigned>(hms.hours().count()),
            static_cast<unsigned>(hms.minutes().count()),
            static_cast<unsigned>(hms.seconds().count())};
    }

    static calendarParts utcParts(){
        return split(std::chrono::system_clock::now());
    }

    static calendarParts localParts(){
        const auto now = std::chrono::system_clock::now();

    #if BSTD_CLOCK_HAS_TZDB
        try{
            std::chrono::zoned_time zt{std::chrono::current_zone(), now};
            // split the *local* time, otherwise the date flips at UTC midnight
            return split(zt.get_local_time());
        }catch(const std::exception&){
            // tzdb present at compile time but unusable at run time (no zone
            // files installed) -- UTC is the honest fallback
            return split(now);
        }
    #else
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
        #if defined(_WIN32)
        if(::localtime_s(&local, &tt) != 0){
            return split(now);
        }
        #else
        if(::localtime_r(&tt, &local) == nullptr){
            return split(now);
        }
        #endif
        return calendarParts{
            local.tm_year + 1900,
            static_cast<unsigned>(local.tm_mon + 1),
            static_cast<unsigned>(local.tm_mday),
            static_cast<unsigned>(local.tm_hour),
            static_cast<unsigned>(local.tm_min),
            static_cast<unsigned>(local.tm_sec)};
    #endif
    }

    /** hh:mm:ss, with an AM/PM suffix and a 1-12 hour on the 12 hour face. */
    static std::string formatClock(const calendarParts& parts, bstd::system::time format){
        char buildTime[16];

        if(format == bstd::system::time::hour24){
            std::snprintf(buildTime, sizeof(buildTime), "%02u:%02u:%02u",
                          parts.hour, parts.minute, parts.second);
            return std::string(buildTime);
        }

        unsigned hour = parts.hour % 12;
        if(hour == 0){
            hour = 12; // midnight and noon both land here
        }
        std::snprintf(buildTime, sizeof(buildTime), "%02u:%02u:%02u %s", hour, parts.minute, parts.second, parts.hour < 12 ? "AM" : "PM");
        return std::string(buildTime);
    }

    std::chrono::year_month_day getMYD()const {
        const auto parts = localParts();
        return std::chrono::year_month_day{
            std::chrono::year{parts.year},
            std::chrono::month{parts.month},
            std::chrono::day{parts.day}};
    }

    /**
    * The elapsed span as a chrono duration; single place that knows about the
    * running/stopped distinction.
    */
    std::chrono::nanoseconds elapsed() const {
        const auto end = _running ? std::chrono::high_resolution_clock::now() : _endTime;
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - _startTime);
    }


    public:
    clock() : _startTime(std::chrono::high_resolution_clock::now()), _endTime(_startTime), _running(false) {}

    void start(){
        _startTime = std::chrono::high_resolution_clock::now();
        _endTime = _startTime;
        _running = true;
    }

    void stop() {
        _endTime = std::chrono::high_resolution_clock::now();
        _running = false;
    }

    bool running() const noexcept { return _running; }

    /**
    * @returns the elapsed time of the clock, down to the nanosecond,
    * formatted as hh:mm:ss.nnnnnnnnn
    *
    * Returns std::string by value: a string_view into a local buffer would
    * dangle the moment this function returns.
    */
    std::string highResolutionPeekTime() const {
        const auto total = elapsed();

        const auto hours   = std::chrono::duration_cast<std::chrono::hours>(total);
        const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(total - hours);
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(total - hours - minutes);
        const auto nanos   = (total - hours - minutes - seconds).count();

        char buildTime[32];
        std::snprintf(buildTime, sizeof(buildTime), "%02lld:%02lld:%02lld.%09lld",
                      static_cast<long long>(hours.count()),
                      static_cast<long long>(minutes.count()),
                      static_cast<long long>(seconds.count()),
                      static_cast<long long>(nanos));

        return std::string(buildTime);
    }

    std::size_t timePassedInMilliseconds() const {
        return static_cast<std::size_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed()).count());
    }

    std::size_t timePassedInNanoseconds() const {
        return static_cast<std::size_t>(elapsed().count());
    }


    void reset() {
        _startTime = std::chrono::high_resolution_clock::now();
        _endTime = _startTime;
        _running = false;
        return;
    }


    /**
    * @param format bstd::system::time::hour12 (the default) for hh:mm:ss with
    * an AM/PM suffix, or bstd::system::time::hour24 for hh:mm:ss.
    * @returns the current UTC time of day.
    */
    std::string time(bstd::system::time format = bstd::system::time::hour12) const {
        return formatClock(utcParts(), format);
    }

    /**
    * @param format bstd::system::time::hour12 (the default) for hh:mm:ss with
    * an AM/PM suffix, or bstd::system::time::hour24 for hh:mm:ss.
    * @returns the current local time of day. Falls back to UTC on a platform
    * with no usable time zone database.
    */
    std::string timeLocal(bstd::system::time format = bstd::system::time::hour12) const {
        return formatClock(localParts(), format);
    }


    /**
    * @returns the full English month name. The view points at static storage,
    * so it stays valid for the life of the program.
    */
    std::string_view monthString() const{
        static constexpr std::array<std::string_view, 12> names{
            "January", "February", "March",     "April",   "May",      "June",
            "July",    "August",   "September", "October", "November", "December"};

        const u8 month = monthIntegral();
        if(month < 1 || month > 12){
            return "Unknown";
        }
        return names[month - 1];
    }

    /**
    * @returns the current month, 1 == January. 0 if the calendar date is
    * somehow invalid.
    */
    u_int8_t monthIntegral() const {
        const auto ymd = getMYD();
        if(!ymd.ok()){
            return 0;
        }
        return static_cast<u_int8_t>(static_cast<unsigned>(ymd.month()));
    }

    /**
    * @returns the current day of the month, 1-31. 0 if the date is invalid.
    */
    u_int8_t dayIntegral() const {
        const auto ymd = getMYD();
        if(!ymd.ok()){
            return 0;
        }
        return static_cast<u_int8_t>(static_cast<unsigned>(ymd.day()));
    }

    /**
    * @returns the current year, e.g. 2026.
    */
    u_int16_t yearIntegral() const {
        const auto ymd = getMYD();
        return static_cast<u_int16_t>(static_cast<int>(ymd.year()));
    }

    /**
    * @returns the year as decimal text. std::string by value for the same
    * lifetime reason as highResolutionPeekTime().
    */
    std::string yearString() const {
        return std::to_string(yearIntegral());
    }

    /**
    * @returns the local date as YYYY-MM-DD.
    */
    std::string dateString() const {
        char buildDate[16];
        std::snprintf(buildDate, sizeof(buildDate), "%04u-%02u-%02u",
                static_cast<unsigned>(yearIntegral()),
                static_cast<unsigned>(monthIntegral()),
                static_cast<unsigned>(dayIntegral()));
        return std::string(buildDate);
    }

};
}
}//bstd