// Modified from
// https://github.com/forrestthewoods/MallocMicrobench/blob/main/MallocMicrobench.cpp

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <future>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// Config
constexpr const char *journalPath = "./journal.txt";
constexpr const char *resultDir = "./result";
constexpr const char *allocator_name = "baseline";

// Typedefs
using Nanoseconds = std::chrono::nanoseconds;
using Microseconds = std::chrono::microseconds;

enum MemoryOp { Alloc, Free };

// jack-of-all-trades struct for memory journal
// contains both input and output because it makes things easy to track and
// report
struct MemoryEntry {
    // Input
    MemoryOp op = MemoryOp::Alloc;
    uint64_t allocSize = 0;  // unused for free
    uint64_t originalPtr = 0;
    uint64_t threadId = 0;

    // Pre-process
    int64_t allocIdx =
        -1;  // help MemoryOp::Free point back to source MemoryOp::Alloc

    // Output
    Microseconds allocTime = Microseconds{0};
    Microseconds freeTime = Microseconds{0};

    static std::vector<MemoryEntry> ParseJournal(const char *filepath);
};


// ----------------------------------------------------------------------------------
// Utility implementations
// ----------------------------------------------------------------------------------
std::string formatTime(Microseconds ns)
{
    auto count = ns.count();
    if (count < 1000) {
        return std::format("{:.2f} microseconds", (double) count);
    } else if (count < 1000 * 1000) {
        return std::format("{:.2f} milliseconds", (double) count / 1000);
    } else {
        return std::format("{:.2f} seconds", (double) count / 1000 / 1000);
    }
}

std::string formatTime(Nanoseconds ns)
{
    auto count = ns.count();
    if (count < 1000) {
        return std::format("{} nanoseconds", count);
    } else if (count < 1000 * 1000) {
        return std::format("{:.2f} microseconds", (double) count / 1000);
    } else if (count < 1000 * 1000 * 1000) {
        return std::format("{:.2f} milliseconds", (double) count / 1000 / 1000);
    } else {
        return std::format("{:.2f} seconds",
                           (double) count / 1000 / 1000 / 1000);
    }
}

std::string formatBytes(uint64_t bytes)
{
    if (bytes < 1024) {
        return std::format("{} bytes", bytes);
    } else if (bytes < 1024 * 1024) {
        return std::format("{} kilobytes", bytes / 1024);
    } else if (bytes < 1024 * 1024 * 1024) {
        return std::format("{} megabytes", bytes / 1024 / 1024);
    } else {
        return std::format("{:.2f} gigabytes",
                           (double) bytes / 1024 / 1024 / 1024);
    }
}

std::vector<MemoryEntry> MemoryEntry::ParseJournal(const char *filepath)
{
    std::vector<MemoryEntry> result;
    result.reserve(1000000);

    std::string line;
    std::ifstream file;
    file.open(filepath);

    if (!file.is_open()) {
        std::cout << "file.is_open" << std::endl;
        std::abort();
    }

    const std::string space_delimiter = " ";
    while (std::getline(file, line)) {
        MemoryEntry entry;

        // Parse op type
        entry.op = line[0] == 'a' ? MemoryOp::Alloc : MemoryOp::Free;

        // Pointer work because C++ can't split a string by whitespace
        char *begin = line.data() + 2;  // skip first char and first space
        char *const lineEnd = line.data() + line.size();
        char *end = std::find(begin, lineEnd, ' ');
        auto advancePtrs = [&begin, &end, lineEnd]() {
            begin = end + 1;
            end = std::find(begin, lineEnd, ' ');
        };

        // (Optional) Parse size
        if (entry.op == MemoryOp::Alloc) {
            entry.allocSize = strtoull(begin, &end, 10);
            advancePtrs();
        }

        // Parse ptr
        // begin = std::find_if(begin, end, [](char c) { return c != '0'; });
        entry.originalPtr = strtoull(begin, &end, 16);
        advancePtrs();

        // Parse threadId
        entry.threadId = strtoull(begin, &end, 10);
        advancePtrs();

        // Parse timepoint
        if (entry.op == MemoryOp::Alloc) {
            entry.allocTime = Microseconds{strtoull(begin, &end, 10)};
        } else {
            entry.freeTime = Microseconds{strtoull(begin, &end, 10)};
        }

        // if (entry.op == MemoryOp::Alloc) {
        //     printf("%c %ld %ld %ld %ld\n", entry.op, entry.allocSize,
        //            entry.originalPtr, entry.threadId,
        //            entry.allocTime.count());
        // }
        result.push_back(entry);
    }

    return result;
}

