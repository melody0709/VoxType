#include "asr_history.h"

#include <deque>
#include <mutex>

namespace asr_history {
namespace {

std::deque<std::wstring> s_history;
std::mutex s_mutex;

} // namespace

void Add(std::wstring_view text) {
    if (text.empty()) return;
    std::lock_guard<std::mutex> lock(s_mutex);
    s_history.emplace_back(text);
    while (s_history.size() > kMaxRetainedRounds) {
        s_history.pop_front();
    }
}

std::vector<std::wstring> Snapshot() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return { s_history.begin(), s_history.end() };
}

size_t Size() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_history.size();
}

void Clear() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_history.clear();
}

} // namespace asr_history
