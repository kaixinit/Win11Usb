#pragma once
#include "win11usb.hpp"
#include <mutex>
#include <optional>

namespace wu {
// One wake-up per pending batch. Logs remain ordered and complete; only progress
// values that have not yet been displayed are replaced by the newest value.
class UiUpdates {
  public:
    struct Batch {
        std::vector<std::wstring> logs;
        std::optional<Progress> progress;
    };

  private:
    std::mutex mutex;
    Batch pending;
    bool notified = false;
    bool Notify() {
        bool wake = !notified;
        notified = true;
        return wake;
    }

  public:
    bool Log(const std::wstring &line) {
        if (line.empty())
            return false;
        std::lock_guard<std::mutex> lock(mutex);
        pending.logs.push_back(line);
        return Notify();
    }
    bool Update(const Progress &value) {
        std::lock_guard<std::mutex> lock(mutex);
        pending.progress = value;
        return Notify();
    }
    Batch Take() {
        std::lock_guard<std::mutex> lock(mutex);
        Batch batch;
        batch.logs.swap(pending.logs);
        batch.progress.swap(pending.progress);
        notified = false;
        return batch;
    }
};
} // namespace wu
