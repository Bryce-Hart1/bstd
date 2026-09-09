#pragma once
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>




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
* Calendar readers use the C library's local zone; they fall back to UTC if the
* zone lookup fails rather than throwing.
*/
class timeKeeper{
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

    static calendarParts fromTm(const std::tm& broken){
        return calendarParts{
            broken.tm_year + 1900,
            static_cast<unsigned>(broken.tm_mon + 1),
            static_cast<unsigned>(broken.tm_mday),
            static_cast<unsigned>(broken.tm_hour),
            static_cast<unsigned>(broken.tm_min),
            static_cast<unsigned>(broken.tm_sec)};
    }

    static std::time_t nowAsTimeT(){
        return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    }

    static calendarParts utcParts(){
        const std::time_t tt = nowAsTimeT();
        std::tm utc{};

    #if defined(_WIN32)
        if(::gmtime_s(&utc, &tt) != 0){
            return calendarParts{};
        }
    #else
        if(::gmtime_r(&tt, &utc) == nullptr){
            return calendarParts{};
        }
    #endif
        return fromTm(utc);
    }

    static calendarParts localParts(){
        const std::time_t tt = nowAsTimeT();
        std::tm local{};

    #if defined(_WIN32)
        if(::localtime_s(&local, &tt) != 0){
            return utcParts();
        }
    #else
        if(::localtime_r(&tt, &local) == nullptr){
            return utcParts();
        }
    #endif
        return fromTm(local);
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

    calendarParts getMYD()const {
        return localParts();
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
    timeKeeper() : _startTime(std::chrono::high_resolution_clock::now()), _endTime(_startTime), _running(false) {}

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
    * Returns std::string by value: a pointer into a local buffer would
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

    std::size_t timePassedInMilliseconds() const{
        return static_cast<std::size_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed()).count());
    }

    std::size_t timePassedInNanoseconds() const{
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
    * @returns the full English month name. The pointer targets static storage,
    * so it stays valid for the life of the program.
    */
    const char* monthString() const{
        static const char* const names[12] = {
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
        const auto parts = getMYD();
        if(parts.month < 1 || parts.month > 12){
            return 0;
        }
        return static_cast<u_int8_t>(parts.month);
    }

    /**
    * @returns the current day of the month, 1-31. 0 if the date is invalid.
    */
    u_int8_t dayIntegral() const {
        const auto parts = getMYD();
        if(parts.day < 1 || parts.day > 31){
            return 0;
        }
        return static_cast<u_int8_t>(parts.day);
    }

    /**
    * @returns the current year, e.g. 2026.
    */
    u_int16_t yearIntegral() const {
        return static_cast<u_int16_t>(getMYD().year);
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
