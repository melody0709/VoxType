// Offline regression：浏览器隔离与配套扩展桥的纯函数契约。
//
// 背景：Chromium 会把来自平台无障碍消费者（UIA/MSAA）的请求按位 OR 进**进程级**
// 无障碍模式，我们释放 COM 对象并不会撤销它 —— 退出应用、刷新页面都不恢复，只有
// 重启浏览器才行。因此"少查几次"没有意义（一次就够），唯一的办法是**根本不向浏览器
// 进程发无障碍请求**。本测试覆盖这条链路里所有可以在离线跑的判定：
//
//   1. browser_target：进程名/窗口类名分类（含 Electron/WebView2 必须**不**算浏览器）；
//   2. ClassifyFocus：fail-closed 三态真值表（只有"确认不是密码框 + 有焦点 + 身份匹配"
//      才允许 Safe）；
//   3. 协议分帧与字段校验（非法版本/类型/结构必须被拒绝）；
//   4. hello 中窗口清单的解析与边界。
//
// 不覆盖：命名管道的真实连接与浏览器端行为（那需要活体浏览器，见
// extensions/browser-bridge/README.md 的验收说明）。

#include "browser_bridge.h"
#include "browser_target.h"
#include "accessibility_focus.h"

#include <cwchar>
#include <iostream>
#include <limits>
#include <string>

