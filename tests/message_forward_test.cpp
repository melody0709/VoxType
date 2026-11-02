// 跨窗口转发携带 heap payload 的消息顺序回归（离线，只用隐藏窗口，不碰麦克风/剪贴板/网络）。
//
// 场景：主窗口把模型下载完成结果转给 Settings 窗口。此刻 Settings 的队列里可能已经排着
// 一条会 DestroyWindow 的消息（关闭窗口、语言切换重建、进程退出）：
//   - 用 PostMessage 转发 → 转发排在销毁消息**之后** → 窗口一销毁，系统把队列里发往它的
//     消息连同 payload 一起静默丢弃（PostMessage 当时仍返回 TRUE）；
//   - 用 SendMessage 转发（app_messages.h 的 SendToWindow）→ 同步派发，一定先于销毁消息
//     被处理，payload 一定有接收者回收。
//
// 本测试同时钉住反例（同样队列顺序改用 PostMessage → 处理次数 0、payload 无人回收），
// 以免以后有人把 SendToWindow 改回 PostMessage：那条断言会直接失败。
#include "app_messages.h"

#include <cstdio>
#include <string>

namespace {

constexpr UINT kTriggerMessage = WM_APP + 61;
constexpr UINT kPayloadMessage = WM_APP + 62;
constexpr UINT kDestroyMessage = WM_APP + 63;

constexpr wchar_t kTargetClass[] = L"VoxTypeTest.ForwardTarget";
constexpr wchar_t kSourceClass[] = L"VoxTypeTest.ForwardSource";

HWND g_target = nullptr;
bool g_usePostMessage = false;   // true = 跑反例（用 PostMessage 转发）
int g_payloadHandled = 0;
int g_payloadAllocated = 0;
int g_payloadFreed = 0;
int g_destroyHandled = 0;
LPARAM g_orphanPayload = 0;      // 反例中被系统丢弃、需要测试自己回收的 payload

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what);
}

LPARAM MakePayload() {
    ++g_payloadAllocated;
    return reinterpret_cast<LPARAM>(new std::wstring(L"C:\\models\\demo"));
}

void FreePayload(LPARAM payload) {
    if (!payload) return;
    delete reinterpret_cast<std::wstring*>(payload);
    ++g_payloadFreed;
}

void ResetCounters() {
    g_payloadHandled = 0;
    g_payloadAllocated = 0;
    g_payloadFreed = 0;
    g_destroyHandled = 0;
    g_orphanPayload = 0;
}

LRESULT CALLBACK TargetProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    switch (msg) {
    case kPayloadMessage:
        FreePayload(lParam);
        ++g_payloadHandled;
        return 0;
    case kDestroyMessage:
        ++g_destroyHandled;
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK SourceProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    (void)lParam;
    if (msg != kTriggerMessage) return DefWindowProcW(hwnd, msg, wParam, lParam);
    if (g_usePostMessage) {
        g_orphanPayload = MakePayload();
        if (!PostMessageW(g_target, kPayloadMessage, 0, g_orphanPayload)) {
            FreePayload(g_orphanPayload);
            g_orphanPayload = 0;
        }
        return 0;
    }
    const LPARAM payload = MakePayload();
    if (!SendToWindow(g_target, kPayloadMessage, 0, payload)) FreePayload(payload);
    return 0;
}

void PumpUntilIdle() {
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool RegisterClasses(HINSTANCE instance) {
    WNDCLASSEXW target = { sizeof(target) };
    target.lpfnWndProc = TargetProc;
    target.hInstance = instance;
    target.lpszClassName = kTargetClass;
    WNDCLASSEXW source = { sizeof(source) };
    source.lpfnWndProc = SourceProc;
    source.hInstance = instance;
    source.lpszClassName = kSourceClass;
    return RegisterClassExW(&target) != 0 && RegisterClassExW(&source) != 0;
}

HWND CreateHidden(const wchar_t* className, HINSTANCE instance) {
    return CreateWindowExW(0, className, L"", WS_OVERLAPPED,
                           0, 0, 120, 80, nullptr, nullptr, instance, nullptr);
}

// 队列顺序：先"触发转发"的消息，再"销毁目标窗口"的消息。触发消息出队时，销毁消息
// 已经排在队列里——这正是转发必须同步的理由。
void RunScenario(bool usePostMessage, HINSTANCE instance) {
    ResetCounters();
    g_usePostMessage = usePostMessage;
    HWND target = CreateHidden(kTargetClass, instance);
    HWND source = CreateHidden(kSourceClass, instance);
    g_target = target;
    PostMessageW(source, kTriggerMessage, 0, 0);
    PostMessageW(target, kDestroyMessage, 0, 0);
    PumpUntilIdle();
    g_target = nullptr;
    if (source) DestroyWindow(source);
}

} // namespace

int wmain() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!RegisterClasses(instance)) {
        std::printf("FAIL: window class registration failed\n");
        return 1;
    }

    // 1. 修复后的行为：SendMessage 同步转发，赶在销毁消息之前处理完 payload。
    RunScenario(false, instance);
    Check(g_payloadHandled == 1, "synchronous forward is handled while a destroy is queued");
    Check(g_destroyHandled == 1, "the queued destroy message still runs afterwards");
    Check(g_payloadAllocated == 1 && g_payloadFreed == 1,
          "synchronous forward frees its payload exactly once");

    // 2. 目标窗口已不在：必须报告未送达，由调用方回收（不得静默丢失）。
    const LPARAM orphan = MakePayload();
    Check(!SendToWindow(reinterpret_cast<HWND>(static_cast<INT_PTR>(0x1)), kPayloadMessage, 0, orphan),
          "SendToWindow reports failure for a dead handle");
    Check(!SendToWindow(nullptr, kPayloadMessage, 0, orphan),
          "SendToWindow reports failure for a null handle");
    FreePayload(orphan);
    Check(g_payloadAllocated == g_payloadFreed, "caller-owned payload is balanced");

    // 3. 反例（钉住"为什么必须同步"）：同样顺序用 PostMessage，处理器一次都不会跑，
    //    payload 被系统丢弃，只能由测试自己回收。
    RunScenario(true, instance);
    Check(g_payloadHandled == 0, "queued forward is dropped by the destroy message (counterexample)");
    Check(g_destroyHandled == 1, "counterexample still runs the destroy message");
    Check(g_orphanPayload != 0, "counterexample leaks its payload (nobody receives it)");
    FreePayload(g_orphanPayload);
    g_orphanPayload = 0;
    Check(g_payloadAllocated == g_payloadFreed, "counterexample payload reclaimed by the test only");

    if (g_failures == 0) {
        std::printf("message_forward_test PASSED\n");
        return 0;
    }
    std::printf("message_forward_test FAILED (%d checks)\n", g_failures);
    return 1;
}
