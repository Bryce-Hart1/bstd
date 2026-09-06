/**
 * @date July 2026
 * @author Bryce Hart
 *
 * @class IndexedFileReader
 *   Records the byte offset of every physical line in a text file so that a
 *   line can be fetched by its 0-based position without re-scanning the file.
 *   NOTE: the "index" is positional (line 0, line 1, ...), not the numeric
 *   value written on the line.
 *
 * @class Logger
 *   Basic leveled application logger. Four levels: WARNING, LOW, MED, HIGH.
 *   Any subset of levels can be silenced via editWrites(). Each entry is
 *   stamped with its line index and, optionally, the date and time.
 */

#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <cstdint>
#include <iostream>

namespace bstd {
namespace store {

class IndexedFileReader {
public:
    explicit IndexedFileReader(std::string filename): filename_(std::move(filename)){
        reindex();
    }

    // Rebuild the offset table from the file's current contents. Safe to call
    // after the file has been appended to (or created) by someone else.
    void reindex() {
        offsets_.clear();
        in_.close();
        in_.clear();
        in_.open(filename_);
        if (!in_) return;// no file yet means nothing to index

        std::string line;
        std::streampos pos = in_.tellg();
        while (std::getline(in_, line)) {
            offsets_.push_back(pos);
            pos = in_.tellg();
        }
        in_.clear();// clear eof so we can seek later
    }

    // 0-based: get_line(0) is the first physical line.
    std::string get_line(std::size_t ind) {
        if (ind >= offsets_.size())
            throw std::out_of_range("line index out of range");

        in_.clear();
        in_.seekg(offsets_[ind]);

        std::string line;
        std::getline(in_, line);
        return line;
    }

    std::size_t line_count() const { return offsets_.size(); }

    // Truncates the file at the start of the last indexed line.
    void remove_last_line() {
        if (offsets_.empty()) return;

        const std::streampos cut_point = offsets_.back();
        in_.close();                      // release the handle before truncating
        std::filesystem::resize_file(
            filename_, static_cast<std::uintmax_t>(cut_point));
        offsets_.pop_back();
        in_.open(filename_);              // reopen for further reads
    }

private:
    std::string filename_;
    std::ifstream in_;
    std::vector<std::streampos> offsets_;
};


class Logger {
public:
    enum severity {
        WARNING = 0,
        LOW  = 1,
        MED = 2,
        HIGH = 3,
    };

    Logger() : Logger("Logger.log", true, true) {}

    /**
     * @param name  path of the log file
     * @param date  include the date on each entry?
     * @param time  include the time on each entry?
     */
    Logger(std::string name, bool date, bool time)
        : _fileName(std::move(name)),
          _logCount(0),
          _reader(_fileName),
          _optionDate(date),
          _optionTime(time),
          _optionSilenceWarn(false),
          _optionsWriteLow(true),
          _optionsWriteMed(true),
          _optionsWriteHigh(true)
    {
        std::ofstream out(_fileName, std::ios::app);   // creates the file if absent
        if (!out.is_open())
            throw std::runtime_error(
                "Logger [" + _fileName + "] could not open its file.");

        _reader.reindex();
        _logCount = _reader.line_count();  // resume numbering on an existing file

        writePrefix(out, _logCount);
        out << "Created.\n";
        ++_logCount;
    }


    void editWrites(bool silenceWarnings, bool silenceLow, bool silenceMed, bool silenceHigh) {
        _optionSilenceWarn = silenceWarnings;
        _optionsWriteLow = !silenceLow;
        _optionsWriteMed  = !silenceMed;
        _optionsWriteHigh = !silenceHigh;
    }


    void write(std::string_view message, severity level) {
        if (!canBeWritten(level)) return; //adds noting if silenced

        std::ofstream out(_fileName, std::ios::app);
        if (!out.is_open())
            throw std::runtime_error("Logger: could not open file to write.");

        writePrefix(out, _logCount);
        out << label(level) << ": " << message << '\n';
        ++_logCount; //physical line count
    }


    void readLast() {
        _reader.reindex();
        const std::size_t n = _reader.line_count();
        if (n == 0)
            throw std::runtime_error("Cannot read last: log is empty.");
        std::cout << _reader.get_line(n - 1) << '\n';   // last line is n-1
    }

    void clearLast() {
        _reader.reindex();
        _reader.remove_last_line();
        _logCount = _reader.line_count();
    }

private:
    // Every line (constructor and write) shares this prefix format.
    void writePrefix(std::ofstream& file, std::size_t index) {
        file << '[' << index << "] ";
        if (_optionDate) addDate(file);
        if (_optionTime) addTime(file);
    }

    void addDate(std::ofstream& file) {
        using namespace std::chrono;
        const year_month_day ymd{floor<days>(system_clock::now())};
        file << static_cast<int>(ymd.year()) << '-'
             << static_cast<unsigned>(ymd.month()) << '-'
             << static_cast<unsigned>(ymd.day()) << ' ';
    }

    void addTime(std::ofstream& file) {
        using namespace std::chrono;
        const auto now           = system_clock::now();
        const auto since_midnight = now - floor<days>(now);
        const hh_mm_ss hms{duration_cast<system_clock::duration>(since_midnight)};
        file << hms.hours().count()   << ':'
             << hms.minutes().count() << ':'
             << hms.seconds().count() << ' ';
    }

    bool canBeWritten(severity type) const {
        switch (type) {
            case HIGH:    
            return _optionsWriteHigh;
            case MED:     
            return _optionsWriteMed;
            case LOW:     
            return _optionsWriteLow;
            case WARNING: 
            return !_optionSilenceWarn;
        }
        return false;
    }

    static const char* label(severity type) {
        switch (type) {
            case HIGH:    
            return "HIGH";
            case MED:     
            return "MED";
            case LOW:     
            return "LOW";
            case WARNING: 
            return "WARN";
        }
        return "?";
    }


    std::string _fileName;
    std::size_t _logCount;
    IndexedFileReader _reader;
    bool _optionDate;
    bool _optionTime;
    bool _optionSilenceWarn;
    bool _optionsWriteLow;
    bool _optionsWriteMed;
    bool _optionsWriteHigh;
};

} // namespace store
} // namespace bstd