namespace {

int g_failures = 0;

void Expect(bool condition, const std::string& description) {
    if (condition) return;
    std::cerr << "FAIL: " << description << '\n';
    ++g_failures;
}

// ASCII-only narrowing for test labels; the image names and class names in this
// test are deliberately plain ASCII, so no real conversion is needed.
std::string Narrow(const wchar_t* text) {
    std::string out;
    for (; text && *text; ++text) out.push_back(static_cast<char>(*text));
    return out;
}

void TestBrowserClassification() {
    using browser_target::Classify;
    using browser_target::ImageBasename;
    using browser_target::IsBrowserImageName;
    using browser_target::Kind;

    // 大小写与全路径都要归一到 basename。
    Expect(ImageBasename(L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe") ==
               L"chrome.exe",
           "full path resolves to the lower-case basename");
    Expect(ImageBasename(L"MSEDGE.EXE") == L"msedge.exe", "upper-case image name is lowered");

    for (const wchar_t* name : {L"chrome.exe", L"msedge.exe", L"brave.exe", L"360se.exe",
                                L"qqbrowser.exe", L"firefox.exe", L"thunderbird.exe"}) {
        Expect(IsBrowserImageName(name), "recognised browser: " + Narrow(name));
    }

    // Electron / WebView2 / 普通应用刻意**不**算浏览器：把 Chromium 内核的桌面应用
    // 一律当作浏览器，会静默地抽掉这些应用的焦点字段上下文，超出本次修复的范围。
    for (const wchar_t* name : {L"Code.exe", L"msedgewebview2.exe", L"explorer.exe",
                                L"notepad.exe", L"WINWORD.EXE", L""}) {
        Expect(!IsBrowserImageName(name), "not a browser: " + Narrow(name));
    }

    Expect(Classify(L"chrome.exe", L"chrome_widgetwin_1") == Kind::Chromium,
           "chrome.exe classifies as Chromium");
    Expect(Classify(L"firefox.exe", L"mozillawindowclass") == Kind::Gecko,
           "firefox.exe classifies as Gecko");
    // 窗口类名单独不足以升级：Chrome_WidgetWin_1 也是 Electron/WebView2 的宿主类名。
    Expect(Classify(L"Code.exe", L"chrome_widgetwin_1") == Kind::NotBrowser,
           "an Electron host with a Chromium window class stays out of the browser path");
    Expect(Classify(L"", L"chrome_widgetwin_1") == Kind::NotBrowser,
           "an unknown process stays out of the browser path");
}

void TestFocusClassification() {
    using browser_bridge::ClassifyFocus;
    using browser_bridge::FocusState;
    using browser_bridge::FocusStateFromFailure;
    using browser_bridge::kReasonNoWindowMatch;
    using browser_bridge::kReasonNotConnected;
    using browser_bridge::kReasonTimeout;
    using browser_bridge::kReasonWindowAmbiguous;

    // 明确的密码框命中是结论性的，不再需要焦点/身份旁证。
    Expect(ClassifyFocus(true, false, false, false, false) == FocusState::Sensitive,
           "an explicit password hit is Sensitive without any corroboration");
    Expect(ClassifyFocus(true, true, true, true, true) == FocusState::Sensitive,
           "an explicit password hit stays Sensitive");
    // 连"是不是可编辑控件"都不知道时，密码命中依然压过一切。
    Expect(ClassifyFocus(true, true, true, true, false) == FocusState::Sensitive,
           "a password hit outranks an unidentified control type");

    // 缺任何一项旁证都不得变成 Safe —— 这正是 fail-closed 的核心。
    Expect(ClassifyFocus(false, false, true, true, true) == FocusState::UnknownQuery,
           "an undecidable password read is never Safe");
    Expect(ClassifyFocus(false, true, false, true, true) == FocusState::UnknownFocus,
           "no focused frame is never Safe");
    Expect(ClassifyFocus(false, true, true, false, true) == FocusState::UnknownIdentity,
           "an unverified window identity is never Safe");
    // 闭合 shadow root 里的密码框从外面看就是"非可编辑元素"，因此"没看到可编辑控件"
    // 绝不允许被当成"不是密码框"。
    Expect(ClassifyFocus(false, true, true, true, false) == FocusState::UnknownNotTextInput,
           "an unidentifiable control is never Safe, even when it does not look like a password");
    Expect(ClassifyFocus(false, true, true, true, true) == FocusState::Safe,
           "a fully corroborated non-password answer is Safe");

    Expect(FocusStateFromFailure(kReasonNotConnected) == FocusState::UnknownNotConnected,
           "not_connected maps to its own unknown state");
    Expect(FocusStateFromFailure(kReasonTimeout) == FocusState::UnknownTimeout,
           "timeout maps to its own unknown state");
    // "no companion" and "companion present but the window does not line up" must
    // stay distinguishable: they have unrelated causes and only the second one is a
    // geometry/binding problem.
    Expect(FocusStateFromFailure(kReasonNoWindowMatch) == FocusState::UnknownNoWindowMatch,
           "no_window_match maps to its own unknown state");
    Expect(FocusStateFromFailure(kReasonWindowAmbiguous) == FocusState::UnknownWindowAmbiguous,
           "window_ambiguous maps to its own unknown state");
    Expect(FocusStateFromFailure("query_failed") == FocusState::UnknownQuery,
           "any other failure maps to the generic unknown state");
    for (const auto state : {FocusState::Sensitive, FocusState::Safe, FocusState::UnknownNotConnected,
                             FocusState::UnknownNoWindowMatch, FocusState::UnknownWindowAmbiguous,
                             FocusState::UnknownQuery, FocusState::UnknownFocus,
                             FocusState::UnknownIdentity, FocusState::UnknownNotTextInput,
                             FocusState::UnknownTimeout}) {
        Expect(browser_bridge::FocusStateName(state) != nullptr,
               "every focus state has a diagnostic name");
    }
}

void TestFramingValidation() {
    using browser_bridge::BuildQuery;
    using browser_bridge::Need;

    const std::string probe = BuildQuery(7, Need::Probe, 41, 0);
    const std::string field = BuildQuery(8, Need::Field, 41, 400);
    Expect(browser_bridge::detail::IsWellFormedFrame(probe), "a probe query is a well-formed frame");
    Expect(browser_bridge::detail::IsWellFormedFrame(field), "a field query is a well-formed frame");
    Expect(probe.find("\"need\":\"probe\"") != std::string::npos, "the probe query carries need=probe");
    Expect(field.find("\"need\":\"field\"") != std::string::npos, "the field query carries need=field");
    Expect(field.find("\"valueChars\":400") != std::string::npos, "the field query carries the value budget");
    Expect(field.find("\"windowId\":41") != std::string::npos, "the query carries the target window");

    // 非法输入必须在进入任何字段解析之前被拒绝。
    Expect(!browser_bridge::detail::IsWellFormedFrame(""), "an empty frame is rejected");
    Expect(!browser_bridge::detail::IsWellFormedFrame("not json"),
           "a non-JSON frame is rejected");
    Expect(!browser_bridge::detail::IsWellFormedFrame("{\"type\":\"query\"}"),
           "a frame without a version is rejected");
    Expect(!browser_bridge::detail::IsWellFormedFrame("{\"version\":2,\"type\":\"query\"}"),
           "a frame with a foreign version is rejected");
    Expect(!browser_bridge::detail::IsWellFormedFrame("{\"version\":1,\"type\":\"drop_table\"}"),
           "a frame with an unknown type is rejected");
}

void TestHelloParsing() {
    const std::string hello =
        "{\"version\":1,\"type\":\"hello\",\"ext\":\"cjmhlnmnolcbkjpfdbnpejfnigacbpai\","
        "\"windows\":[{\"id\":41,\"left\":0,\"top\":0,\"width\":1920,\"height\":1080,"
        "\"lastFocused\":true},"
        "{\"id\":42,\"left\":-1920,\"top\":100,\"width\":1280,\"height\":720,"
        "\"lastFocused\":false},"
        "{\"id\":43,\"left\":0,\"top\":0,\"width\":0,\"height\":0,\"lastFocused\":false}]}";
    const auto parsed = browser_bridge::ParseHello(hello);
    Expect(parsed.has_value(), "a well-formed hello parses");
    if (parsed) {
        Expect(parsed->extensionId == "cjmhlnmnolcbkjpfdbnpejfnigacbpai",
               "the extension id is carried through");
        // 宽度为 0 的条目被丢弃：它不可能匹配任何真实窗口。
        Expect(parsed->windows.size() == 2, "windows without a usable size are dropped");
        Expect(!parsed->windows.empty() && parsed->windows[0].id == 41 && parsed->windows[0].lastFocused,
               "the focused window keeps its identity and flag");
        Expect(parsed->windows.size() > 1 && parsed->windows[1].bounds.left == -1920,
               "negative screen origins survive parsing");
    }

    Expect(!browser_bridge::ParseHello("{\"version\":1,\"type\":\"result\",\"ext\":\"x\"}")
                .has_value(),
           "a non-hello message does not parse as hello");
    Expect(!browser_bridge::ParseHello("{\"version\":1,\"type\":\"hello\",\"windows\":[]}")
                .has_value(),
           "hello without an extension id is rejected");
    // 字段缺失的窗口条目被丢弃而不是让整条 hello 失效：一个坏条目不应该让整个浏览器
    // profile 失去上下文。
    const auto partial = browser_bridge::ParseHello(
        "{\"version\":1,\"type\":\"hello\",\"ext\":\"x\",\"windows\":[{\"id\":1},"
        "{\"id\":9,\"left\":0,\"top\":0,\"width\":800,\"height\":600}]}");
    Expect(partial.has_value() && partial->windows.size() == 1 &&
               partial->windows[0].id == 9,
           "a window entry without a usable size is dropped, the rest survives");
    Expect(!browser_bridge::ParseHello(
        "{\"version\":1,\"type\":\"hello\",\"ext\":\"x\",\"windows\":[{\"id\":4294967297,"
        "\"width\":800,\"height\":600}]}").has_value(),
        "oversized window IDs cannot wrap into another window");
    std::string crowded = "{\"version\":1,\"type\":\"hello\",\"ext\":\"x\",\"windows\":[";
    for (size_t i = 0; i <= browser_bridge::kMaxWindows; ++i) {
        if (i) crowded += ',';
        crowded += std::format("{{\"id\":{},\"width\":800,\"height\":600}}", i);
    }
    crowded += "]}";
    Expect(!browser_bridge::ParseHello(crowded).has_value(), "the maximum window count is a hard limit");
}

void TestWindowBoundsMatching() {
    browser_bridge::WindowBounds bounds{0, 0, 1920, 1080};
    // 空 HWND 必须返回 false，绝不因为"看起来合理"就认下身份。
    Expect(!browser_bridge::BoundsMatchWindow(nullptr, bounds, 16),
           "a null anchor never matches");
    // 尺寸非法必须返回 false。
    Expect(!browser_bridge::BoundsMatchWindow(reinterpret_cast<HWND>(1), {0, 0, 0, 0}, 16),
           "a degenerate window rectangle never matches");

    // 真实机器上的 125% 缩放案例（首轮实机测试的日志原文）：
    //   advertised=[0,0,1540x1672]（Chrome 用 DIP）anchor=[0,0,1925x2089]（物理像素）
    // 1925/1540 = 2090/1672 = 1.25。这正是原来"没做换算"导致每次都 no_window_match 的场景。
    const browser_bridge::WindowBounds advertised125{0, 0, 1540, 1672};
    const RECT physical125{0, 0, 1925, 2089};
    Expect(browser_bridge::BoundsMatchPhysical(advertised125, physical125, 1.25, 16),
           "a 125% scaled window matches after the DIP->physical conversion");
    Expect(browser_bridge::BoundsMatchPhysical(advertised125, physical125, 1.0, 16) == false,
           "and it does not match without the conversion");

    // 100% 缩放：两个空间相同，未换算也必须匹配。
    const browser_bridge::WindowBounds advertised100{0, 0, 1920, 1080};
    const RECT physical100{0, 0, 1920, 1080};
    Expect(browser_bridge::BoundsMatchPhysical(advertised100, physical100, 1.0, 16),
           "an unscaled window matches in the same coordinate space");
    Expect(browser_bridge::BoundsMatchPhysical(advertised100, physical100, 1.25, 16) == false,
           "a wrong scale is refused");

    // 换一个窗口（不同原点或不同尺寸）必须被拒绝，容差不能大到认下邻居。
    Expect(!browser_bridge::BoundsMatchPhysical(advertised125, RECT{1925, 0, 3850, 2089}, 1.25, 16),
           "a window at a different origin is refused");
    Expect(!browser_bridge::BoundsMatchPhysical(advertised125, RECT{0, 0, 1925, 1200}, 1.25, 16),
           "a window of a different height is refused");

    // 最大化窗口在 GetWindowRect 里会带不可见边框（约 8~10 物理像素）：容差必须吸收它。
    Expect(browser_bridge::BoundsMatchPhysical(advertised125, RECT{-10, -10, 1935, 2099}, 1.25, 16),
           "the invisible resize border of a maximized window stays inside the tolerance");

    const RECT primaryWork{0, 0, 3840, 2088};
    Expect(browser_bridge::BoundsMatchMonitor(advertised125, physical125, 1.25, 0, 0, primaryWork),
           "forced 125% Chrome UI matches independently of Windows 150% DPI and page zoom");
    Expect(!browser_bridge::BoundsMatchMonitor(advertised125, physical125, 1.5, 0, 0, primaryWork),
           "Windows DPI cannot replace the actual browser UI scale");
    Expect(!browser_bridge::BoundsMatchMonitor(advertised125, physical125, 3.125, 0, 0, primaryWork),
           "page DPR cannot replace the browser UI scale");
    const RECT leftWork{-2560, -1440, 0, 0};
    const browser_bridge::WindowBounds leftWindow{-1600, -880, 800, 600};
    const RECT leftPhysical{-2400, -1320, -1200, -420};
    Expect(browser_bridge::BoundsMatchMonitor(leftWindow, leftPhysical, 1.5, -1707, -960, leftWork),
           "negative mixed-DPI monitor origins are mapped relative to the work area");
    Expect(!browser_bridge::BoundsMatchMonitor(leftWindow, RECT{-2200, -1320, -1000, -420},
                                               1.5, -1707, -960, leftWork),
           "a neighbouring window with the same size is refused");
    Expect(!browser_bridge::BoundsMatchMonitor({0, 0, 800, 600}, physical125,
                                               1.25, 0, 0, primaryWork),
           "a proportional smaller window is not accepted by inferring scale from its width");
    for (const double scale : {0.0, -1.0, 17.0, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        Expect(!browser_bridge::BoundsMatchMonitor(advertised125, physical125, scale, 0, 0, primaryWork),
               "invalid browser scales fail closed");
    }
    Expect(!browser_bridge::BoundsMatchMonitor({10000001, 0, 1, 1}, physical125,
                                               1.25, 0, 0, primaryWork),
           "out-of-range geometry fails closed");
    Expect(!browser_bridge::ReplyMatchesWindow("{}", nullptr), "missing reply geometry fails closed");
}

void TestLiteralReaders() {
    const std::string json = "{\"v\":1,\"neg\":-1920,\"big\":2147483648,\"yes\":true,"
                             "\"no\":false,\"text\":\"true\",\"junk\":\"x\"}";
    Expect(browser_bridge::detail::JsonInt(json, "v", -1) == 1, "plain integers parse");
    Expect(browser_bridge::detail::JsonInt(json, "neg", 0) == -1920, "negative integers parse");
    Expect(browser_bridge::detail::JsonInt(json, "missing", 77) == 77,
           "a missing key falls back");
    Expect(browser_bridge::detail::JsonInt(json, "text", -1) == -1,
           "a string is not read as a number");
    Expect(browser_bridge::detail::JsonBool(json, "yes") == true, "true parses");
    Expect(browser_bridge::detail::JsonBool(json, "no") == false, "false parses");
    Expect(!browser_bridge::detail::JsonBool(json, "missing").has_value(),
           "a missing password boolean stays unknown instead of false");
    Expect(!browser_bridge::detail::JsonBool("not json", "yes").has_value(),
           "a malformed document has no password answer");
    Expect(!browser_bridge::detail::JsonBool(json, "text").has_value(),
           "a string cannot answer the password query");
    Expect(browser_bridge::detail::JsonInt("{\"id\":1.5}", "id", -1) == -1 &&
           browser_bridge::detail::JsonInt("{\"id\":1e3}", "id", -1) == -1,
           "fractional/exponent numbers cannot masquerade as protocol integers");
}

void TestCancelledTransport() {
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    Expect(CreatePipe(&read, &write, nullptr, 0) != FALSE, "test synchronous pipe opens");
    if (!read || !write) return;
    char byte{};
    const ULONGLONG start = GetTickCount64();
    Expect(!browser_bridge::detail::TransferSyncBounded(read, std::span<char>(&byte, 1), false, 20),
           "silent synchronous reads honour the deadline");
    Expect(GetTickCount64() - start < 1000, "cancelled synchronous read returns promptly");
    CloseHandle(read);
    CloseHandle(write);

    const auto name = std::format(L"\\\\.\\pipe\\VoxType.BridgeTest.{}", GetCurrentProcessId());
    HANDLE server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                     PIPE_TYPE_BYTE | PIPE_WAIT, 1, 1024, 1024, 0, nullptr);
    Expect(server != INVALID_HANDLE_VALUE, "test overlapped pipe opens");
    if (server == INVALID_HANDLE_VALUE) return;
    HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    Expect(client != INVALID_HANDLE_VALUE, "test overlapped pipe connects");
    if (client != INVALID_HANDLE_VALUE) {
        HANDLE cancelled = CreateEventW(nullptr, TRUE, TRUE, nullptr);
        Expect(!browser_bridge::detail::ReadFrame(server, INFINITE,
                browser_bridge::detail::PipeIo::Overlapped, cancelled),
               "cancellation before I/O starts cannot miss a future read");
        ResetEvent(cancelled);
        HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool received = true;
        std::jthread worker([&] {
            received = browser_bridge::detail::ReadFrame(server, INFINITE,
                browser_bridge::detail::PipeIo::Overlapped, cancelled).has_value();
            SetEvent(finished);
        });
        SetEvent(cancelled);
        const bool stopped = WaitForSingleObject(finished, 1000) == WAIT_OBJECT_0;
        Expect(stopped, "cancellation during an overlapped read returns promptly");
        if (!stopped) DisconnectNamedPipe(server);
        worker.join();
        Expect(!received, "cancelled frames never become successful replies");
        CloseHandle(finished);
        CloseHandle(cancelled);
        CloseHandle(client);
    }
    CloseHandle(server);
    IUIAutomationElement* focused = reinterpret_cast<IUIAutomationElement*>(1);
    Expect(FAILED(accessibility_focus::FocusedElementWithinWindow(nullptr, nullptr, &focused)) &&
           focused == nullptr, "an unavailable anchor cannot fall back to global UIA focus");
}

} // namespace

int main() {
    TestBrowserClassification();
    TestFocusClassification();
    TestFramingValidation();
    TestHelloParsing();
    TestWindowBoundsMatching();
    TestLiteralReaders();
    TestCancelledTransport();

    if (g_failures == 0) {
        std::cout << "browser_bridge_test: all checks passed\n";
        return 0;
    }
    std::cerr << "browser_bridge_test: " << g_failures << " check(s) failed\n";
    return 1;
}