int main()
{
    /* Parse journal */
    std::cout << "Parsing log file: " << journalPath << std::endl;
    auto journal = MemoryEntry::ParseJournal(journalPath);
    std::cout << "Parse complete" << std::endl << std::endl;

    /* Print stats */

    // Compute data for results
    std::vector<Microseconds> allocTimes;
    for (auto const &entry : journal) {
        if (entry.op == MemoryOp::Alloc) {
            allocTimes.push_back(entry.allocTime);
        }
    }

    std::vector<Microseconds> freeTimes;
    for (auto const &entry : journal) {
        if (entry.op == MemoryOp::Free) {
            freeTimes.push_back(entry.freeTime);
        }
    }

    // Results
    std::sort(allocTimes.begin(), allocTimes.end());
    size_t mallocCount = allocTimes.size();

    std::sort(freeTimes.begin(), freeTimes.end());
    size_t freeCount = freeTimes.size();

    // Compute median alloc size
    std::vector<size_t> allocSizes;
    allocSizes.reserve(mallocCount);
    for (auto const &entry : journal) {
        if (entry.op == MemoryOp::Alloc) {
            allocSizes.push_back(entry.allocSize);
        }
    }
    auto medianIter = allocSizes.begin() + allocSizes.size() / 2;
    std::nth_element(allocSizes.begin(), medianIter, allocSizes.end());
    size_t medianAllocSize = *medianIter;

    Microseconds totalMallocTimeNs{0};
    for (auto const &allocTime : allocTimes) {
        totalMallocTimeNs += allocTime;
    }

    std::cout << "== Replay Results ==" << std::endl;
    std::cout << "Number of Mallocs:    " << mallocCount << std::endl;
    std::cout << "Number of Frees:      " << freeCount << std::endl;
    std::cout << "Median Allocation:    " << formatBytes(medianAllocSize)
              << std::endl;
    std::cout << "Average Malloc Time:  "
              << formatTime(Nanoseconds{
                     (long long int) ((double) totalMallocTimeNs.count() /
                                      mallocCount * 1000)})
              << std::endl;
    std::cout << std::endl;

    std::cout << "Alloc Time" << std::endl;
    std::cout << "Best:    " << formatTime(allocTimes[0]) << std::endl;
    std::cout << "p1:      "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.01f)])
              << std::endl;
    std::cout << "p10:     "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.10f)])
              << std::endl;
    std::cout << "p25:     "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.25f)])
              << std::endl;
    std::cout << "p50:     "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.50f)])
              << std::endl;
    std::cout << "p75:     "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.75f)])
              << std::endl;
    std::cout << "p90:     "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.90f)])
              << std::endl;
    std::cout << "p95:     "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.95f)])
              << std::endl;
    std::cout << "p98:     "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.98f)])
              << std::endl;
    std::cout << "p99:     "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.99f)])
              << std::endl;
    std::cout << "p99.9:   "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.999f)])
              << std::endl;
    std::cout << "p99.99:  "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.9999f)])
              << std::endl;
    std::cout << "p99.999: "
              << formatTime(allocTimes[size_t((float) mallocCount * 0.99999f)])
              << std::endl;
    std::cout << "Worst:   " << formatTime(allocTimes[mallocCount - 1])
              << std::endl
              << std::endl;

    std::cout << "Free Time" << std::endl;
    std::cout << "Best:    " << formatTime(freeTimes[0]) << std::endl;
    std::cout << "p1:      "
              << formatTime(freeTimes[size_t((float) freeCount * 0.01f)])
              << std::endl;
    std::cout << "p10:     "
              << formatTime(freeTimes[size_t((float) freeCount * 0.10f)])
              << std::endl;
    std::cout << "p25:     "
              << formatTime(freeTimes[size_t((float) freeCount * 0.25f)])
              << std::endl;
    std::cout << "p50:     "
              << formatTime(freeTimes[size_t((float) freeCount * 0.50f)])
              << std::endl;
    std::cout << "p75:     "
              << formatTime(freeTimes[size_t((float) freeCount * 0.75f)])
              << std::endl;
    std::cout << "p90:     "
              << formatTime(freeTimes[size_t((float) freeCount * 0.90f)])
              << std::endl;
    std::cout << "p95:     "
              << formatTime(freeTimes[size_t((float) freeCount * 0.95f)])
              << std::endl;
    std::cout << "p98:     "
              << formatTime(freeTimes[size_t((float) freeCount * 0.98f)])
              << std::endl;
    std::cout << "p99:     "
              << formatTime(freeTimes[size_t((float) freeCount * 0.99f)])
              << std::endl;
    std::cout << "p99.9:   "
              << formatTime(freeTimes[size_t((float) freeCount * 0.999f)])
              << std::endl;
    std::cout << "p99.99:  "
              << formatTime(freeTimes[size_t((float) freeCount * 0.9999f)])
              << std::endl;
    std::cout << "p99.999: "
              << formatTime(freeTimes[size_t((float) freeCount * 0.99999f)])
              << std::endl;
    std::cout << "Worst:   " << formatTime(freeTimes[freeCount - 1])
              << std::endl
              << std::endl;
}
