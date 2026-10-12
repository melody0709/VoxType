#include "injection_ledger.h"

#include <mutex>

namespace injection_ledger {

namespace {

std::mutex g_mutex;
Record g_record;
uint64_t g_sequence = 0;

} // namespace

const char* MethodLogName(InjectionMethod method) {
    switch (method) {
    case InjectionMethod::ClipboardPaste: return "clipboard";
    case InjectionMethod::UnicodeInput: return "unicode";
    case InjectionMethod::WmChar: return "wmchar";
    case InjectionMethod::Unknown: break;
    }
    return "unknown";
}

uint64_t NextSequence() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return ++g_sequence;
}

uint64_t RecordInjection(Record record) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_record = std::move(record);
    g_record.consumed = false;
    if (g_record.sequence == 0) {
        g_record.sequence = ++g_sequence;
    } else if (g_record.sequence > g_sequence) {
        g_sequence = g_record.sequence;
    }
    return g_record.sequence;
}

bool Snapshot(Record& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_record.Valid()) return false;
    out = g_record;
    return true;
}

void Invalidate() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_record = Record{};
}

void Consume() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_record.consumed = true;
}

} // namespace injection_ledger
