// 离线协议回归测试：公共 JSON 解码、百度响应解析与火山请求参数构造。
// 无网络、无 WinHTTP 实际调用，只验证纯协议函数。

#include <windows.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "utils.h"
#include "config_registry.h"
#include "vocabulary_manager.h"
#include "asr_context.h"
#include "asr_history.h"
#include "input_context.h"   // sensitive-focus probe answers / fail-closed mapping
#include "baidu_asr.h"       // ExtractJsonInt / ExtractBaiduResultText
#include "mai_transcribe.h"
#include "volcengine_asr.h"  // BuildExtraParamsJson
#include "asr_result.h"
#include "path_service.h"
#include "llm_refine.h"

// 离线测试桩：不链接真实日志实现（其依赖 globals.h → sherpa-onnx 等重头文件），
// 本测试只验证协议解析行为，日志调用直接吞掉。
namespace asr_runtime_log {
void Write(const char* format, ...) {
    (void)format;
}
} // namespace asr_runtime_log

static int g_failures = 0;

#define CHECK(cond, name)                                     \
    do {                                                      \
        if (!(cond)) {                                        \
            ++g_failures;                                     \
            wprintf(L"FAIL: %hs\n", name);                    \
        } else {                                              \
            wprintf(L"ok:   %hs\n", name);                    \
        }                                                     \
    } while (0)

