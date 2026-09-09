#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <thread>

/**
* Every reader below is plain standard C++ over the Linux procfs/sysfs, so this
* header compiles anywhere. On a system without /proc (macOS, Windows) the
* readers simply fail to open the file and return std::nullopt.
* Verified against Alpine (musl) and glibc layouts; no libc extensions used.
*
* Inside a container these files describe the *host* unless the runtime maps
* them (lxcfs and friends). cgroup-aware limits are a separate concern.
*/

namespace bstd{
namespace syst{
/**
* @author Bryce Hart @date Jul 2026
* Returns size_t of threadCount. returns nullopt if std::thread::hardware_concurrency()
* returns 0 (failure to get threadcount). Used std::size_t for compatiblity,
*/
inline std::optional<std::size_t> threadCount() noexcept{
    // Get the number of available hardware threads
    unsigned int threads = std::thread::hardware_concurrency();

    if(threads == 0){
        return std::nullopt;
    }else{
        return static_cast<std::size_t>(threads);
    }

}

/**
* The three load figures the kernel keeps in /proc/loadavg. These are runnable
* + uninterruptible process counts, NOT percentages -- divide by threadCount()
* if you want a per-core figure.
*/
struct LoadAverage{
    double oneMinute;
    double fiveMinute;
    double fifteenMinute;
};

/**
* @author Bryce Hart @date Jul 2026
* Reads all three kernel load averages in a single pass.
* @returns nullopt if /proc/loadavg is missing or malformed.
*/
inline std::optional<LoadAverage> loadAverages() noexcept{
    try{
        std::ifstream f("/proc/loadavg");
        if(!f){
            return std::nullopt;
        }
        LoadAverage load{};
        //the file is "1min 5min 15min running/total lastpid" -- only take the three doubles
        f >> load.oneMinute >> load.fiveMinute >> load.fifteenMinute;
        if(f.fail()){
            return std::nullopt;
        }
        return load;
    }catch(...){
        return std::nullopt;
    }
}

/**
* @author Bryce Hart @date Jul 2026
* The one minute load average, the figure people normally mean by "load".
* @returns nullopt if /proc/loadavg could not be read.
*/
inline std::optional<double> cpuLoadAverage() noexcept{
    const std::optional<LoadAverage> load = loadAverages();
    if(!load.has_value()){
        return std::nullopt;
    }
    return load->oneMinute;
}


namespace privateMembers{
    struct CpuTimes{
        std::uint64_t idle = 0;  //idle + iowait
        std::uint64_t total = 0; //every counted field, idle included
    };

    /**
    * Aggregate "cpu" line of /proc/stat:
    *   cpu user nice system idle iowait irq softirq steal guest guest_nice
    * guest/guest_nice are deliberately skipped -- the kernel already folds them
    * into user/nice, so summing them would double count.
    * Older kernels publish fewer fields, so anything past idle is optional.
    */
    inline std::optional<CpuTimes> readCpuTimes() noexcept{
        try{
            std::ifstream f("/proc/stat");
            if(!f){
                return std::nullopt;
            }
            std::string label;
            f >> label;
            if(f.fail() || label != "cpu"){
                return std::nullopt;
            }

            std::array<std::uint64_t, 8> fields{}; //user..steal
            std::size_t count = 0;
            for(; count < fields.size(); ++count){
                std::uint64_t value = 0;
                if(!(f >> value)){
                    break; //end of line, or the "cpu0" label of the next line
                }
                fields[count] = value;
            }
            if(count < 4){ //need at least user, nice, system, idle
                return std::nullopt;
            }

            CpuTimes times{};
            times.idle = fields[3] + fields[4]; //iowait stays 0 when absent
            for(std::size_t i = 0; i < count; ++i){
                times.total += fields[i];
            }
            return times;
        }catch(...){
            return std::nullopt;
        }
    }

    //samples /proc/stat twice and reports the busy share of the window between them
    inline std::optional<double> sampleCpuUsage(std::chrono::milliseconds interval) noexcept{
        const std::optional<CpuTimes> a = readCpuTimes();
        if(!a.has_value()){
            return std::nullopt;
        }
        std::this_thread::sleep_for(interval);
        const std::optional<CpuTimes> b = readCpuTimes();
        if(!b.has_value()){
            return std::nullopt;
        }

        //counters are monotonic; a non advancing total means no usable window
        if(b->total <= a->total){
            return std::nullopt;
        }
        const double totalDelta = static_cast<double>(b->total - a->total);
        const double idleDelta = (b->idle > a->idle)
            ? static_cast<double>(b->idle - a->idle)
            : 0.0;

        const double percent = ((totalDelta - idleDelta) / totalDelta) * 100.0;
        if(percent < 0.0){
            return 0.0;
        }
        if(percent > 100.0){
            return 100.0;
        }
        return percent;
    }
}

/**
* @author Bryce Hart @date Jul 26
* Busy percentage of every core combined, measured across sampleInterval.
* Blocks for sampleInterval -- /proc/stat holds cumulative counters since boot,
* so a single read tells you nothing; two reads are the minimum.
* @param precision decimal places to round to, clamped to 0..9
* @param sampleInterval width of the measurement window, longer is steadier
* @returns 0.0..100.0, or nullopt if /proc/stat could not be read
*/
inline std::optional<double> cpuAveragePrecentUsage(
        int precision = 2,
        std::chrono::milliseconds sampleInterval = std::chrono::milliseconds(250)) noexcept{
    if(sampleInterval <= std::chrono::milliseconds(0)){
        return std::nullopt;
    }
    const std::optional<double> percent = privateMembers::sampleCpuUsage(sampleInterval);
    if(!percent.has_value()){
        return std::nullopt;
    }
    if(precision < 0){
        precision = 0;
    }
    if(precision > 9){
        precision = 9;
    }
    const double scale = std::pow(10.0, precision);
    return std::round(percent.value() * scale) / scale;
}


namespace privateMembers{
    struct MemInfo{
        std::uint64_t total = 0;
        std::uint64_t free = 0;
        std::uint64_t available = 0;
        std::uint64_t buffers = 0;
        std::uint64_t cached = 0;
        std::uint64_t sReclaimable = 0;
        bool hasAvailable = false;
    };