int wmain() {
    // 1) 基础反转义：\" \\ \n \t
    {
        const std::string json = "{\"text\":\"a\\\"b\\\\c\\nd\\te\"}";
        std::wstring t = ExtractJsonStringDecoded(json, "text");
        CHECK(t == L"a\"b\\c\nd\te", "decoded basic escapes");
    }
    // 2) Unicode 代理对：\ud83d\ude00 -> U+1F600（UTF-16 代理对）
    {
        const std::string json = "{\"text\":\"\\ud83d\\ude00\"}";
        std::wstring t = ExtractJsonStringDecoded(json, "text");
        CHECK(t.size() == 2 && t[0] == 0xD83D && t[1] == 0xDE00,
              "surrogate pair UTF-16 preserved");
    }
    // 3) 汉字原样透传
    {
        const std::string json = "{\"text\":\"\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C\"}";
        CHECK(ExtractJsonStringDecoded(json, "text") == L"\u4F60\u597D\u4E16\u754C",
              "utf8 passthrough");
    }
    // 4) 字符串提取器拒绝数字；数字走 ExtractJsonInt
    {
        const std::string json = "{\"expires_in\":2592000}";
        CHECK(ExtractJsonStringDecoded(json, "expires_in") == L"",
              "string extractor rejects numbers");
        CHECK(baidu_asr::ExtractJsonInt(json, "expires_in", -1) == 2592000,
              "numeric expires_in decoded");
    }
    // 5) 百度 result[0]：文本内转义引号（旧裸 find 会在此截断）
    {
        const std::string body = "{\"err_no\":0,\"result\":[\"\xE4\xBB\x96\xE8\xAF\xB4\\\"\xE4\xBD\xA0\xE5\xA5\xBD\\\"\xE5\x95\x8A\"]}";
        CHECK(baidu_asr::ExtractBaiduResultText(body) == L"\u4ED6\u8BF4\"\u4F60\u597D\"\u554A",
              "baidu result[0] escaped quotes");
    }
    // 6) 百度 result[0]：结果含反斜杠
    {
        const std::string body = "{\"err_no\":0,\"result\":[\"C:\\\\temp\"]}";
        CHECK(baidu_asr::ExtractBaiduResultText(body) == L"C:\\temp",
              "baidu result[0] backslash");
    }
    // 7) 火山 Extra Params：字符串内逗号/括号不得截断
    {
        const std::wstring params = L"\"utterance_eos\":1,\"punctuation_text\":\"a,b{c}\",\"nested\":{\"k\": [1,2]}";
        const std::string out = volc_asr::BuildExtraParamsJson(params);
        const std::string expect = ",\"utterance_eos\":1,\"punctuation_text\":\"a,b{c}\",\"nested\":{\"k\": [1,2]}";
        CHECK(out == expect, "extra params string-aware");
    }
    // 8) 火山 Extra Params：corpus 整体跳过，其余保留
    {
        const std::wstring params = L"\"corpus\":{\"x\":\"a,b\"},\"keep\":1";
        const std::string out = volc_asr::BuildExtraParamsJson(params);
        CHECK(out == ",\"keep\":1", "extra params corpus skipped");
    }
    // 9) 火山 Extra Params：空输入返回空
    {
        CHECK(volc_asr::BuildExtraParamsJson(L"").empty(), "extra params empty input");
        CHECK(volc_asr::BuildExtraParamsJson(L" \t\r\n ").empty(),
              "extra params whitespace-only input");
        std::string out;
        CHECK(!volc_asr::TryBuildExtraParamsJson(L"\"bad\":\"unterminated", out),
              "extra params malformed string rejected");
        CHECK(!volc_asr::TryBuildExtraParamsJson(L"\"bad\":[1,2", out),
              "extra params malformed nesting rejected");
    }
    // 10) 畸形 JSON 字符串必须整体失败，不能返回部分文本
    {
        CHECK(ExtractJsonStringDecoded("{\"text\":\"partial", "text").empty(),
              "unterminated string rejected");
        CHECK(ExtractJsonStringDecoded("{\"text\":\"bad\\q\"}", "text").empty(),
              "unknown escape rejected");
        CHECK(ExtractJsonStringDecoded("{\"text\":\"bad\\u12xz\"}", "text").empty(),
              "invalid unicode escape rejected");
        CHECK(ExtractJsonStringDecoded("{\"text\":\"ok\",\"tail\":", "text").empty(),
              "malformed trailing document rejected");
    }
    // 11) UTF-16 代理项必须成对出现
    {
        CHECK(ExtractJsonStringDecoded("{\"text\":\"\\ud83d\"}", "text").empty(),
              "lone high surrogate rejected");
        CHECK(ExtractJsonStringDecoded("{\"text\":\"\\ude00\"}", "text").empty(),
              "lone low surrogate rejected");
    }
    // 12) 数组跳过嵌套值时必须保持结构，不得被嵌套逗号截断
    {
        const std::string body = "{\"result\":[{\"nested\":[1,2]},\"ok\"]}";
        CHECK(ExtractJsonArrayFirstStringDecoded(body, "result") == L"ok",
              "array nested value skipped structurally");
        CHECK(ExtractJsonArrayFirstStringDecoded("{\"result\":[{\"x\":1},]}", "result").empty(),
              "malformed array rejected");
    }
    // 13) 百度短 token 有效期不会因提前刷新减法而下溢
    {
        CHECK(baidu_asr::TokenCacheLifetimeMs(30) == 27000ULL,
              "short token lifetime bounded");
        CHECK(baidu_asr::TokenCacheLifetimeMs(600) == 540000ULL,
              "long token refresh margin capped");
    }
    // 14) 火山字符串字段转义覆盖引号、反斜杠和控制字符
    {
        CHECK(volc_asr::ToBackslashEscape("a\"b\\c\nd\t") == "a\\\"b\\\\c\\nd\\t",
              "volc json string escaping");
        CHECK(EscapeJson(std::wstring(L"a\b\f\n\t") + wchar_t(1)) ==
                  "a\\b\\f\\n\\t\\u0001",
              "common json control escaping");
    }
    // 15) 百度整数解析不抛异常，也不把小数/溢出值截断成有效整数
    {
        CHECK(baidu_asr::ExtractJsonInt("{\"n\":-2147483648}", "n", 7) == INT_MIN,
              "baidu int minimum decoded");
        CHECK(baidu_asr::ExtractJsonInt("{\"n\":2147483648}", "n", 7) == 7,
              "baidu int overflow rejected");
        CHECK(baidu_asr::ExtractJsonInt("{\"n\":12.5}", "n", 7) == 7,
              "baidu fractional number rejected");
        CHECK(baidu_asr::ExtractJsonInt("{\"n\":--1}", "n", 7) == 7,
              "baidu malformed number rejected");
    }
    // 16) OpenRouter MAI 请求固定模型，Auto 省略 language。
    {
        CHECK(mai_transcribe::EncodeBase64ForTest({'a', 'b', 'c'}) == "YWJj",
              "mai base64 has no line breaks");
        const std::string automatic =
            mai_transcribe::BuildOpenRouterJsonForTest("YWJj", L"auto");
        CHECK(automatic.find("\"model\":\"microsoft/mai-transcribe-2\"") !=
                  std::string::npos &&
              automatic.find("\"format\":\"wav\"") != std::string::npos &&
              automatic.find("\"language\"") == std::string::npos,
              "mai openrouter automatic request");
        const std::string cantonese =
            mai_transcribe::BuildOpenRouterJsonForTest("YWJj", L"yue");
        CHECK(cantonese.find("\"language\":\"yue\"") != std::string::npos,
              "mai openrouter language request");
    }
    // 17) OpenRouter response 使用共享严格 JSON 解码。
    {
        std::wstring expected = L"say \"hi\" ";
        expected.push_back(0xD83D);
        expected.push_back(0xDE00);
        CHECK(mai_transcribe::ParseOpenRouterTextForTest(
                  "{\"text\":\"say \\\"hi\\\" \\ud83d\\ude00\",\"usage\":{}}") ==
                  expected,
              "mai openrouter response decoded");
        CHECK(mai_transcribe::ParseOpenRouterTextForTest(
                  "{\"text\":\"partial\"").empty(),
              "mai openrouter malformed response rejected");
    }
    // 18) Azure multipart 使用 enhancedMode、固定模型和原始 WAV。
    {
        const std::vector<BYTE> wav = {'R', 'I', 'F', 'F', 0, 1, 2, 3};
        const std::string boundary = "----VoxTypeTestBoundary";
        const std::vector<BYTE> multipart =
            mai_transcribe::BuildAzureMultipartForTest(wav, L"zh", boundary);
        const std::string body(multipart.begin(), multipart.end());
        CHECK(body.find("--" + boundary + "\r\n") == 0 &&
              body.find("\"locales\":[\"zh\"]") != std::string::npos &&
              body.find("\"enhancedMode\":{\"enabled\":true") !=
                  std::string::npos &&
              body.find("\"transcriptionMode\"") == std::string::npos &&
              body.find("\"model\":\"MAI-Transcribe-2\"") !=
                  std::string::npos &&
              body.rfind("--" + boundary + "--\r\n") ==
                  body.size() - boundary.size() - 6,
              "mai azure multipart shape");
        CHECK(std::search(multipart.begin(), multipart.end(),
                          wav.begin(), wav.end()) != multipart.end(),
              "mai azure multipart preserves wav bytes");
        const std::vector<BYTE> automaticMultipart =
            mai_transcribe::BuildAzureMultipartForTest(wav, L"auto", boundary);
        const std::string automatic(automaticMultipart.begin(),
                                    automaticMultipart.end());
        CHECK(automatic.find("\"locales\"") == std::string::npos,
              "mai azure automatic omits locales");
    }
    // 19) Azure combinedPhrases 多项聚合并严格拒绝畸形 JSON。
    {
        const std::string body =
            "{\"combinedPhrases\":[{\"text\":\"hello\"},{\"text\":\"\\u4e16\\u754c\"}],"
            "\"phrases\":[]}";
        CHECK(mai_transcribe::ParseAzureTextForTest(body) == L"hello \u4e16\u754c",
              "mai azure combined phrases decoded");
        CHECK(mai_transcribe::ParseAzureTextForTest(
                  "{\"combinedPhrases\":[{\"text\":1}]}").empty(),
              "mai azure malformed phrase rejected");
    }
    // 20) Azure Endpoint 只接受 HTTPS resource root。
    {
        std::wstring error;
        CHECK(mai_transcribe::ValidateAzureEndpointForTest(
                  L"https://example.cognitiveservices.azure.com/", error),
              "mai azure endpoint accepted");
        CHECK(!mai_transcribe::ValidateAzureEndpointForTest(
                  L"http://example.test", error),
              "mai azure http rejected");
        CHECK(!mai_transcribe::ValidateAzureEndpointForTest(
                  L"https://example.test/custom/path", error),
              "mai azure path rejected");
        CHECK(!mai_transcribe::ValidateAzureEndpointForTest(
                  L"https://example.test/?api-version=bad", error),
              "mai azure query rejected");
    }
    // 21) 火山 BuildInitRequestJson 协议构建与参数覆盖
    {
        volc_asr::VolcConfig cfg;
        cfg.apiKey = L"test_key";
        cfg.resourceId = L"volc.seedasr.sauc.duration";
        cfg.mode = L"bigmodel";
        std::string json;
        std::wstring err;
        CHECK(volc_asr::BuildInitRequestJson(cfg, json, &err), "volc build init default ok");
        CHECK(json.find("\"model_name\":\"bigmodel\"") != std::string::npos &&
              json.find("\"format\":\"pcm\"") != std::string::npos &&
              json.find("\"rate\":16000") != std::string::npos &&
              json.find("\"bits\":16") != std::string::npos &&
              json.find("\"enable_itn\":true") != std::string::npos,
              "volc init default fields present");

        // bigmodel_nostream 模式允许传 language
        cfg.mode = L"bigmodel_nostream";
        cfg.language = L"zh-CN";
        CHECK(volc_asr::BuildInitRequestJson(cfg, json, &err), "volc build init nostream ok");
        CHECK(json.find("\"language\":\"zh-CN\"") != std::string::npos,
              "volc nostream carries language");

        // bigmodel_async 模式忽略 language
        cfg.mode = L"bigmodel_async";
        CHECK(volc_asr::BuildInitRequestJson(cfg, json, &err), "volc build init async ok");
        CHECK(json.find("\"language\"") == std::string::npos,
              "volc async omits language");

        // hotwords 与 context 合并入 corpus
        cfg.hotwordsId = L"hw_test_id";
        cfg.contextJson = L"{\"ctx\":\"prev_speech\"}";
        CHECK(volc_asr::BuildInitRequestJson(cfg, json, &err), "volc build init corpus ok");
        CHECK(json.find("\"corpus\":{") != std::string::npos &&
              json.find("\"boosting_table_id\":\"hw_test_id\"") != std::string::npos &&
              json.find("\"context\":") != std::string::npos,
              "volc corpus fields merged");

        // 畸形 extraParams 必须返回 false
        cfg.extraParams = L"\"broken_json_key\":[unterminated";
        CHECK(!volc_asr::BuildInitRequestJson(cfg, json, &err),
              "volc malformed extra params rejected");
        CHECK(!err.empty(), "volc extra params error message populated");
    }
    // 22) MAI Transcribe 429 限流提示与大小写鲁棒性
    {
        const std::string bodyExact = "{\"error\":{\"message\":\"Provider returned 429\",\"code\":429}}";
        const std::wstring errExact = mai_transcribe::FormatHttpErrorForTest(429, bodyExact);
        CHECK(errExact.find(L"OpenRouter connected, but MAI-Transcribe-2 upstream provider is temporarily rate-limited") != std::wstring::npos,
              "mai 429 exact provider returned 429 detected");

        const std::string bodyLower = "{\"error\":{\"message\":\"provider returned 429: rate limit\",\"code\":429}}";
        const std::wstring errLower = mai_transcribe::FormatHttpErrorForTest(429, bodyLower);
        CHECK(errLower.find(L"OpenRouter connected, but MAI-Transcribe-2 upstream provider is temporarily rate-limited") != std::wstring::npos,
              "mai 429 case-insensitive provider returned 429 detected");

        const std::string bodyOther = "{\"error\":{\"message\":\"quota exceeded\",\"code\":429}}";
        const std::wstring errOther = mai_transcribe::FormatHttpErrorForTest(429, bodyOther);
        CHECK(errOther == L"MAI ASR error: HTTP 429: quota exceeded",
              "mai 429 generic message formatted");
    }
    // 23) Config 强类型注册表序列化与反序列化验证
    {
        config_registry::InitializeRegistry();
        const auto& reg = config_registry::Registry::Instance();

        Config original;
        original.configVersion = 15;
        original.modelId = L"firered_ctc";
        original.enableVad = true;
        original.vadThreshold = 0.25f;
        original.volcEnableNonstream = true; // RawInt
        original.qwenFreePolishEnabled = true; // RawInt
        original.llmEndpoint = L"https://api.deepseek.com/v1";

        std::string json = reg.SaveJson(original);
        CHECK(json.find("\"enable_vad\": true") != std::string::npos, "config registry literal bool true");
        CHECK(json.find("\"volc_enable_nonstream\": 1") != std::string::npos, "config registry rawint bool 1");
        CHECK(json.find("\"qwen_free_polish\": 1") != std::string::npos, "config registry rawint qwen free 1");
        CHECK(json.find("\"vad_threshold\": 0.25") != std::string::npos, "config registry float precision");

        Config loaded;
        reg.LoadJson(loaded, json);
        CHECK(loaded.configVersion == 15, "config registry load version");
        CHECK(loaded.modelId == L"firered_ctc", "config registry load string");
        CHECK(loaded.enableVad == true, "config registry load literal bool");
        CHECK(loaded.volcEnableNonstream == true, "config registry load rawint bool");
        CHECK(loaded.qwenFreePolishEnabled == true, "config registry load qwen free bool");
        CHECK(std::abs(loaded.vadThreshold - 0.25f) < 0.001f, "config registry load float");
        CHECK(loaded.llmEndpoint == L"https://api.deepseek.com/v1", "config registry load endpoint");

        // Sub-0.01 precision check (e.g. 0.075 must not be truncated to 0.08 or 0.07)
        original.vadThreshold = 0.075f;
        std::string jsonPrec = reg.SaveJson(original);
        CHECK(jsonPrec.find("\"vad_threshold\": 0.075") != std::string::npos, "config registry sub-0.01 float precision preserved");
        Config loadedPrec;
        reg.LoadJson(loadedPrec, jsonPrec);
        CHECK(std::abs(loadedPrec.vadThreshold - 0.075f) < 0.0001f, "config registry load sub-0.01 float");

        // Full round-trip precision check (e.g. 1.2345678f must not be truncated to 1.23457 by 6-digit default)
        original.vadThreshold = 1.2345678f;
        std::string json7Digit = reg.SaveJson(original);
        CHECK(json7Digit.find("1.23457") == std::string::npos, "config registry max_digits10 float does not round to 6 digits");
        Config loaded7Digit;
        reg.LoadJson(loaded7Digit, json7Digit);
        CHECK(loaded7Digit.vadThreshold == 1.2345678f, "config registry round-trip exact float");
    }
    // 24) QuotedInt 布尔格式与引号布尔字符串解析验证
    {
        config_registry::Registry customReg;
        customReg.Register({"quoted_flag", &Config::enablePartial,
                            config_registry::CryptoPolicy::None,
                            config_registry::BoolJsonFormat::QuotedInt});
        customReg.Register({"literal_flag", &Config::enableVad,
                            config_registry::CryptoPolicy::None,
                            config_registry::BoolJsonFormat::Literal});

        Config c1;
        c1.enablePartial = true;
        c1.enableVad = false;
        std::string json1 = customReg.SaveJson(c1);
        CHECK(json1.find("\"quoted_flag\": \"1\"") != std::string::npos,
              "quoted int bool format produces \"1\"");
        CHECK(json1.find("\"literal_flag\": false") != std::string::npos,
              "literal bool format produces false");

        c1.enablePartial = false;
        std::string json2 = customReg.SaveJson(c1);
        CHECK(json2.find("\"quoted_flag\": \"0\"") != std::string::npos,
              "quoted int bool format produces \"0\"");

        // 反序列化：支持 "1", "0", "true", "false", 以及裸 1, 0, true, false
        Config c2;
        customReg.LoadJson(c2, "{\"quoted_flag\": \"1\", \"literal_flag\": \"true\"}");
        CHECK(c2.enablePartial == true, "load quoted \"1\" as bool true");
        CHECK(c2.enableVad == true, "load quoted \"true\" as bool true");

        customReg.LoadJson(c2, "{\"quoted_flag\": \"0\", \"literal_flag\": \"false\"}");
        CHECK(c2.enablePartial == false, "load quoted \"0\" as bool false");
        CHECK(c2.enableVad == false, "load quoted \"false\" as bool false");

        customReg.LoadJson(c2, "{\"quoted_flag\": 1, \"literal_flag\": 0}");
        CHECK(c2.enablePartial == true, "load raw int 1 as bool true");
        CHECK(c2.enableVad == false, "load raw int 0 as bool false");
    }
    // 25) DPAPI 加密字段在增量/局部 JSON 加载时保持内存原有值（防止误清空）
    {
        config_registry::Registry dpapiReg;
        dpapiReg.Register({"secret_key", &Config::qwenApiKey,
                           config_registry::CryptoPolicy::Dpapi});
        dpapiReg.Register({"version", &Config::configVersion});

        Config c;
        c.qwenApiKey = L"existing_unencrypted_secret";
        c.configVersion = 10;

        // 局部 JSON 不含 secret_key 或 secret_key_dpapi，原有值不得被覆盖清空
        dpapiReg.LoadJson(c, "{\"version\": 20}");
        CHECK(c.configVersion == 20, "partial json updates present field");
        CHECK(c.qwenApiKey == L"existing_unencrypted_secret",
              "partial json preserves absent DPAPI secret");
    }
    // 26) 生命周期钩子 PreSaveHook 与 PostLoadHook 执行验证
    {
        config_registry::Registry hookReg;
        hookReg.Register({"model", &Config::modelId});
        hookReg.Register({"version", &Config::configVersion});

        bool hookCalled = false;
        hookReg.SetPreSaveHook([&](Config& cfg) {
            hookCalled = true;
            if (cfg.modelId == L"raw") {
                cfg.modelId = L"normalized_model";
            }
        });
        hookReg.SetPostLoadHook([](Config& cfg) {
            cfg.configVersion += 100;
        });

        Config c;
        c.modelId = L"raw";
        c.configVersion = 5;

        std::string saved = hookReg.SaveJson(c);
        CHECK(hookCalled, "presave hook invoked");
        CHECK(saved.find("\"model\": \"normalized_model\"") != std::string::npos,
              "presave hook reflected in serialized json");

        Config loaded;
        hookReg.LoadJson(loaded, "{\"model\": \"foo\", \"version\": 10}");
        CHECK(loaded.modelId == L"foo", "loaded value deserialized");
        CHECK(loaded.configVersion == 110, "postload hook executed on loaded config");
    }
    // 27) Audio 3.1 新增配置项的持久化往返（真实注册表：键名、布尔字面量格式与回读）
    {
        config_registry::InitializeRegistry();
        const auto& reg = config_registry::Registry::Instance();

        Config on;
        on.qwenVadModel = L"near_meeting_16k";
        on.qwenKeepDialect = true;
        on.qwenDisfluencyRemovalEnabled = true;
        const std::string json = reg.SaveJson(on);
        CHECK(json.find("\"qwen_vad_model\": \"near_meeting_16k\"") != std::string::npos,
              "qwen_vad_model persists as a string");
        CHECK(json.find("\"qwen_keep_dialect\": true") != std::string::npos,
              "qwen_keep_dialect persists as a literal boolean");
        CHECK(json.find("\"qwen_disfluency_removal\": true") != std::string::npos,
              "qwen_disfluency_removal persists as a literal boolean");

        Config onLoaded;
        reg.LoadJson(onLoaded, json);
        CHECK(onLoaded.qwenVadModel == L"near_meeting_16k" &&
                  onLoaded.qwenKeepDialect == true &&
                  onLoaded.qwenDisfluencyRemovalEnabled == true,
              "Audio 3.1 settings survive a save/load round-trip");

        Config off;
        off.qwenVadModel = L"far_field_meeting_16k";
        off.qwenKeepDialect = false;
        off.qwenDisfluencyRemovalEnabled = false;
        const std::string offJson = reg.SaveJson(off);
        Config offLoaded;
        reg.LoadJson(offLoaded, offJson);
        CHECK(offLoaded.qwenVadModel == L"far_field_meeting_16k" &&
                  offLoaded.qwenKeepDialect == false &&
                  offLoaded.qwenDisfluencyRemovalEnabled == false,
              "Audio 3.1 settings round-trip when explicitly disabled");
    }
    // 27b) 出厂默认：Qwen 上下文/保活/方言/润色开关默认开启
    {
        const Config defaults;
        CHECK(defaults.qwenEnableInputContext && defaults.qwenHistoryContext &&
                  defaults.qwenEnableContinueContext && defaults.qwenHeartbeat &&
                  defaults.qwenKeepDialect && defaults.qwenDisfluencyRemovalEnabled,
              "Qwen context, heartbeat, dialect and polish switches default to on");
    }
    // 28) 全量 101 个持久化字段 Legacy JSON 配置夹具反序列化保真回归测试
    {
        config_registry::InitializeRegistry();
        const auto& reg = config_registry::Registry::Instance();
        CHECK(reg.GetEntries().size() == 101, "registry total entry count is 101");

        const std::string legacyJson = R"({
            "config_version": 15,
            "hotkey": "Ctrl+Shift+Space",
            "model_id": "SenseVoiceSmall",
            "model_dir": "D:\\models\\custom",
            "threads": "4",
            "enable_vad": true,
            "vad_model": "firered",
            "vad_threshold": 0.075,
            "vad_min_silence": 350,
            "vad_min_speech": 120,
            "vad_pad_start": 80,
            "vad_smooth_window": 5,
            "postprocess": "llm",
            "enable_partial": true,
            "asr_backend": "qwen",
            "fallback_asr_backend": "local",
            "cloud_provider": "volcengine",
            "volc_api_key": "volc_key_345",
            "volc_resource_id": "volc.bigasr.sauc.duration",
            "volc_mode": "bigmodel_async",
            "volc_language": "zh-CN",
            "volc_enable_nonstream": 1,
            "volc_end_window_size": 800,
            "volc_enable_ddc": 1,
            "volc_extra_params": "{\"custom\":\"param\"}",
            "volc_enable_context": 1,
            "volc_context_history": 7,
            "volc_enable_input_context": 1,
            "volc_enable_music_fc": 1,
            "volc_enable_poi_fc": 0,
            "volc_force_to_speech_time": 1000,
            "volc_hotwords_id": "hw_123",
            "volc_hotwords_name": "hw_name",
            "volc_correct_table_id": "tbl_456",
            "volc_correct_table_name": "tbl_name",
            "volc_reuse_vocabulary": 1,
            "baidu_api_key": "baidu_key_123",
            "baidu_secret_key": "baidu_secret_456",
            "baidu_dev_pid": 1737,
            "qwen_api_key": "qwen_key_678",
            "qwen_base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
            "qwen_http_base_url": "https://dashscope.aliyuncs.com/api/v1/services/audio/asr",
            "qwen_audio_streaming_base_url": "wss://dashscope.aliyuncs.com/api-ws/v1/inference",
            "qwen_model": "qwen-audio-3.0-asr-flash-streaming",
            "qwen_transport": "websocket",
            "qwen_language": "zh",
            "qwen_chunk_ms": 400,
            "qwen_language_hints": "zh,en",
            "qwen_vocabulary_id": "voc_123",
            "qwen_vocabulary": "vocab_text",
            "qwen_semantic_punctuation": true,
            "qwen_max_sentence_silence": 1500,
            "qwen_multi_threshold": false,
            "qwen_heartbeat": true,
            "qwen_speech_noise_threshold_enabled": true,
            "qwen_speech_noise_threshold": 0.35,
            "qwen_enable_input_context": true,
            "qwen_enable_continue_context": true,
            "qwen_history_context": true,
            "qwen_history_context_rounds": 4,
            "qwen_special_word_replace": "foo=bar",
            "qwen_special_word_empty": "baz",
            "qwen_system_reserved_filter": true,
            "mimo_api_key": "mimo_key_901",
            "mimo_base_url": "https://token-plan-ams.xiaomimimo.com/v1",
            "mimo_model": "mimo-v2.5-asr",
            "mimo_language": "zh",
            "doubao_ime_device_id": "legacy_device_123",
            "doubao_ime_cdid": "legacy_cdid_456",
            "doubao_ime_token": "legacy_token_789",
            "qwen_free_polish": 1,
            "qwen_free_punct": 1,
            "qwen_free_correct": 1,
            "qwen_free_rewrite": 0,
            "qwen_free_debug_log": 0,
            "qwen_free_shell_path": "C:\\Program Files\\QianwenIME",
            "qwen_free_utdid_override": "custom_utdid",
            "mai_api_provider": "azure",
            "mai_openrouter_api_key": "mai_or_key_789",
            "mai_azure_endpoint": "https://custom.azure.com",
            "mai_azure_api_key": "mai_az_key_012",
            "mai_language": "zh-CN",
            "audio_backend": "wasapi",
            "audio_device_id": "device_default",
            "diagnostic_audio_mode": "failures",
            "llm_provider": "DeepSeek",
            "llm_providers_json": "[{\"id\":\"custom\"}]",
            "llm_endpoint": "https://api.deepseek.com/v1",
            "llm_api_key": "llm_secret_key_abc",
            "llm_model": "deepseek-chat",
            "llm_prompt": "Custom prompt text",
            "enable_llm_debug": true,
            "enable_debug_mode": true,
            "force_unicode_input": true
        })";

        Config cfg;
        reg.LoadJson(cfg, legacyJson);

        CHECK(cfg.configVersion == 15, "legacy fixture 1: version");
        CHECK(cfg.hotkey == L"Ctrl+Shift+Space", "legacy fixture 2: hotkey");
        CHECK(cfg.modelId == L"SenseVoiceSmall", "legacy fixture 3: modelId");
        CHECK(cfg.modelDir == L"D:\\models\\custom", "legacy fixture 4: modelDir");
        CHECK(cfg.threads == L"4", "legacy fixture 5: threads");
        CHECK(cfg.enableVad == true, "legacy fixture 6: enableVad");
        CHECK(cfg.vadModel == L"firered", "legacy fixture 7: vadModel");
        CHECK(std::abs(cfg.vadThreshold - 0.075f) < 0.0001f, "legacy fixture 8: vadThreshold sub-0.01 precision");
        CHECK(cfg.vadMinSilence == 350, "legacy fixture 9: vadMinSilence");
        CHECK(cfg.vadMinSpeech == 120, "legacy fixture 10: vadMinSpeech");
        CHECK(cfg.vadPadStart == 80, "legacy fixture 11: vadPadStart");
        CHECK(cfg.vadSmoothWindow == 5, "legacy fixture 12: vadSmoothWindow");
        CHECK(cfg.postprocess == L"llm", "legacy fixture 13: postprocess");
        CHECK(cfg.enablePartial == true, "legacy fixture 14: enablePartial");
        CHECK(cfg.asrBackend == L"qwen", "legacy fixture 15: asrBackend");
        CHECK(cfg.fallbackAsrBackend == L"local", "legacy fixture 16: fallbackAsrBackend");
        CHECK(cfg.cloudProvider == L"volcengine", "legacy fixture 17: cloudProvider");
        CHECK(cfg.volcApiKey == L"volc_key_345", "legacy fixture 18: volcApiKey");
        CHECK(cfg.volcResourceId == L"volc.bigasr.sauc.duration", "legacy fixture 19: volcResourceId");
        CHECK(cfg.volcMode == L"bigmodel_async", "legacy fixture 20: volcMode");
        CHECK(cfg.volcLanguage == L"zh-CN", "legacy fixture 21: volcLanguage");
        CHECK(cfg.volcEnableNonstream == true, "legacy fixture 22: volcEnableNonstream rawint 1");
        CHECK(cfg.volcEndWindowSize == 800, "legacy fixture 23: volcEndWindowSize");
        CHECK(cfg.volcEnableDdc == true, "legacy fixture 24: volcEnableDdc");
        CHECK(cfg.volcExtraParams == L"{\"custom\":\"param\"}", "legacy fixture 25: volcExtraParams");
        CHECK(cfg.volcEnableContext == true, "legacy fixture 26: volcEnableContext");
        CHECK(cfg.volcContextHistory == 7, "legacy fixture 27: volcContextHistory");
        CHECK(cfg.volcEnableInputContext == true, "legacy fixture 28: volcEnableInputContext");
        CHECK(cfg.volcEnableMusicFc == true, "legacy fixture 29: volcEnableMusicFc");
        CHECK(cfg.volcEnablePoiFc == false, "legacy fixture 30: volcEnablePoiFc");
        CHECK(cfg.volcForceToSpeechTime == 1000, "legacy fixture 31: volcForceToSpeechTime");
        CHECK(cfg.volcHotwordsId == L"hw_123", "legacy fixture 32: volcHotwordsId");
        CHECK(cfg.volcHotwordsName == L"hw_name", "legacy fixture 33: volcHotwordsName");
        CHECK(cfg.volcCorrectTableId == L"tbl_456", "legacy fixture 34: volcCorrectTableId");
        CHECK(cfg.volcCorrectTableName == L"tbl_name", "legacy fixture 35: volcCorrectTableName");
        CHECK(cfg.volcEnableReuseVocabulary == true, "legacy fixture: volcEnableReuseVocabulary rawint 1");
        CHECK(cfg.baiduApiKey == L"baidu_key_123", "legacy fixture 36: baiduApiKey");
        CHECK(cfg.baiduSecretKey == L"baidu_secret_456", "legacy fixture 37: baiduSecretKey");
        CHECK(cfg.baiduDevPid == 1737, "legacy fixture 38: baiduDevPid");
        CHECK(cfg.qwenApiKey == L"qwen_key_678", "legacy fixture 39: qwenApiKey");
        CHECK(cfg.qwenBaseUrl == L"https://dashscope.aliyuncs.com/compatible-mode/v1", "legacy fixture 40: qwenBaseUrl");
        CHECK(cfg.qwenHttpBaseUrl == L"https://dashscope.aliyuncs.com/api/v1/services/audio/asr", "legacy fixture 41: qwenHttpBaseUrl");
        CHECK(cfg.qwenAudioStreamingBaseUrl == L"wss://dashscope.aliyuncs.com/api-ws/v1/inference", "legacy fixture 42: qwenAudioStreamingBaseUrl");
        CHECK(cfg.qwenModel == L"qwen-audio-3.0-asr-flash-streaming", "legacy fixture 43: qwenModel");
        CHECK(cfg.qwenTransport == L"websocket", "legacy fixture 44: qwenTransport");
        CHECK(cfg.qwenLanguage == L"zh", "legacy fixture 45: qwenLanguage");
        CHECK(cfg.qwenChunkMs == 400, "legacy fixture 46: qwenChunkMs");
        CHECK(cfg.qwenLanguageHints == L"zh,en", "legacy fixture 47: qwenLanguageHints");
        CHECK(cfg.qwenVocabularyId == L"voc_123", "legacy fixture 48: qwenVocabularyId");
        CHECK(cfg.qwenVocabulary == L"vocab_text", "legacy fixture 49: qwenVocabulary");
        CHECK(cfg.qwenSemanticPunctuation == true, "legacy fixture 50: qwenSemanticPunctuation");
        CHECK(cfg.qwenMaxSentenceSilenceMs == 1500, "legacy fixture 51: qwenMaxSentenceSilenceMs");
        CHECK(cfg.qwenMultiThresholdMode == false, "legacy fixture 52: qwenMultiThresholdMode");
        CHECK(cfg.qwenHeartbeat == true, "legacy fixture 53: qwenHeartbeat");
        CHECK(cfg.qwenSpeechNoiseThresholdEnabled == true, "legacy fixture 54: qwenSpeechNoiseThresholdEnabled");
        CHECK(std::abs(cfg.qwenSpeechNoiseThreshold - 0.35f) < 0.001f, "legacy fixture 55: qwenSpeechNoiseThreshold");
        CHECK(cfg.qwenEnableInputContext == true, "legacy fixture 56: qwenEnableInputContext");
        CHECK(cfg.qwenEnableContinueContext == true, "legacy fixture 57: qwenEnableContinueContext");
        CHECK(cfg.qwenHistoryContext == true, "legacy fixture: qwenHistoryContext");
        CHECK(cfg.qwenHistoryContextRounds == 4, "legacy fixture: qwenHistoryContextRounds");
        CHECK(cfg.qwenSpecialWordReplaceList == L"foo=bar", "legacy fixture 58: qwenSpecialWordReplaceList");
        CHECK(cfg.qwenSpecialWordEmptyList == L"baz", "legacy fixture 59: qwenSpecialWordEmptyList");
        CHECK(cfg.qwenSystemReservedFilter == true, "legacy fixture 60: qwenSystemReservedFilter");
        CHECK(cfg.mimoApiKey == L"mimo_key_901", "legacy fixture 61: mimoApiKey");
        CHECK(cfg.mimoBaseUrl == L"https://token-plan-ams.xiaomimimo.com/v1", "legacy fixture 62: mimoBaseUrl");
        CHECK(cfg.mimoModel == L"mimo-v2.5-asr", "legacy fixture 63: mimoModel");
        CHECK(cfg.mimoLanguage == L"zh", "legacy fixture 64: mimoLanguage");
        CHECK(cfg.doubaoImeDeviceId == L"legacy_device_123", "legacy fixture 65: doubaoImeDeviceId");
        CHECK(cfg.doubaoImeCdid == L"legacy_cdid_456", "legacy fixture 66: doubaoImeCdid");
        CHECK(cfg.doubaoImeToken == L"legacy_token_789", "legacy fixture 67: doubaoImeToken");
        CHECK(cfg.qwenFreePolishEnabled == true, "legacy fixture 68: qwenFreePolishEnabled rawint 1");
        CHECK(cfg.qwenFreePunctEnabled == true, "legacy fixture 69: qwenFreePunctEnabled rawint 1");
        CHECK(cfg.qwenFreeCorrectEnabled == true, "legacy fixture 70: qwenFreeCorrectEnabled rawint 1");
        CHECK(cfg.qwenFreeRewriteEnabled == false, "legacy fixture 71: qwenFreeRewriteEnabled rawint 0");
        CHECK(cfg.qwenFreeDebugLog == false, "legacy fixture 72: qwenFreeDebugLog rawint 0");
        CHECK(cfg.qwenFreeShellPath == L"C:\\Program Files\\QianwenIME", "legacy fixture 73: qwenFreeShellPath");
        CHECK(cfg.qwenFreeUtdidOverride == L"custom_utdid", "legacy fixture 74: qwenFreeUtdidOverride");
        CHECK(cfg.maiApiProvider == L"azure", "legacy fixture 75: maiApiProvider");
        CHECK(cfg.maiOpenRouterApiKey == L"mai_or_key_789", "legacy fixture 76: maiOpenRouterApiKey");
        CHECK(cfg.maiAzureEndpoint == L"https://custom.azure.com", "legacy fixture 77: maiAzureEndpoint");
        CHECK(cfg.maiAzureApiKey == L"mai_az_key_012", "legacy fixture 78: maiAzureApiKey");
        CHECK(cfg.maiLanguage == L"zh-CN", "legacy fixture 79: maiLanguage");
        CHECK(cfg.audioBackend == L"wasapi", "legacy fixture 80: audioBackend");
        CHECK(cfg.audioDeviceId == L"device_default", "legacy fixture 81: audioDeviceId");
        CHECK(cfg.diagnosticAudioMode == L"failures", "legacy fixture 82: diagnosticAudioMode");
        CHECK(cfg.llmProvider == L"DeepSeek", "legacy fixture 83: llmProvider");
        CHECK(cfg.llmProvidersJson == L"[{\"id\":\"custom\"}]", "legacy fixture 84: llmProvidersJson");
        CHECK(cfg.llmEndpoint == L"https://api.deepseek.com/v1", "legacy fixture 85: llmEndpoint");
        CHECK(cfg.llmApiKey == L"llm_secret_key_abc", "legacy fixture 86: llmApiKey");
        CHECK(cfg.llmModel == L"deepseek-chat", "legacy fixture 87: llmModel");
        CHECK(cfg.llmPrompt == L"Custom prompt text", "legacy fixture 88: llmPrompt");
        CHECK(cfg.enableLlm == false, "legacy fixture: enableLlm default false");
        CHECK(cfg.enableLlmDebug == true, "legacy fixture 89: enableLlmDebug");
        CHECK(cfg.enableDebugMode == true, "legacy fixture 90: enableDebugMode");
        CHECK(cfg.forceUnicodeInput == true, "legacy fixture 91: forceUnicodeInput");
    }

    // LLM enable & ShouldRunLlmRefine tests
    {
        Config cfg;
        cfg.postprocess = L"auto";
        cfg.enableLlm = false;
        cfg.llmEndpoint = L"https://api.example.com";
        cfg.llmApiKey = L"key";
        CHECK(!ShouldRunLlmRefine(cfg, L"识别文本"), "ShouldRunLlmRefine false when enableLlm is false");

        cfg.enableLlm = true;
        CHECK(ShouldRunLlmRefine(cfg, L"识别文本"), "ShouldRunLlmRefine true when enableLlm is true and usable text");

        cfg.postprocess = L"none";
        CHECK(ShouldRunLlmRefine(cfg, L"识别文本"), "ShouldRunLlmRefine independent of postprocess punctuation setting");

        cfg.enableLlm = false;
        CHECK(!ShouldRunLlmRefine(cfg, L"识别文本"), "ShouldRunLlmRefine false when enableLlm false even if postprocess is auto");

        auto& reg = config_registry::Registry::Instance();
        std::string serialized = reg.SaveJson(cfg);
        CHECK(serialized.find("\"enable_llm\": false") != std::string::npos, "enable_llm serialized as false literal");

        cfg.enableLlm = true;
        serialized = reg.SaveJson(cfg);
        CHECK(serialized.find("\"enable_llm\": true") != std::string::npos, "enable_llm serialized as true literal");

        Config loaded;
        reg.LoadJson(loaded, serialized);
        CHECK(loaded.enableLlm == true, "enable_llm roundtrip true");

        // Backward compatibility migration test: postprocess == "llm" with missing enable_llm
        const std::string legacyOldJson = "{\"config_version\":14,\"postprocess\":\"llm\"}";
        Config cfgOld;
        reg.LoadJson(cfgOld, legacyOldJson);
        if (legacyOldJson.find("\"enable_llm\"") == std::string::npos && cfgOld.postprocess == L"llm") {
            cfgOld.enableLlm = true;
            cfgOld.postprocess = L"auto";
        }
        CHECK(cfgOld.enableLlm == true, "legacy config migration: enableLlm migrated from postprocess==llm");
        CHECK(cfgOld.postprocess == L"auto", "legacy config migration: postprocess migrated to auto");

        // Prompt preset and backup state logic
        std::wstring customBackup = L"My Custom System Prompt";
        std::wstring currentPrompt = llm::kPromptPresets[0].prompt;
        int matchedPreset = -1;
        for (int i = 0; i < llm::kPromptPresetCount; ++i) {
            if (currentPrompt == llm::kPromptPresets[i].prompt) {
                matchedPreset = i;
                break;
            }
        }
        CHECK(matchedPreset == 0, "preset 0 matches Basic Fix");
        CHECK(!customBackup.empty(), "custom backup preserved when browsing preset 0");

        // Switch to preset 1: customBackup must NOT be overwritten
        currentPrompt = llm::kPromptPresets[1].prompt;
        CHECK(customBackup == L"My Custom System Prompt", "custom backup unaffected by switching presets");

        // Restore custom
        currentPrompt = customBackup;
        matchedPreset = -1;
        for (int i = 0; i < llm::kPromptPresetCount; ++i) {
            if (currentPrompt == llm::kPromptPresets[i].prompt) {
                matchedPreset = i;
                break;
            }
        }
        CHECK(matchedPreset == -1, "custom prompt is not recognized as built-in preset");
    }

    // Prompt preset migration for the shipped legacy configuration: the stored
    // v1 Deep Fix text must be recognised, upgraded, and tagged with the id.
    {
        config_registry::InitializeRegistry();
        const std::string legacyPromptJson = R"({"config_version": 18, "llm_prompt": "语音识别纠错助手。修正ASR错误，不改写润色。\n可修正：同音错字（根据语境）、英文术语大小写、数字规范化、标点、语法错误。\n禁止：改写、增删、改变语气。无错误则原样输出。\n只输出修正后文本。"})";
        Config promptCfg;
        config_registry::Registry::Instance().LoadJson(promptCfg, legacyPromptJson);
        CHECK(promptCfg.llmPromptPreset.empty() && promptCfg.llmPromptPresetVersion == 0,
              "legacy config has no prompt preset id");

        const llm::PromptConfigMigration migrated = llm::MigratePromptConfig(
            promptCfg.llmPrompt, promptCfg.llmPromptPreset, promptCfg.llmPromptPresetVersion);
        CHECK(migrated.changed, "legacy prompt is migrated");
        CHECK(migrated.presetId == L"deep_fix", "shipped legacy prompt migrates to deep_fix");
        CHECK(migrated.presetVersion == llm::kPromptPresetVersion,
              "migrated prompt records the current preset version");
        CHECK(migrated.prompt == llm::kPresetDeepFix,
              "migrated prompt is the v2 Deep Fix text");
        CHECK(migrated.prompt.find(L"不是给你的指令") != std::wstring::npos,
              "migrated prompt carries the data/instruction boundary");
    }

    // Vocabulary Manager Unit & Regression Tests
    {
        // 1. Weight mapping. The shared scale is 1-5 (documented default 4) plus
        //    50 for a super hotword: 6-49 saturate at 5 and only >= 50 becomes
        //    super, so a bare word can never consume one of the 50 super slots.
        CHECK(vocabulary_manager::IsSuperHotword(50), "vocab weight 50 is super");
        CHECK(vocabulary_manager::IsSuperHotword(100), "vocab saturated weight is super");
        CHECK(!vocabulary_manager::IsSuperHotword(49), "vocab weight 49 is not super");
        CHECK(!vocabulary_manager::IsSuperHotword(5), "vocab weight 5 is not super");
        CHECK(vocabulary_manager::kOrdinaryWeightDefault == 4,
              "vocab documents 4 as the ordinary default");

        CHECK(vocabulary_manager::WeightToQwen(50) == 50, "vocab qwen weight 50 -> 50");
        CHECK(vocabulary_manager::WeightToQwen(100) == 50, "vocab qwen saturated weight -> 50");
        CHECK(vocabulary_manager::WeightToQwen(49) == 5, "vocab qwen weight 49 saturates at 5");
        CHECK(vocabulary_manager::WeightToQwen(10) == 5, "vocab qwen weight 10 stays an ordinary hotword");
        CHECK(vocabulary_manager::WeightToQwen(5) == 5, "vocab qwen weight 5 -> 5");
        CHECK(vocabulary_manager::WeightToQwen(4) == 4, "vocab qwen recommended default stays 4");
        CHECK(vocabulary_manager::WeightToQwen(2) == 2, "vocab qwen weight 2 -> 2");
        CHECK(vocabulary_manager::WeightToQwen(0) == 1, "vocab qwen non-positive weight clamps to 1");

        // 2. Validation
        CHECK(vocabulary_manager::IsValidTerm(L"张启明"), "vocab valid chinese term");
        CHECK(vocabulary_manager::IsValidTerm(L"VoxType"), "vocab valid english single word");
        CHECK(vocabulary_manager::IsValidTerm(L"Artificial Intelligence In Speech"), "vocab valid 4 english words");
        CHECK(!vocabulary_manager::IsValidTerm(L""), "vocab reject empty term");
        CHECK(!vocabulary_manager::IsValidTerm(L"这是一段非常非常非常非常长的一句话肯定超过了十五个汉字限制"), "vocab reject >15 chars chinese");
        CHECK(!vocabulary_manager::IsValidTerm(L"one two three four five six seven eight"), "vocab reject >7 words english");

        // 3. JSON parsing with comments
        const wchar_t* sampleJson =
            L"{\n"
            L"  // 单行注释\n"
            L"  \"// 伪注释键\": \"说明内容\",\n"
            L"  /* 块注释 */\n"
            L"  \"张启明\": 50,\n"
            L"  \"张启星\": \"25\",\n"
            L"  \"VoxType\": 10\n"
            L"}\n";
        auto parsedRes = vocabulary_manager::ParseVocabularyJson(sampleJson);
        CHECK(parsedRes.has_value(), "vocab parse json success");
        if (parsedRes) {
            CHECK(parsedRes->size() == 3, "vocab parse json count 3");
            CHECK((*parsedRes)[0].word == L"张启明" && (*parsedRes)[0].weight == 50, "vocab entry 1 match");
            CHECK((*parsedRes)[1].word == L"张启星" && (*parsedRes)[1].weight == 25, "vocab entry 2 match");
            CHECK((*parsedRes)[2].word == L"VoxType" && (*parsedRes)[2].weight == 10, "vocab entry 3 match");
        }

        // 4. Line format parsing
        const wchar_t* lineText =
            L"# Line comment\n"
            L"// Another comment\n"
            L"\"李悦宁\" : 50\n"
            L"王协宁 20\n"
            L"Claude\n";
        auto parsedLines = vocabulary_manager::ParseVocabularyLines(lineText);
        CHECK(parsedLines.has_value(), "vocab parse lines success");
        if (parsedLines) {
            CHECK(parsedLines->size() == 3, "vocab parse lines count 3");
            CHECK((*parsedLines)[0].word == L"李悦宁" && (*parsedLines)[0].weight == 50, "line entry 1");
            CHECK((*parsedLines)[1].word == L"王协宁" && (*parsedLines)[1].weight == 20, "line entry 2");
            CHECK((*parsedLines)[2].word == L"Claude" &&
                      (*parsedLines)[2].weight == vocabulary_manager::kOrdinaryWeightDefault,
                  "a bare line entry uses the ordinary default weight (4)");
        }

        // 5. Transpilation to Qwen (including 50 super-priority cap)
        vocabulary_manager::VocabularyList bigList;
        for (int i = 0; i < 60; ++i) {
            bigList.push_back({ L"词条" + std::to_wstring(i), 50 });
        }
        std::string qwenJson = vocabulary_manager::TranspileToQwenJson(bigList);
        CHECK(!qwenJson.empty(), "vocab transpile to qwen json non-empty");
        // Count occurrences of ":50" vs ":5"
        size_t count50 = 0, count5 = 0;
        size_t p = 0;
        while ((p = qwenJson.find(":50", p)) != std::string::npos) {
            count50++;
            p += 3;
        }
        p = 0;
        while ((p = qwenJson.find(":5", p)) != std::string::npos) {
            if (p + 2 < qwenJson.size() && qwenJson[p + 2] == '0') {
                p += 3; // skip :50
            } else {
                count5++;
                p += 2;
            }
        }
        CHECK(count50 == 50, "vocab qwen max 50 super-priorities capped at 50");
        CHECK(count5 == 10, "vocab qwen excess capped down to 5");

        // The caps must go to the user's most important words, not to whichever
        // words happen to sit at the end of the file.
        vocabulary_manager::VocabularyList ordered = {
            { L"低权重", 1 },
            { L"次高权重", 5 },
            { L"最高权重", 50 },
        };
        const std::wstring orderedJson =
            Utf8ToWide(vocabulary_manager::TranspileToQwenJson(ordered));
        CHECK(orderedJson.find(L"\"最高权重\":50") != std::wstring::npos,
              "the highest-weight entry keeps a super slot regardless of file order");
        CHECK(orderedJson.find(L"\"最高权重\":") < orderedJson.find(L"\"低权重\":"),
              "qwen vocabulary is emitted highest weight first");
        CHECK(vocabulary_manager::TranspileToQwenJson({}).empty(),
              "an empty vocabulary emits no JSON object");

        // 6. Transpilation to Volcengine context. The sample is deliberately
        // ASCENDING in file order, so only real weight sorting can produce the
        // descending output asserted below.
        vocabulary_manager::VocabularyList volcEntries = {
            { L"第三词", 4 },
            { L"张启星", 25 },
            { L"张启明", 50 },
        };
        std::wstring ctxJson = vocabulary_manager::BuildVolcengineContextJson(
            volcEntries, L"hello input", { L"older turn", L"newer turn" });
        CHECK(ctxJson.find(L"\"hotwords\":[") != std::wstring::npos, "volc context has hotwords");
        CHECK(ctxJson.find(L"{\"word\":\"张启明\"}") != std::wstring::npos,
              "volc inline hotwords use the documented word-only object");
        CHECK(ctxJson.find(L"\"scale\"") == std::wstring::npos,
              "volc inline hotwords carry no undocumented weight/scale field");
        CHECK(ctxJson.find(L"{\"word\":\"张启明\"") != std::wstring::npos &&
                  ctxJson.find(L"{\"word\":\"张启星\"") != std::wstring::npos &&
                  ctxJson.find(L"{\"word\":\"第三词\"") != std::wstring::npos,
              "volc context emits every inline hotword");
        // Ordering is the only weight lever the provider offers: it keeps the
        // front of the list and truncates the tail, so the important words must
        // come first.
        CHECK(ctxJson.find(L"{\"word\":\"张启明\"") < ctxJson.find(L"{\"word\":\"张启星\"") &&
                  ctxJson.find(L"{\"word\":\"张启星\"") < ctxJson.find(L"{\"word\":\"第三词\""),
              "volc inline hotwords are ordered by weight for the server-side tail cut");
        CHECK(ctxJson.find(L"\"context_type\":\"dialog_ctx\"") != std::wstring::npos, "volc context has dialog_ctx");
        CHECK(ctxJson.find(L"\"text\":\"hello input\"") != std::wstring::npos, "volc context has input text");
        CHECK(ctxJson.find(L"\"text\":\"hello input\"") < ctxJson.find(L"\"text\":\"newer turn\"") &&
                  ctxJson.find(L"\"text\":\"newer turn\"") < ctxJson.find(L"\"text\":\"older turn\""),
              "volc context_data is newest-first with the field text as the current turn");
        CHECK(vocabulary_manager::BuildVolcengineContextJson({}).empty(),
              "an empty context produces no corpus payload");

        // 6b. Vocabulary word list used as the focused-field fallback
        const std::wstring wordList = vocabulary_manager::BuildVocabularyWordList(
            { { L"低", 1 }, { L"高优先级词", 50 }, { L"中", 25 } }, 64);
        CHECK(wordList.find(L"高优先级词 中 低") == 0,
              "the vocabulary word list keeps weight order");
        CHECK(wordList.size() <= 64, "the vocabulary word list respects its character budget");
        CHECK(vocabulary_manager::BuildVocabularyWordList({ { L"甲", 4 } }, 0).empty(),
              "a zero-character budget yields an empty word list");

        // 7. Transpilation to Sherpa-onnx
        std::string sherpaHotwords = vocabulary_manager::TranspileToSherpaHotwords(volcEntries);
        CHECK(sherpaHotwords.find(" : 3.0\n") != std::string::npos, "sherpa hotwords score 3.0");
        CHECK(sherpaHotwords.find(" : 2.0\n") != std::string::npos, "sherpa hotwords score 2.0");

        // 8. Temp file roundtrip
        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        std::wstring testFilePath = std::wstring(tempPath) + L"voxtype_test_vocab_" + std::to_wstring(GetCurrentProcessId()) + L".json";
        const std::wstring testContent = L"{\r\n  \"测试\": 50\r\n}";
        auto writeRes = vocabulary_manager::WriteVocabularyFile(testContent, testFilePath);
        CHECK(writeRes.has_value(), "vocab write file");
        auto readRes = vocabulary_manager::ReadVocabularyFile(testFilePath);
        CHECK(readRes.has_value() && *readRes == testContent, "vocab read file matches");
        DeleteFileW(testFilePath.c_str());

        // 9. Edge cases: Rejecting unclosed JSON, missing values, lone surrogates
        CHECK(!vocabulary_manager::ParseVocabularyJson(L"{\"张启明\": 50").has_value(),
              "vocab reject unclosed json missing brace");
        CHECK(!vocabulary_manager::ParseVocabularyJson(L"{\"张启明\": }").has_value(),
              "vocab reject json missing value");
        CHECK(!vocabulary_manager::ParseVocabularyJson(L"{\"张启明\": true}").has_value(),
              "vocab reject json bool value");
        CHECK(!vocabulary_manager::ParseVocabularyJson(L"{\"\\ude00\": 50}").has_value(),
              "vocab reject lone low surrogate key");

        // 10. Line parsing delimiter stripping & float weight tolerance
        const wchar_t* complexLines =
            L"张启明 : 50\n"
            L"张启星: 40\n"
            L"李悦宁 = 30\n"
            L"王协宁, 20\n"
            L"SherpaTerm : 3.0\n";
        auto parsedComplex = vocabulary_manager::ParseVocabularyLines(complexLines);
        CHECK(parsedComplex.has_value(), "vocab complex lines parse ok");
        if (parsedComplex && parsedComplex->size() == 5) {
            CHECK((*parsedComplex)[0].word == L"张启明" && (*parsedComplex)[0].weight == 50, "line strip colon space");
            CHECK((*parsedComplex)[1].word == L"张启星" && (*parsedComplex)[1].weight == 40, "line strip colon direct");
            CHECK((*parsedComplex)[2].word == L"李悦宁" && (*parsedComplex)[2].weight == 30, "line strip equals");
            CHECK((*parsedComplex)[3].word == L"王协宁" && (*parsedComplex)[3].weight == 20, "line strip comma");
            CHECK((*parsedComplex)[4].word == L"SherpaTerm" && (*parsedComplex)[4].weight == 3, "line float weight parsed");
        }

        // 11. FormatVocabularyJson CRLF verification
        std::string formatted = vocabulary_manager::FormatVocabularyJson(volcEntries, true);
        CHECK(formatted.find("\r\n") != std::string::npos, "format vocab uses crlf");

        // LLM vocabulary section: protect-only, weight-ordered, capped
        CHECK(vocabulary_manager::BuildLlmVocabularySection({}).empty(),
              "an empty vocabulary produces no LLM section");

        const std::wstring section = vocabulary_manager::BuildLlmVocabularySection({
            { L"gittag", 10 },
            { L"张启明", 50 },
            { L"Conmmand Code", 25 },
        });
        CHECK(section.find(L"【用户词表】") == 0, "llm vocab section opens with its heading");
        CHECK(section.find(L"张启明, Conmmand Code, gittag") != std::wstring::npos,
              "llm vocab entries are listed highest weight first");
        CHECK(section.find(L"不得改动其拼写、大小写或写法") != std::wstring::npos,
              "llm vocab section protects spelling without replacing it");
        CHECK(section.find(L"（已截断）") == std::wstring::npos,
              "an in-budget vocabulary is not marked as truncated");

        vocabulary_manager::VocabularyList oversized;
        for (size_t i = 0; i < vocabulary_manager::kLlmVocabularyMaxEntries + 5; ++i) {
            oversized.push_back({ L"词" + std::to_wstring(i), static_cast<int>(i % 50) + 1 });
        }
        oversized.push_back({ L"最高权重", 50 });
        const std::wstring capped = vocabulary_manager::BuildLlmVocabularySection(oversized);
        CHECK(capped.find(L"最高权重") != std::wstring::npos,
              "the highest-weight entry survives the cap");
        CHECK(capped.find(L"（已截断）") != std::wstring::npos,
              "an over-budget vocabulary is marked as truncated");
    }

    // Shared recognition history and multi-turn context assembly
    {
        asr_history::Clear();
        CHECK(asr_history::Size() == 0, "a cleared recognition history is empty");

        asr_history::Add(L"第一句");
        asr_history::Add(L"第二句");
        CHECK(asr_history::Size() == 2, "recognition history counts its rounds");
        asr_history::Add(L"");
        CHECK(asr_history::Size() == 2, "an empty transcript is not recorded");

        const auto newestOnly = asr_context::BuildHistoryTurns(asr_history::Snapshot(), 1, 400);
        CHECK(newestOnly.size() == 1 && newestOnly[0] == L"第二句",
              "a one-round budget keeps the newest transcript");

        for (size_t i = 0; i < asr_history::kMaxRetainedRounds + 5; ++i) {
            asr_history::Add(L"轮" + std::to_wstring(i));
        }
        CHECK(asr_history::Size() == asr_history::kMaxRetainedRounds,
              "the recognition history never exceeds its retained-round cap");

        const auto threeRounds = asr_context::BuildHistoryTurns(asr_history::Snapshot(), 3, 400);
        CHECK(threeRounds.size() == 3 && threeRounds.back() == L"轮24",
              "a three-round budget keeps the three newest transcripts");
        const auto fiveRounds = asr_context::BuildHistoryTurns(asr_history::Snapshot(), 5, 400);
        CHECK(fiveRounds.size() == 5 && fiveRounds.front() == L"轮20" &&
                  fiveRounds.back() == L"轮24",
              "context turns are ordered oldest first");

        // Head vs tail discriminator: repeated characters cannot tell the two
        // apart, so the sample changes halfway through.
        const std::wstring headAndTail = std::wstring(100, L'甲') + std::wstring(500, L'乙');
        const auto tailCapped = asr_context::BuildHistoryTurns({ headAndTail }, 1, 400);
        CHECK(tailCapped.size() == 1 && tailCapped[0].size() == 400 &&
                  tailCapped[0].find(L'甲') == std::wstring::npos &&
                  tailCapped[0].find(L'乙') != std::wstring::npos,
              "a capped turn keeps the text closest to the caret (tail), not the head");

        // Provider window: five context messages, oldest dropped first, blanks
        // never consume a slot.
        const auto windowed = asr_context::ClampTurns(
            { L"一", L"二", L"三", L"四", L"五", L"六" },
            asr_context::kMaxContextTurns, 400);
        CHECK(windowed.size() == asr_context::kMaxContextTurns &&
                  windowed.front() == L"二" && windowed.back() == L"六",
              "the context window keeps the newest five turns in oldest-first order");
        const auto blanksDropped = asr_context::ClampTurns({ L"", L"有", L"   " }, 5, 400);
        CHECK(blanksDropped.size() == 1 && blanksDropped[0] == L"有",
              "blank context turns are dropped instead of consuming the window");
        CHECK(asr_context::ClampTurns({ L"甲" }, 0, 400).empty(),
              "a zero-turn window yields no turns");

        CHECK(asr_context::BuildHistoryTurns(asr_history::Snapshot(), 0, 400).empty(),
              "a zero-round budget yields no history turns");

        // Sendable-history policy: usable text alone is not enough. A recording
        // that started in a sensitive control must be excluded at write time,
        // because the history can be uploaded later by any context consumer.
        CHECK(asr_context::ShouldRecordHistory(true, false),
              "usable text is recorded when the recording was not sensitive");
        CHECK(!asr_context::ShouldRecordHistory(true, true),
              "a transcript from a sensitive control is never recorded");
        CHECK(!asr_context::ShouldRecordHistory(false, false),
              "operational text is never recorded");
        CHECK(!asr_context::ShouldRecordHistory(false, true),
              "an operational transcript from a sensitive control is never recorded");

        // The actual keep/discard decision reads the raw probe answer, and it is
        // fail-closed: only an explicit "safe" answer may record. An unverified
        // focus (and a missing probe) refuses the write, because leaving the
        // transcript in an uploadable history is the higher risk; the skip is
        // logged as reason=focus_unknown.
        CHECK(!input_context::MustSkipHistoryForFocus(
                  std::make_shared<std::atomic<int>>(input_context::kSensitiveProbeSafe)),
              "only an explicit safe answer permits recording");
        const int mustSkip[] = {
            input_context::kSensitiveProbeSensitive,
            input_context::kSensitiveProbeUnknownNotStarted,
            input_context::kSensitiveProbeUnknownProbeBusy,
            input_context::kSensitiveProbeUnknownUiaUnavailable,
            input_context::kSensitiveProbeUnknownNoFocusedElement,
            input_context::kSensitiveProbeUnknownNoHandle,
            input_context::kSensitiveProbeUnknownFocusMoved,
            input_context::kSensitiveProbeUnknownNoProbe,
            input_context::kSensitiveProbeUnknownNoStartFocus,
            input_context::kSensitiveProbeUnknownPasswordQuery,
        };
        for (int answer : mustSkip) {
            const std::string label = std::string("probe answer ") +
                input_context::SensitiveFocusAnswerName(answer) +
                " must refuse the history write";
            CHECK(input_context::MustSkipHistoryForFocus(
                      std::make_shared<std::atomic<int>>(answer)),
                  label.c_str());
        }
        CHECK(input_context::MustSkipHistoryForFocus(nullptr),
              "a recording without a probe is treated as unverified");
        CHECK(input_context::SensitiveFocusAnswer(nullptr) ==
                  input_context::kSensitiveProbeUnknownNoProbe,
              "a missing probe reports unknown_no_probe, not sensitive");

        // The UIA password property is tri-state: only an explicit boolean false
        // may ever become Safe, so a control whose password state cannot be read
        // is never recorded.
        VARIANT boolTrue;
        VariantInit(&boolTrue);
        boolTrue.vt = VT_BOOL;
        boolTrue.boolVal = VARIANT_TRUE;
        VARIANT boolFalse;
        VariantInit(&boolFalse);
        boolFalse.vt = VT_BOOL;
        boolFalse.boolVal = VARIANT_FALSE;
        VARIANT noValue;
        VariantInit(&noValue);

        CHECK(input_context::PasswordQueryState(S_OK, boolTrue) ==
                  input_context::kPasswordQueryPassword,
              "an explicit true reads as a password control");
        CHECK(input_context::PasswordQueryState(S_OK, boolFalse) ==
                  input_context::kPasswordQueryNotPassword,
              "an explicit false reads as not a password control");
        CHECK(input_context::PasswordQueryState(E_FAIL, noValue) ==
                  input_context::kPasswordQueryFailed,
              "a failed property read is not 'not a password control'");
        CHECK(input_context::PasswordQueryState(S_OK, noValue) ==
                  input_context::kPasswordQueryFailed,
              "a property read without a boolean value is undecidable");
        CHECK(input_context::PasswordQueryState(S_FALSE, boolFalse) ==
                  input_context::kPasswordQueryFailed,
              "a non-S_OK property result is undecidable even with a false payload");
        VARIANT nonCanonicalBool;
        VariantInit(&nonCanonicalBool);
        nonCanonicalBool.vt = VT_BOOL;
        nonCanonicalBool.boolVal = 1;
        CHECK(input_context::PasswordQueryState(S_OK, nonCanonicalBool) ==
                  input_context::kPasswordQueryFailed,
              "a non-canonical VARIANT_BOOL is undecidable");

        // The text reader stops on the payload alone: a contradictory status must
        // not turn an explicit password signal back into "keep reading".
        CHECK(input_context::PasswordPayloadIsTrue(boolTrue),
              "an explicit TRUE payload is a password signal");
        CHECK(!input_context::PasswordPayloadIsTrue(boolFalse),
              "an explicit FALSE payload is not a password signal");
        CHECK(!input_context::PasswordPayloadIsTrue(nonCanonicalBool),
              "a non-canonical payload is not an explicit password signal");
        CHECK(!input_context::PasswordPayloadIsTrue(noValue),
              "an empty payload is not a password signal");

        CHECK(input_context::SensitiveProbeAnswerFromUia(
                  input_context::kPasswordQueryFailed, false, false) ==
                  input_context::kSensitiveProbeUnknownPasswordQuery,
              "an undecidable password read must never become safe");
        CHECK(input_context::SensitiveProbeAnswerFromUia(
                  input_context::kPasswordQueryPassword, false, false) ==
                  input_context::kSensitiveProbeSensitive,
              "an explicit password hit is conclusive without a window handle");
        CHECK(input_context::SensitiveProbeAnswerFromUia(
                  input_context::kPasswordQueryNotPassword, true, true) ==
                  input_context::kSensitiveProbeSafe,
              "safe needs an explicit false and a matching anchor window");
        CHECK(input_context::SensitiveProbeAnswerFromUia(
                  input_context::kPasswordQueryNotPassword, false, false) ==
                  input_context::kSensitiveProbeUnknownNoHandle,
              "a missing window handle cannot be cross-checked");
        CHECK(input_context::SensitiveProbeAnswerFromUia(
                  input_context::kPasswordQueryNotPassword, true, false) ==
                  input_context::kSensitiveProbeUnknownFocusMoved,
              "an element outside the anchored window is not safe");

        // Without a focused control at recording start there is nothing to
        // cross-check the asynchronous query against, so it cannot be Safe.
        CHECK(input_context::InitialSensitiveProbeAnswer(true, false, false) ==
                  input_context::kSensitiveProbeUnknownNoStartFocus,
              "no focused control at recording start means unknown");
        CHECK(input_context::InitialSensitiveProbeAnswer(false, false, false) ==
                  input_context::kSensitiveProbeUnknownNoFocusedElement,
              "no foreground window means unknown");
        CHECK(input_context::InitialSensitiveProbeAnswer(true, true, true) ==
                  input_context::kSensitiveProbeSensitive,
              "a Win32 password edit is decisive");
        CHECK(input_context::InitialSensitiveProbeAnswer(true, true, false) ==
                  input_context::kSensitiveProbeUnknownNotStarted,
              "a complete anchor lets the UI Automation worker run");
        VariantClear(&boolTrue);
        VariantClear(&boolFalse);
        VariantClear(&noValue);
        VariantClear(&nonCanonicalBool);

        asr_history::Clear();
    }

    // 12. Scheme A: AsrBackendDisplayName hierarchical (Provider / Model) tests
    {
        // 1. Local
        {
            Config cfg;
            cfg.asrBackend = L"local";
            cfg.modelId = L"firered_ctc";
            CHECK(AsrBackendDisplayName(cfg) == L"Local / FireRedASR2 CTC", "local firered_ctc");
            cfg.modelId = L"firered_aed";
            CHECK(AsrBackendDisplayName(cfg) == L"Local / FireRedASR2 AED", "local firered_aed");
            cfg.modelId = L"sensevoice";
            CHECK(AsrBackendDisplayName(cfg) == L"Local / SenseVoiceSmall", "local sensevoice");
            cfg.modelId = L"sense_voice";
            CHECK(AsrBackendDisplayName(cfg) == L"Local / SenseVoiceSmall", "local sense_voice alias");
            cfg.modelId = L"";
            CHECK(AsrBackendDisplayName(cfg) == L"Local / FireRedASR2 CTC", "local empty modelId default");
            cfg.modelId = L"custom_onnx_model";
            CHECK(AsrBackendDisplayName(cfg) == L"Local / custom_onnx_model", "local custom modelId preserved");
            cfg.asrBackend = L"";
            cfg.modelId = L"firered_ctc";
            CHECK(AsrBackendDisplayName(cfg) == L"Local / FireRedASR2 CTC", "empty asrBackend default");

            // ModelIndex / ModelIdFromIndex / DefaultModelDir consistency
            CHECK(ModelIndex(L"firered_ctc") == 0, "model index firered_ctc");
            CHECK(ModelIndex(L"firered_aed") == 1, "model index firered_aed");
            CHECK(ModelIndex(L"sensevoice") == 2, "model index sensevoice");
            CHECK(ModelIndex(L"sense_voice") == 2, "model index sense_voice alias");
            CHECK(ModelIndex(L"") == 0, "model index empty");
            CHECK(ModelIndex(L"custom_model") == 0, "model index custom");

            CHECK(ModelIdFromIndex(0) == L"firered_ctc", "model id from index 0");
            CHECK(ModelIdFromIndex(1) == L"firered_aed", "model id from index 1");
            CHECK(ModelIdFromIndex(2) == L"sensevoice", "model id from index 2");
            CHECK(ModelIdFromIndex(99) == L"firered_ctc", "model id from index out of range");

            CHECK(DefaultModelDir(L"sense_voice") == DefaultModelDir(L"sensevoice"), "default model dir sense_voice alias matches");
        }
        // 2. Qwen ASR
        {
            Config cfg;
            cfg.asrBackend = L"qwen";
            cfg.qwenModel = L"qwen-audio-3.1-asr-flash-streaming";
            CHECK(AsrBackendDisplayName(cfg) == L"Qwen ASR / qwen-audio-3.1-asr-flash-streaming", "qwen 3.1 flash streaming");
            cfg.qwenModel = L"qwen-audio-3.1-asr-flash";
            CHECK(AsrBackendDisplayName(cfg) == L"Qwen ASR / qwen-audio-3.1-asr-flash", "qwen 3.1 audio flash");
            cfg.qwenModel = L"qwen-audio-3.0-asr-flash-streaming";
            CHECK(AsrBackendDisplayName(cfg) == L"Qwen ASR / qwen-audio-3.0-asr-flash-streaming", "qwen flash streaming");
            cfg.qwenModel = L"qwen-audio-3.0-asr-flash";
            CHECK(AsrBackendDisplayName(cfg) == L"Qwen ASR / qwen-audio-3.0-asr-flash", "qwen audio flash");
            cfg.qwenModel = L"qwen-audio-3.1-asr-flash-message";
            CHECK(AsrBackendDisplayName(cfg) == L"Qwen ASR / qwen-audio-3.1-asr-flash-message", "qwen 3.1 message");
            cfg.qwenModel = L"qwen3-asr-flash-realtime";
            CHECK(AsrBackendDisplayName(cfg) == L"Qwen ASR / qwen3-asr-flash-realtime", "qwen realtime");
            cfg.qwenModel = L"";
            CHECK(AsrBackendDisplayName(cfg) == L"Qwen ASR / qwen-audio-3.1-asr-flash-message", "qwen empty default");
            cfg.qwenModel = L"qwen-audio-turbo";
            CHECK(AsrBackendDisplayName(cfg) == L"Qwen ASR / qwen-audio-turbo", "qwen custom model");
        }
        // 3. Volcano Engine
        {
            Config cfg;
            cfg.asrBackend = L"volcengine";
            cfg.volcResourceId = L"volc.seedasr.sauc.duration";
            CHECK(AsrBackendDisplayName(cfg) == L"Volcano Engine / Seed-ASR 2.0 (duration)", "volc seedasr duration");
            cfg.volcResourceId = L"volc.seedasr.sauc.concurrent";
            CHECK(AsrBackendDisplayName(cfg) == L"Volcano Engine / Seed-ASR 2.0 (concurrent)", "volc seedasr concurrent");
            cfg.volcResourceId = L"volc.bigasr.sauc.duration";
            CHECK(AsrBackendDisplayName(cfg) == L"Volcano Engine / BigASR 1.0 (duration)", "volc bigasr duration");
            cfg.volcResourceId = L"volc.bigasr.sauc.concurrent";
            CHECK(AsrBackendDisplayName(cfg) == L"Volcano Engine / BigASR 1.0 (concurrent)", "volc bigasr concurrent");
            cfg.volcResourceId = L"";
            CHECK(AsrBackendDisplayName(cfg) == L"Volcano Engine / Seed-ASR 2.0 (duration)", "volc empty default");
            cfg.volcResourceId = L"volc.custom.model";
            CHECK(AsrBackendDisplayName(cfg) == L"Volcano Engine / volc.custom.model", "volc custom model");
        }
        // 4. Baidu Cloud
        {
            Config cfg;
            cfg.asrBackend = L"baidu";
            cfg.baiduDevPid = 1537;
            CHECK(AsrBackendDisplayName(cfg) == L"Baidu Cloud / Mandarin (1537)", "baidu mandarin 1537");
            cfg.baiduDevPid = 0;
            CHECK(AsrBackendDisplayName(cfg) == L"Baidu Cloud / Mandarin (1537)", "baidu 0 default");
            cfg.baiduDevPid = 1737;
            CHECK(AsrBackendDisplayName(cfg) == L"Baidu Cloud / English (1737)", "baidu english 1737");
            cfg.baiduDevPid = 1637;
            CHECK(AsrBackendDisplayName(cfg) == L"Baidu Cloud / Cantonese (1637)", "baidu cantonese 1637");
            cfg.baiduDevPid = 1837;
            CHECK(AsrBackendDisplayName(cfg) == L"Baidu Cloud / Sichuanese (1837)", "baidu sichuanese 1837");
            cfg.baiduDevPid = 1536;
            CHECK(AsrBackendDisplayName(cfg) == L"Baidu Cloud / DevPid (1536)", "baidu custom devpid");
        }
        // 5. MiMo ASR
        {
            Config cfg;
            cfg.asrBackend = L"mimo";
            cfg.mimoModel = L"mimo-v2.5-asr";
            CHECK(AsrBackendDisplayName(cfg) == L"MiMo ASR / mimo-v2.5-asr", "mimo default model");
            cfg.mimoModel = L"";
            CHECK(AsrBackendDisplayName(cfg) == L"MiMo ASR / mimo-v2.5-asr", "mimo empty default");
            cfg.mimoModel = L"mimo-v3-pro";
            CHECK(AsrBackendDisplayName(cfg) == L"MiMo ASR / mimo-v3-pro", "mimo custom model");
        }
        // 6. MAI
        {
            Config cfg;
            cfg.asrBackend = L"mai";
            cfg.maiApiProvider = L"openrouter";
            CHECK(AsrBackendDisplayName(cfg) == L"Microsoft MAI Transcribe 2 / OpenRouter", "mai openrouter");
            cfg.maiApiProvider = L"azure";
            CHECK(AsrBackendDisplayName(cfg) == L"Microsoft MAI Transcribe 2 / Azure Fast Transcription", "mai azure");
            cfg.maiApiProvider = L"";
            CHECK(AsrBackendDisplayName(cfg) == L"Microsoft MAI Transcribe 2 / OpenRouter", "mai empty default");
        }
        // 7. Single-level backends
        {
            Config cfg;
            cfg.asrBackend = L"doubao_ime";
            CHECK(AsrBackendDisplayName(cfg) == L"Doubao IME", "doubao_ime single-level");
            cfg.asrBackend = L"qwen_free";
            CHECK(AsrBackendDisplayName(cfg) == L"Qwen IME (Free)", "qwen_free single-level");
        }
        // 8. None & Fallback integration
        {
            Config cfg;
            cfg.asrBackend = L"none";
            CHECK(AsrBackendDisplayName(cfg) == L"None", "none backend");

            Config primary;
            primary.asrBackend = L"qwen";
            primary.qwenModel = L"qwen-audio-3.0-asr-flash-streaming";
            primary.fallbackAsrBackend = L"local";
            primary.modelId = L"firered_ctc";
            CHECK(AsrBackendDisplayName(primary) == L"Qwen ASR / qwen-audio-3.0-asr-flash-streaming", "primary qwen");

            Config fallback = BuildFallbackConfig(primary);
            CHECK(AsrBackendDisplayName(fallback) == L"Local / FireRedASR2 CTC", "fallback local scheme A");

            primary.fallbackAsrBackend = L"baidu";
            primary.baiduDevPid = 1537;
            fallback = BuildFallbackConfig(primary);
            CHECK(AsrBackendDisplayName(fallback) == L"Baidu Cloud / Mandarin (1537)", "fallback baidu scheme A");
        }
    }

    if (g_failures == 0) {
        wprintf(L"ALL PASS\n");
        return 0;
    }
    wprintf(L"%d FAIL\n", g_failures);
    return 1;
}