    //every /proc/meminfo line is "Key:  <number> [kB]", so read the pair then drop the rest
    inline const std::optional<MemInfo> readMemInfo() noexcept{
        try{
            std::ifstream f("/proc/meminfo");
            if(!f){
                return std::nullopt;
            }
            MemInfo mem{};
            std::string key;
            std::uint64_t value = 0;
            while(f >> key >> value){
                f.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); //discard the "kB" unit
                if(key == "MemTotal:"){
                    mem.total = value;
                }else if(key == "MemFree:"){
                    mem.free = value;
                }else if(key == "MemAvailable:"){
                    mem.available = value;
                    mem.hasAvailable = true;
                }else if(key == "Buffers:"){
                    mem.buffers = value;
                }else if(key == "Cached:"){
                    mem.cached = value;
                }else if(key == "SReclaimable:"){
                    mem.sReclaimable = value;
                }
            }
            if(mem.total == 0){
                return std::nullopt;
            }
            return mem;
        }catch(...){
            return std::nullopt;
        }
    }

    //buffers + page cache + reclaimable slab: held by the kernel but handed back on demand
    inline std::uint64_t reclaimableKb(const MemInfo& mem) noexcept{
        return mem.buffers + mem.cached + mem.sReclaimable;
    }

    inline constexpr std::uint64_t KB_PER_MB = 1024; //values are kibibytes, so MB here is MiB
}

/**
* @author Bryce Hart @date Jul 26
* Total physical RAM of the machine, in MB.
* @returns MemTotal in MB, or nullopt if /proc/meminfo could not be read
*/
inline const std::optional<std::uint64_t> memoryTotal() noexcept{
    const std::optional<privateMembers::MemInfo> mem = privateMembers::readMemInfo();
    if(!mem.has_value()){
        return std::nullopt;
    }
    return mem->total / privateMembers::KB_PER_MB;
}

/**
* @author Bryce Hart @date Jul 26
* Memory a new allocation can actually get, in MB. This is MemAvailable, not
* MemFree -- MemFree excludes the page cache the kernel will evict on demand and
* so reads near zero on any machine that has been up a while.
* Kernels older than 3.14 have no MemAvailable; those fall back to
* MemFree + Buffers + Cached + SReclaimable.
* @returns available memory in MB, or nullopt if /proc/meminfo could not be read
*/
inline const std::optional<std::uint64_t> memoryFree() noexcept{
    const std::optional<privateMembers::MemInfo> mem = privateMembers::readMemInfo();
    if(!mem.has_value()){
        return std::nullopt;
    }
    const std::uint64_t availableKb = mem->hasAvailable
        ? mem->available
        : mem->free + privateMembers::reclaimableKb(*mem);
    return availableKb / privateMembers::KB_PER_MB;
}

/**
* @author Bryce Hart @date Jul 26
* Memory actually held by processes, in MB. Matches the "used" column of
* free(1): MemTotal - MemFree - Buffers - Cached - SReclaimable.
* @returns used memory in MB, or nullopt if /proc/meminfo could not be read
*/
inline const std::optional<std::uint64_t> memoryUsed() noexcept{
    const std::optional<privateMembers::MemInfo> mem = privateMembers::readMemInfo();
    if(!mem.has_value()){
        return std::nullopt;
    }
    const std::uint64_t notUsedKb = mem->free + privateMembers::reclaimableKb(*mem);
    if(notUsedKb >= mem->total){ //guard the unsigned wrap if the file is inconsistent
        return 0;
    }
    return (mem->total - notUsedKb) / privateMembers::KB_PER_MB;
}



/**
* @attention if you dont run this as root, you may get nullopt
* @returns double cpuTemp if successful, nullopt if fail.
 */
inline const std::optional<double> cpuTemp(std::int32_t zone) noexcept{
    try{
        std::string zoneStr = std::to_string(zone);
        std::string path = "/sys/class/thermal/thermal_zone" + zoneStr + "/temp";
        std::ifstream f(path);
        if(!f){
            return std::nullopt;
        }
        //if zone is good, get degree
        int milliDeg = 0;
        f >> milliDeg;
        if(f.fail()){
            return std::nullopt;
        }
        return milliDeg / 1000.0;
    }catch(...){
        return std::nullopt;
    }
}

inline const std::optional<double> cpuTemp() noexcept{
    return bstd::syst::cpuTemp(1);
}



}
}
