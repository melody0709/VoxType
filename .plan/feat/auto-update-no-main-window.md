# VoxType 无主界面自动更新方案

状态：方案研究（未实施）
日期：2026-09-17
参考实现：`D:\GITHUB_melody0709\stock_new`（Rust / Slint，Minisign 签名清单 + MSI 自动更新）
本轮范围：信任模型选型、无主界面下的更新交互与生命周期设计、模块与文件划分、发版流水线改造、分阶段实施与验收标准。

---

## 0. 结论摘要

核心矛盾是：参考项目有一套成熟的「签名清单 → 下载校验 → helper 脱离安装 → 重启回执」链路，**但它的每一个用户可见状态都挂在一张 Slint 主窗口上**（更新胶囊、进度条、发布说明、重试按钮）。VoxType 没有主窗口，只有托盘图标、Settings 窗口和录音 HUD，不能照搬 UI 编排。

结论：**链路照搬，呈现层重做**。更新状态机的每个状态必须映射到一个「托盘常驻工具天然拥有的」呈现面，并且能在进程被杀、机器重启后从磁盘恢复。

| 决策点 | 结论 | 理由 |
| --- | --- | --- |
| 信任模型 | ECDSA P-256 + SHA-256 清单签名，公钥编译进 EXE；**不用 Minisign** | Minisign 默认预哈希签名需要 Ed25519 **和 BLAKE2b-512** 两个算法，Windows 原生都不提供；`bcrypt` 原生提供 ECDSA-P256 + SHA-256，零第三方代码 |
| 更新源 | `https://github.com/melody0709/VoxType/releases/latest/download/update-manifest.json`（+ `.sig`），编译进 EXE 常量 | 与参考项目一致；避免环境变量缺失导致正式包没有更新能力 |
| 触发面 | ① 启动后延迟检查 ② 常驻 30 分钟定时器（6 小时节流）③ 托盘菜单手动 ④ Settings → Update 页按钮 | 无主窗口，没有「打开应用顺手看到」这个自然入口，必须给足主动入口 |
| 呈现面 | 托盘气泡通知 + 托盘右键菜单项 + tray tooltip + Settings 新增 `Update` 页 | 三者按侵入性递增，覆盖「一眼扫到」「一步操作」「看细节」三种需求 |
| 弹窗策略 | **禁止任何模态对话框**（UAC 除外） | 语音输入工具在用户打字时弹窗是高干扰行为；且托盘工具没有「确认后回到主窗口」的语境 |
| 安装确认 | 两段式：点击 → 自动下载+校验（可后台）→ 再次点击「重启并更新」才退出安装 | 参考项目一键到底是因为有胶囊作为明确的意图确认面；托盘菜单的一次点击语义太轻，不足以代表「退出当前程序」 |
| 自动预取 | 默认关闭（`automaticUpdateDownload` 默认 false） | MSI ~11 MB，托盘工具的预取收益低、磁盘与带宽副作用可见；用户点击后下载更快也更符合预期 |
| 状态存储 | 新增 `%LOCALAPPDATA%\VoxType\update-state.json`（独立文件，不进 Config） | 避免污染 config.json、避免 Config 三处同步负担；参考项目的 `update-state.json` 同样独立 |
| Config 新增 | 仅 1 个布尔字段 `enableAutoUpdate`（默认 true） | 用户意图需要可发现、可手改；按 AGENTS.md 规则同步 globals.h + engine.cpp + settings.cpp 三处 |
| 版本号 | 清单 `version` = `APP_VERSION_MAJOR.MINOR.PATCH`，与 MSI 版本、git tag 三者一致 | `APP_VERSION_BUILD`（日期）按 UPGRADE_CONTRACT 明确不是 MSI 版本，不参与比较 |
| 适用范围 | 只对**由本产品 MSI 注册的安装版**开放；Portable 版只提示、不自动安装 | 沿用参考项目原则；符合 UPGRADE_CONTRACT 与 productline 决策 |
| CI | 暂不引入 GitHub Actions 发版工作流；保持本地发版 | 当前仓库没有 `.github/workflows/`；参考项目也把清单签名刻意留在本地手工脚本 |

---

## 1. 问题定义

### 1.1 要解决的问题

VoxType 现在的发版是纯手工：`build.bat --package` 产出 MSI/Portable，人工上传到 GitHub Release。用户升级完全依赖自己回仓库看有没有新版本。要补上的是：

1. 客户端能自己发现新版本；
2. 下载与安装过程可被密码学验证，不依赖 CA 代码签名证书（当前产物全是 `-unsigned`）；
3. 用户能在**没有主窗口**的前提下看到、确认、完成升级；
4. 任何中断（杀进程、关机、UAC 取消、安装失败）都能恢复或安全回退，不留半成品。

### 1.2 不在本轮范围

- 降级 / 回滚到旧版本（参考项目同样不做）。
- 多通道（beta / nightly）。
- 增量更新、差分补丁。
- Authenticode 证书采购。方案必须做到「有证书更好，没证书也能安全更新」。
- 模型文件（`%LOCALAPPDATA%\VoxType\models`）的更新。MSI 不拥有这些数据，更新流程也不得触碰。

---

## 2. 参考实现解构（stock_new）

先把参考项目里**值得复用**的东西与**不能复用**的东西分清楚。

### 2.1 可复用的链路（与 UI 无关）

```
读取 update-state.json ──► 节流判定（6h 周期 / 10min 启动地板 / 缓存 TTL）
        │
        ▼
GET update-manifest.json + .minisig ──► 先验签，成功前不解析任何字段
        │
        ▼
校验 schema / product / channel / 严格 x.y.z / RFC3339 时间 /
最小 Windows Build / 安装包名与版本一致 / HTTPS 无凭据无 fragment / 大小与 SHA-256 格式
        │
        ▼
版本比较 ──► UpToDate 或 Available
        │
        ▼ (用户确认)
下载 MSI 到 temp/.part，边下边算 SHA-256，校验声明长度与上限
        │
        ▼
原子提交到 updates/pending/{MSI, update-manifest.json, .minisig}
失败路径由 RAII guard 清理 .part 与残留
        │
        ▼ (用户确认退出)
复制自身到 %TEMP% → 以 --update-install-helper --parent-pid --data-root 启动
        │
        ▼
helper 重新验证 pending 全部材料 ──► 只读锁 MSI ──► 锁下复核大小与 SHA-256
        │
        ▼
等待父进程退出 ──► 取得 supervisor 互斥量 ──► msiexec /i ... /passive /norestart
        │
        ├─ exit 0     → 清 pending → 从 HKLM 安装目录拉起新版本
        ├─ exit 3010  → 清 pending → 提示需重启 Windows
        └─ 其他       → 保留 pending → 恢复启动当前版本（可重试）
        │
        ▼
helper 写 diagnostics/update-last-result.json（success / version / consumed）
        │
        ▼
新版本首次启动 consume_upgrade_receipt()，只提示一次
```

关键的工程细节，全部值得照抄：

- **验签先于解析**：`verify_minisign_signature` 在 `serde_json::from_slice` 之前，且明确禁止 legacy 签名。
- **三处独立的大小/哈希校验**：下载中增量哈希、提交前声明比对、helper 锁后复核。第三处是为了关闭「验证完成后、取得锁之前」的 TOCTOU 窗口。
- **受控目录 + 程序生成文件名**：pending 目录里的文件名永远由程序按清单版本拼出来，命令行只传 dataRoot 和 parentPid，不传路径、不传哈希。
- **下载即写 `.part` + 唯一后缀**（UUID），同版本重复操作不互相覆盖；提交用原子替换。
- **节流双阈值**：周期 6 小时，启动路径 10 分钟地板（30 分钟阈值 + 崩溃重启循环保护）。
- **每版本一次性提醒**：`bannerDismissedVersion` 只隐藏被点过「稍后」的那个版本，更高的新版本重新展示。
- **失败不写检查缓存**：只有成功（含「已是最新」）才进 6 小时缓存，失败下次自动检查仍会重试。
- **只读兜底探测**：清单拿不到时，解析 `releases/latest` 的 302 `Location` 取 tag，**仅用于提示 + 打开下载页**，绝不参与下载或安装判断。

### 2.2 不能复用的部分

- `UpdateController` 的 Slint 部分：`publish_available` / `publish_downloading` / `publish_ready` / `publish_installing` / `publish_failed` / `flash_note` / 绿色胶囊 / `slint::invoke_from_event_loop` —— 全部依赖主窗口。
- `native_tray::set_update_badge` —— windows-rs 托盘是参考项目自己的实现；VoxType 的托盘是裸 `Shell_NotifyIconW`，需要另一套做法。
- Watchdog supervisor 互斥量 —— VoxType 没有 watchdog 进程，退出守卫要另想办法（见 §5.3）。
- `reqwest` / `chrono` / `uuid` / `serde` —— VoxType 是 C++，对应换成 WinHTTP + `SYSTEMTIME`/`FileTimeToSystemTime` + `CoCreateGuid` + 手写 JSON。

---

## 3. VoxType 现状盘点

### 3.1 已具备的基础（省了大量工作）

| 能力 | 现状 | 位置 |
| --- | --- | --- |
| MSI 稳定产品身份 | `stable\|x64\|perMachine`，UpgradeCode / ProductCode 命名空间已冻结 | `packaging/windows/ProductIdentity.wxi` |
| Major Upgrade | `InstallInitialize` 之后执行，拒绝降级、不允许同版本升级 | `packaging/windows/UPGRADE_CONTRACT.md` |
| 安装目录注册表值 | `HKLM\Software\VoxType\InstallFolder`，AppSearch 会在 `RemoveExistingProducts` 前恢复 | 同上 |
| 数据与安装分离 | 用户数据在 `%LOCALAPPDATA%\VoxType`，MSI 不拥有 | `src/audio/engine.cpp::MutableDataDir()` |
| 打包脚本 | `scripts/package_voxtype.ps1`，产出 MSI + 7z + `.sha256` + `.input.sha256`，含同版本 digest 保护 | 脚本第 164 行 `Test-ExistingArtifact` |
| 可选 Authenticode | `build.bat --package --require-signing` + `VOXTYPE_SIGN_*` 环境变量 | `UPGRADE_CONTRACT.md` 第 30 行起 |
| HTTP 栈 | WinHTTP 已在用 | `src/asr/cloud_http_common.cpp`、`winhttp_websocket_transport.cpp` |
| 加密库 | `bcrypt` / `crypt32` 已在主目标链接列表 | `CMakeLists.txt` 104–110 行 |
| GitHub 发布 | 仓库 `melody0709/VoxType`，tag `v0.9.27`，资产 `VoxType-v0.9.27-win-x64-unsigned.msi` | `gh release list` |
| 单实例 | `Local\VoxType.SingleInstance` 命名互斥量，位于 `wWinMain` 最前 | `src/app/main.cpp` 2655 行起 |

**结论：安装侧的前置条件（稳定产品身份、Major Upgrade、数据分离、安装目录注册表）已经全部就绪。** 参考项目花了大力气解决的数据目录重构问题，VoxType 在上一轮 MSI 改造里已经做完了。本轮只需要补「检查 → 下载 → 脱离安装 → 重启回执」这一段，以及呈现层。

### 3.2 缺口清单

| 缺口 | 影响 |
| --- | --- |
| 没有任何更新检查/下载/安装代码 | 全新模块 |
| 没有清单签名与密钥工具链 | 需新建生成密钥、签名清单、验证三个脚本 |
| 没有托盘气泡通知能力 | `AddTrayIcon` 只有 `NIF_ICON \| NIF_MESSAGE \| NIF_TIP`，需补 `NIM_MODIFY` + `NIF_INFO` |
| 托盘菜单是静态构造 | `ShowTrayMenu` 每次重建菜单，加动态项容易（好消息） |
| 没有版本展示面 | 只有托盘菜单里一行灰色 `APP_VERSION_WSTR`，Settings 完全不显示版本 |
| 无 `.github/workflows/` | 发版全在本地，需要写清发版顺序与产物清单 |
| `wWinMain` 开头直接建互斥量 | helper 模式必须在互斥量之前分派，否则 helper 自己就退出了 |

### 3.3 与参考项目的结构差异

| 维度 | stock_new（Rust / Slint） | VoxType（C++ / Win32 托盘） |
| --- | --- | --- |
| 主界面 | Slint 主窗口，常驻可见 | **无**：托盘 + Settings 模态窗口 + 录音 HUD |
| 进程模型 | 主程序 + Watchdog supervisor | 单进程，托盘常驻，全局键盘钩子 |
| 退出守卫 | supervisor 互斥量 | 自身单实例命名互斥量（需新用途） |
| 信任算法 | Minisign Ed25519 + BLAKE2b prehash | ECDSA P-256 + SHA-256（bcrypt） |
| 网络栈 | reqwest | WinHTTP |
| 序列化 | serde | 手写 JSON（项目已有 `ExtractJsonStr` 等工具，注意区分两个同名函数） |
| 状态节流 | `update-state.json` | 同（照抄结构） |
| 收据/回执 | `diagnostics/update-last-result.json` | 同（照抄结构） |
| 数据目录 | SQLite 数据根 | `%LOCALAPPDATA%\VoxType` 或 Portable 运行目录 |
| CI | `signed-release.yml`（手动触发，只构建不发布） | 无 |

---

## 4. 关键决策与论证

### 4.1 信任模型：为什么不用 Minisign

参考项目的信任根是 Minisign（`minisign-verify` crate），签名对象是清单写盘后的原始 UTF-8 字节，且明确用 `allow_legacy=false` 拒绝 legacy 非预哈希签名。

Minisign 的**预哈希模式**定义为：

```
signature = Ed25519_sign(secret_key, BLAKE2b-512(message))
```

也就是说客户端验签需要两个原语：**Ed25519 验签** + **BLAKE2b-512 摘要**。

- Windows CNG（`bcrypt.dll`）在 Windows 10 19041 上没有可用的 Ed25519 算法标识；BLAKE2b 同样不在 CNG 支持范围。
- 因此走 Minisign 路线就必须 vendor 第三方实现：Ed25519（TweetNaCl 或 ref10 抽取，约 200–800 行）+ BLAKE2b-512（约 300 行），并自行解析 minisign 的签名容器（`untrusted comment:` 行、base64 blob、`Ed` 前缀 + 8 字节 key id）。
- 这条路上「自己写的解析 + 自己 vendor 的密码学」都会成为需要长期维护的安全边界。

**ECDSA P-256 + SHA-256 路线**的所有原语都由 `bcrypt` 原生提供，且该库已在链接列表里：

| 步骤 | API |
| --- | --- |
| 导入公钥 | `BCryptImportKeyPair(BCRYPT_ECDSA_PUBLIC_P256_ALGORITHM, ..., BCRYPT_ECCPUBLIC_BLOB)` |
| 摘要 | `BCryptCreateHash(BCRYPT_SHA256_ALGORITHM)` + `BCryptHashData` |
| 验签 | `BCryptVerifySignature` |
| 随机（helper 临时文件名等） | `BCryptGenRandom` / `CoCreateGuid` |

公钥以 CNG 的 `BCRYPT_ECCPUBLIC_BLOB` 形式（8 字节 `BCRYPT_ECDSA_PUBLIC_P256_MAGIC` 头 + 32 字节 X + 32 字节 Y）作为字节数组编译进 EXE，签名采用 CNG 原生的 raw `r‖s`（64 字节）。

**签名侧不需要额外工具**：PowerShell 5.1（Windows 自带）的 `System.Security.Cryptography.ECDsaCng` 使用 CNG 后端，`SignHash()` 返回的正是 raw `r‖s`；密钥生成与导出也全部走 .NET BCL。参考项目需要用户额外安装 `minisign.exe`（其工作流里专门有一段下载解压 minisign 的步骤），本轮可以省掉。

代价与取舍：

- 失去「用现成 CLI 手工验签」的便利。补偿手段是 `scripts/verify_update_manifest.ps1`（同样用 `ECDsaCng.VerifyHash`，与客户端同一套算法与格式），另外提供客户端 `--update-self-test` 子命令用**编译进 EXE 的生产公钥**做端到端复核 —— 这一点与参考项目的 `--update-bundle-self-test` 完全对应，实际证据强度更高。
- 需要在 AGENTS.md 的依赖规则下新增 `#pragma comment(lib, "bcrypt.lib")`（CMakeLists 已链接，仅补 pragma）。

> 备选：如果坚持与参考项目工具链完全一致（复用 `sign-update-manifest.ps1`、复用本机已有 minisign），则改为 vendor TweetNaCl + BLAKE2b-512 并自行解析 minisign 容器。两条路线的客户端安全属性等价，差别只在「零第三方代码」与「工具链一致」之间取舍。**本方案推荐前者。**

### 4.2 清单格式（schema v1）

格式对齐参考项目的 schema v2，字段语义一致，只换签名容器。

```json
{
  "schemaVersion": 1,
  "product": "VoxType",
  "channel": "stable",
  "version": "0.9.28",
  "publishedAtUtc": "2026-09-17T12:00:00.0000000Z",
  "minimumWindowsBuild": 19041,
  "releaseNotesUrl": "RELEASE_NOTES.md",
  "installer": {
    "url": "VoxType-v0.9.28-win-x64-unsigned.msi",
    "sha256": "9f2c...（64 位小写十六进制）",
    "sizeBytes": 11120640
  }
}
```

客户端校验顺序（**固定，不得调整**）：

1. 清单原始字节的 ECDSA 签名验证 —— 成功之前不解析任何字段。
2. `schemaVersion == 1`、`product == "VoxType"`、`channel == "stable"`。
3. `version` 必须是严格的 `x.y.z`（三段纯数字，拒绝 `-beta`、`0.9` 之类）。
4. `publishedAtUtc` 必须是可解析的 RFC 3339。
5. `minimumWindowsBuild` 不得高于当前系统 build（`RtlGetVersion().dwBuildNumber`）。
6. `installer.sha256` 必须是 64 位十六进制；`installer.sizeBytes` 在 `(0, 100 MiB]` 区间内。
7. `installer.url` 是**纯文件名**：不含 `/`、`\`、`..`、`://`，以 `.msi` 结尾，且包含 `version` 字符串。
8. 与 feed 同目录 join 出 HTTPS URL，再次校验 scheme 为 `https`、无凭据、无 fragment。

> 与参考项目的差异（第 7 条）：参考项目要求文件名严格等于 `{Product}-{version}-win-x64.msi`。VoxType 的 UPGRADE_CONTRACT 明确规定未签名产物必须带 `-unsigned` 后缀，将来有证书后又会出现别的后缀，所以改成「纯文件名 + 以 `.msi` 结尾 + 包含版本号」的约束。信任来自签名与 SHA-256，文件名只用于路径安全与防错配。

### 4.3 版本比较

- 客户端当前版本取 `APP_VERSION_MAJOR.MINOR.PATCH`（`resource.h` 宏拼出的 `APP_VERSION_STR`）。
- `APP_VERSION_BUILD`（形如 `20260904` 的日期）**不参与**任何比较，符合 MSI 契约。
- 三段整数元组比较，只有严格更大才视为有更新。相等或更小一律 `UpToDate`（因此天然拒绝降级）。

### 4.4 为什么不把状态写进 Config

`update-state.json` 单独放，字段：

```json
{
  "schemaVersion": 1,
  "lastAutomaticCheckUtc": "2026-09-17T12:00:00Z",
  "pendingVersion": "0.9.28",
  "dismissedVersion": "0.9.27",
  "balloonShownVersion": "0.9.28",
  "autoDownload": false
}
```

- 与 `config.json` 分离：更新状态是运行态，且会被高频改写（每次检查都写），混进用户配置会放大写坏配置的风险，也会让 `SaveConfig` 的语义变脏。
- 全部字段可选、缺失时回退默认；`schemaVersion` 缺失或非 1 视为文件损坏 → 重置为默认并记 WARN（沿用参考项目的容错策略）。
- 读取-修改-写回用进程内互斥（`std::mutex`）+ 临时文件 + 原子替换（`MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)`）。
- Portable 模式下跟随 `MutableDataDir()`（即解压目录），与 config.json 同一目录。

**唯一进 Config 的字段**是 `enableAutoUpdate`（默认 `true`）。按 AGENTS.md 规则必须同步三处：
`src/app/globals.h` Config 字段 + `src/audio/engine.cpp` LoadConfig/SaveConfig + `src/ui/settings.cpp` UI 控件。

---

## 5. 无主界面下的更新交互与生命周期

这一节是本方案的核心，也是与参考项目差异最大的部分。

### 5.1 三个呈现面

VoxType 只有三块「屏幕」，分别承担不同密度的信息：

| 呈现面 | 性质 | 承担什么 |
| --- | --- | --- |
| **托盘图标 tooltip / 气泡** | 只读、一次性、会被用户忽略 | 「有新版本」这件事本身 |
| **托盘右键菜单** | 一步可达、无窗口 | 「检查更新」「下载」「重启并更新」「跳过此版本」四个动作 |
| **Settings → Update 页** | 有窗口、可停留、有空间 | 版本号、上次检查时间、发布说明、进度条、错误详情、开关 |

录音 HUD **明确不复用**：它由 `kHudUpdateMessage` / `kHudUpdateWithOptionsMessage` 驱动，服务于录音—识别流程并与 `audio_diagnostics` 的 attempt 关联。往里塞更新提示会污染 ASR 诊断数据，也会在用户打字时突然冒出一个本该只在录音时出现的浮层。

### 5.2 状态 → 呈现映射表

`UpdatePhase` 状态机：`Idle → Checking → Available → Downloading → Ready → Installing → (进程退出) → Idle(新版本)`，任意阶段失败回到 `Available` 或 `Idle`，失败详情缓存成为下一次的起点。

| 阶段 | tray tooltip | 托盘气泡（每版本一次） | 托盘右键菜单 | Settings → Update 页 |
| --- | --- | --- | --- | --- |
| Idle | `VoxType` | — | `Check for Updates…` | 显示当前版本 + 上次检查时间 + 「立即检查」 |
| Checking | `VoxType` | — | `Checking…`（灰色） | 按钮禁用 + 「正在检查…」 |
| Available | `VoxType 0.9.27 → 0.9.28` | 「发现 VoxType v0.9.28」 | `Download Update to v0.9.28…` | 版本对比 + 发布说明链接 + 「下载并验证」+「跳过此版本」 |
| Downloading | `VoxType（下载 42%）` | — | `Downloading 42%…`（灰色） | 进度条 + `已下载 / 总大小` |
| Ready | `VoxType 0.9.27 → 0.9.28（已就绪）` | 「已就绪，重启即可更新」 | **`Restart & Update to v0.9.28`** | 「立即重启并更新」+「稍后」 |
| Installing | 图标随进程退出而消失 | — | — | —（窗口随进程关闭） |
| 升级成功 | `VoxType 0.9.28` | 「已更新到 v0.9.28」（新版本首次启动） | `Check for Updates…` | 「当前版本 0.9.28（刚刚升级）」 |
| 失败 | 回到 `Available` 形态 | 「更新失败：<短原因>」 | `Retry Update to v0.9.28` | 完整错误文本 + 「重试」 |
| 已跳过 | `VoxType` | — | `Check for Updates…` | 注明「已跳过 0.9.28」+「取消跳过」 |
| 便携版 | — | — | `Check for Updates…`（打开下载页） | 「便携版请手动下载」+ 打开发布页按钮 |
| 清单不可验证 | — | 「无法验证更新清单」 | `Open Download Page` | 说明 + 打开发布页按钮 |

设计约束：

- **气泡通知每版本最多一次**，且用户手动检查成功时不再重复弹（手动检查的结果直接在 Settings 页或状态行体现）。`balloonShownVersion` 持久化，避免每次重启刷屏。
- 气泡没有可点击按钮（`NIF_INFO` 的 balloon 不支持动作按钮），所以文案必须是完整的指路语：「右键托盘图标更新」。

### 5.3 进程生命周期：没有 watchdog 怎么做退出守卫

这是 VoxType 相比参考项目**最需要新设计**的一环。

参考项目在 `main.cpp` 里有 Watchdog supervisor 进程，helper 安装前靠「轮询取得 supervisor 互斥量」来确认「旧 EXE 已经真正被释放」。VoxType 没有 watchdog，但已有的单实例命名互斥量可以承担同一职责，代价是语义要从「防止重复启动」扩展成「防止安装期间启动」。

helper 的执行序列：

```
1. 参数分派必须在 wWinMain 创建 Local\VoxType.SingleInstance 之前完成，
   否则 helper 自己是这个 exe 的第二个实例，会立即被自己的单实例检查拒绝。
   （对应参考项目 main.rs:52 的 updater::try_handle，位置完全一致）

2. 从 updates\pending\ 重新读取并验证全部材料：
   签名 → schema → 版本递增 → Windows Build → 文件名 → 大小 → SHA-256

3. 以 FILE_SHARE_READ 打开 MSI（禁写、禁删），持有句柄直到 msiexec 结束；
   在锁保护下再复核一次大小与 SHA-256（关闭 TOCTOU 窗口）。

4. wait_for_parent_exit(parent_pid)：OpenProcess(SYNCHRONIZE) + WaitForSingleObject(30s)。
   父进程退出即意味着 WM_DESTROY 清理链已跑完
   （UninstallKeyboardHook / CloseAudioCapture / VolcengineForceAbortAndCloseAll）。

5. 取得单实例守卫：CreateMutexW(nullptr, FALSE, L"Local\\VoxType.SingleInstance")，
   若 GetLastError() == ERROR_ALREADY_EXISTS 则 100ms 重试，上限 30 秒；
   超时则取消安装（不陷入无限等待），并恢复启动当前版本。
   与参考项目不同：这里不需要成为 mutex 的 owner，只需要「句柄存在」
   让后续任何 VoxType 实例看到 ERROR_ALREADY_EXISTS 而主动退出。

6. msiexec /i "<pending MSI>" /passive /norestart，CREATE_NO_WINDOW，
   等待退出码。

   6a. 0     → cleanup_pending → 关闭守卫句柄 → 从 InstallFolder 启动 VoxType.exe
   6b. 3010  → cleanup_pending → 关闭守卫 → 记录「需重启 Windows」回执
   6c. 其他  → 保留 pending → 关闭守卫 → 恢复启动当前版本 → 记录失败回执
               （1602/1223 通常是用户取消了 UAC，属于正常路径，不作 ERROR）

7. 写 %LOCALAPPDATA%\VoxType\update-last-result.json
   { success, detail, version?, consumed: false }

8. helper 自身（%TEMP%\VoxType-Update-<guid>.exe）用
   MoveFileExW(path, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) 延迟删除。
```

**守卫句柄必须在启动新版本之前关闭**，否则新启动的 VoxType 会看到 `ERROR_ALREADY_EXISTS` 而拒绝运行。这是与参考项目 `drop(supervisor)` 后再 `relaunch` 完全对应的顺序，必须写进实现时注释。

同时新建的 VoxType 实例存在极小的双击竞态窗口（helper 关闭守卫 → 新实例创建守卫之间）。缓解：`relaunch` 内部带一次短重试；即便失败也只是「用户需要再点一次图标」，不影响正确性。

### 5.4 中断恢复

托盘工具会被用户用任务管理器杀掉、会被关机流程结束，而且**没有主窗口可以承载「进行中」的 UI**。所以恢复必须完全靠磁盘：

| 中断点 | 恢复行为 |
| --- | --- |
| 下载中被杀 | `temp\updates\*.part` 残留 → 下次启动扫描并清理（文件名带 UUID，不会误删有效材料） |
| 提交 pending 后被杀 | 下次启动 `verify_pending()` 成功 → 直接进入 `Ready`，气泡提示一次 |
| pending 存在但校验失败 | 记 WARN → 清理 pending → 托盘菜单回到 `Check for Updates…`，Settings 页说明「上次待安装的更新已失效」 |
| 安装成功后被杀在 helper 重启前 | 下次任意启动读到 `update-last-result.json` 且 `version == 当前版本` → 提示一次并标 `consumed: true` |
| 安装失败后被杀 | pending 保留 → 下次启动重新进入 `Available`/`Ready`，可重试 |
| 用户取消 UAC | 同「安装失败」，但提示语是「已取消更新，当前版本继续运行」 |

恢复动作放在**启动路径的 worker 线程**上，不能在 `wWinMain` 主线程同步做（AGENTS.md：不要让 UI 线程加载模型或等待 ASR；同理不能让主线程做文件哈希与网络）。

### 5.5 触发点与节流

| 触发 | 时机 | 节流 |
| --- | --- | --- |
| 启动检查 | 主窗口创建成功、预加载/预热线程启动之后 **延迟 25 秒**（避开启动时的模型预加载与火山预热网络竞争） | 10 分钟地板 + 6 小时周期 |
| 常驻周期 | `SetTimer(g_mainWindow, kUpdateRecheckTimer, 30 * 60 * 1000)` | 6 小时周期 |
| 托盘菜单/设置页手动 | 用户点击 | **无节流**，但受 `checkBusy` 互斥保护 |
| 检查结果缓存 | 进程内 | 成功结果缓存 6 小时（失败不缓存） |

- `SetTimer` 而非新增长驻线程：符合项目「不新增长驻线程/不增加空闲开销」的现状（当前纯云端模式空闲 ~12 MB 的约束）。
- 节流时间取自 `update-state.json` 的 `lastAutomaticCheckUtc`，**每次自动检查开始就写入**（无论成败），失败只等下一个周期 —— 与参考项目一致。

### 5.6 线程与消息约定

沿用项目现有模式，所有 UI/托盘状态变更只在主线程发生：

| 自定义消息（`globals.h`，从 `WM_APP + 14` 起） | 用途 |
| --- | --- |
| `kUpdateStateMessage` | worker → 主线程：阶段变更（携带 `UpdatePhase` + 版本），主线程据此更新 tooltip/菜单/气泡/Settings |
| `kUpdateProgressMessage` | worker → 主线程：下载进度百分比（按 1% 变化节流后再 Post，避免消息轰炸） |

worker 线程只做：网络、哈希、文件 IO。绝不直接调用 `Shell_NotifyIconW`、`AppendMenuW`、`SetWindowTextW`。

`UpdatePhase` 与当前版本、可用版本、错误文本集中放在一个 `std::mutex` 保护的小结构体里，`kUpdateStateMessage` 只是「状态变了，去读它」的通知，避免把字符串塞进 `WPARAM`/`LPARAM`。

---

## 6. 模块与文件设计

### 6.1 新增源文件

全部放在新目录 `src/update/`，与 `src/asr/`、`src/audio/` 同级。

| 文件 | 职责 | 关键接口（示意） |
| --- | --- | --- |
| `update_types.h` | 纯数据：`UpdateManifest`、`UpdateState`、`UpdatePhase`、`AvailableUpdate`、`PendingUpdate` | 无逻辑，无 Windows 依赖，便于单测 |
| `update_json.h/.cpp` | 极简 JSON 取值（字符串/数字/对象），支持转义 | `ExtractJsonString` 已有实现可参考但**不要直接复用**（见 AGENTS.md：`volcengine_asr.h::ExtractJsonStr` 与 `engine.cpp::ExtractJsonString` 语义不同） |
| `update_signature.h/.cpp` | CNG ECDSA-P256 验签 + SHA-256（文件/内存） | `VerifyManifestSignature(bytes, sig) -> bool`、`Sha256File(path) -> hex`、`Sha256Hex(bytes)` |
| `update_manifest.h/.cpp` | 清单解析 + 全字段校验 + 版本比较（**无网络、无磁盘**） | `ParseAndValidateManifest(bytes, selfVersion, windowsBuild) -> Expected<UpdateManifest>` |
| `update_state.h/.cpp` | `update-state.json` 读写（原子替换）+ 节流判定 + 跳过版本 | `LoadUpdateState()`、`MutateUpdateState(fn)`、`AutomaticCheckDue()`、`StartupCheckDue()`、`DismissVersion()` |
| `update_http.h/.cpp` | WinHTTP GET：清单、签名、安装包（大小上限、声明长度、增量 SHA-256、进度回调） | `FetchLimited(url, limit) -> bytes`、`DownloadInstaller(url, manifest, dest, progress)` |
| `update_paths.h/.cpp` | 受控目录路径：`updates\pending`、`temp\updates`、pending 文件名生成 | 全部文件名由程序按清单版本生成，不接受外部输入 |
| `update_controller.h/.cpp` | 阶段状态机、worker 线程、消息回投、托盘/设置页文案发布 | `UpdateController::Start()`、`CheckNow(force)`、`Download(trigger)`、`RequestInstall()`、`DismissVersion(v)`、`RestorePending()`、`ConsumeReceipt()` |
| `update_helper.cpp` | helper / self-test 入口（被 `wWinMain` 在互斥量之前调用） | `TryHandleUpdateArguments(argc, argv) -> optional<int>` |

### 6.2 需要修改的既有文件

| 文件 | 改动 |
| --- | --- |
| `src/app/globals.h` | ① `kUpdateStateMessage`/`kUpdateProgressMessage`（`WM_APP + 14/15`）② `kUpdateRecheckTimer`（timer id 7）③ `ID_TRAY_UPDATE`/`ID_TRAY_UPDATE_DISMISS`（1007/1008）④ `Config::enableAutoUpdate` ⑤ `IDC_UPDATE_*` 控件 ID（2230 起）⑥ `UiStyle` 里 Update 页的布局常量（**不得硬编码魔法数字**） |
| `src/app/main.cpp` | ① `wWinMain` 参数分派插到 `CreateMutexW` **之前** ② `ShowTrayMenu` 增加动态更新项与版本行文案 ③ `MainWndProc` 处理两个新消息 + 两个新命令 + 新 timer ④ `AddTrayIcon` 抽出可复用的 `UpdateTrayIcon(tooltip)`，供 `NIM_MODIFY` 改 tooltip/弹气泡 ⑤ `WM_DESTROY` 里 `KillTimer` 新 timer、通知 controller 停止 ⑥ 启动延迟触发首次检查 |
| `src/ui/settings.cpp` | 新增第 6 个 tab `Update`：`AddUpdateControl()`、`ShowSettingsPage` 加 `page == 5` 分支、`LoadSettingsControls`/`SaveSettingsControls` 处理 `enableAutoUpdate`、按钮命令处理（检查/下载/安装/跳过/打开发布页） |
| `src/audio/engine.cpp` | `LoadConfig`/`SaveConfig` 增加 `enableAutoUpdate` |
| `CMakeLists.txt` | 新源文件加入 `add_executable(VoxType ...)`；`msi` 加入 `target_link_libraries`（若采用 MSI 产品码检测） |
| `src/app/main.cpp` pragma 区 | 补 `#pragma comment(lib, "bcrypt.lib")`（新增依赖需同步 pragma + CMakeLists） |
| `packaging/windows/UPGRADE_CONTRACT.md` | 追加一节说明「自动更新只对 MSI 安装版开放」「更新不触碰 `%LOCALAPPDATA%\VoxType`」 |
| `README.md` / `CHANGELOG.md` | 版本与功能记录 |

### 6.3 新增脚本与文档

| 文件 | 职责 |
| --- | --- |
| `scripts/generate_update_signing_key.ps1` | 生成 ECDSA P-256 密钥对；私钥导出为带密码的 PKCS#8，公钥导出为 CNG blob（同时输出可读的 X/Y 十六进制用于人工复核）。**只运行一次**，生成后不得重跑 |
| `scripts/sign_update_manifest.ps1` | 读 `resource.h` 的版本、读最终 MSI 的大小与 SHA-256、生成 `update-manifest.json`、用 `ECDsaCng.SignHash` 生成 `update-manifest.json.sig`、用仓库公钥复核、重新生成 `SHA256SUMS.txt` |
| `scripts/verify_update_manifest.ps1` | 只读验证：给清单 + 签名 + MSI，用仓库公钥完整复核一遍（替代参考项目里 `minisign -V` 的角色） |
| `scripts/test_update_verification.ps1` | 沙盒测试：一次性测试密钥验证「正确接受 / 篡改清单拒绝 / 错误密钥拒绝 / 生产公钥拒绝测试密钥 / MSI 哈希不匹配拒绝」 |
| `doc/update-signing-and-updates_zh.md` | 信任模型、密钥管理、发版顺序、验证清单（对齐 `release-signing-and-updates.md` 的章节结构） |

### 6.4 目录约定

```
%LOCALAPPDATA%\VoxType\
├─ config.json                    （既有，新增 enableAutoUpdate 字段）
├─ update-state.json              （新增，运行态节流与一次性提醒）
├─ diagnostics\
│  └─ update-last-result.json     （新增，helper 安装回执）
├─ updates\
│  └─ pending\
│     ├─ VoxType-v0.9.28-win-x64-unsigned.msi   （程序按清单版本生成，无外部输入）
│     ├─ update-manifest.json
│     └─ update-manifest.json.sig
├─ temp\
│  └─ updates\
│     └─ .VoxType-v0.9.28-<guid>.msi.part       （下载中间态，任何失败都清理）
├─ log\                           （既有）
└─ models\                        （既有，更新流程绝不触碰）
```

Portable 模式下 `updates/`、`temp/`、`update-state.json` 全部落在解压目录（跟随 `MutableDataDir()`），且由于便携版不安装，`updates/pending` 不会产生——便携版的菜单项直接是「打开下载页」。

---

## 7. 安全边界

照抄参考项目的顺序，不发明：

1. **信任根是编译进 EXE 的公钥 + 编译进 EXE 的固定 feed URL。** 不存在「漏设环境变量导致正式包没有更新能力」的情况，也不存在运行时可改的更新源。
2. **验签先于解析。** 签名失败立即中止，不进入 JSON 解析。
3. **仅 HTTPS、无凭据、无 fragment。** `GetUrlComponents` 后逐项检查，重定向后再次检查最终 URL。
4. **只接受严格更高的 `x.y.z`。** 天然拒绝降级与同版本重放。
5. **三方独立的大小/哈希校验**（下载中增量 → 提交前比对 → helper 锁后复核）。
6. **受控目录 + 程序生成文件名。** 命令行只传 `--data-root` 与 `--parent-pid`，不传路径、不传哈希、不传版本。
7. **TOCTOU 关闭。** 只读 + 禁写禁删共享句柄锁定 MSI，锁下复核。
8. **失败即清理。** 下载与提交全部包在 RAII guard 内，任何异常路径都尽力删除 `.part` 与中间文件。
9. **私钥离线。** 不进 Git、不进 CI、不进日志、不进命令行正文；密码只经标准输入或交互提示传给签名工具。OneDrive 不算离线备份。
10. **不要绕过系统安全交互。** UAC 提示、SmartScreen、「未知发布者」在没有 Authenticode 时是正常现象；本方案不写任何绕过代码。

### 7.1 MOTW 的取舍（必须显式说明）

用 WinHTTP 直接写文件**不会**给 MSI 打上 `Zone.Identifier` 备用数据流，因此 `msiexec` 不会触发 SmartScreen 的「来自 Internet 的文件」拦截。这是刻意的，也带来两个必须承认的后果：

- 优点：11 MB 的本地补丁不会因为 MOTW 被 SmartScreen 拦下，用户体验与参考项目一致。
- 责任转移：因为不依赖 MOTW，**信任完全落在清单签名 + SHA-256 上**。签名验证必须没有任何可跳过的分支，且不允许存在「签名验证失败但继续安装」的降级路径。
- 相应约束：`minimumWindowsBuild` 与 `product`/`channel` 校验不得为了兼容性放宽。

---

## 8. 发版流水线改造

### 8.1 发版顺序（本地，不进 CI）

1. 提升 `src/app/resource.h` 的 `APP_VERSION_MAJOR/MINOR/PATCH`（`APP_VERSION_BUILD` 可同时更新为当天日期），同步 `README.md` 版本与 `CHANGELOG.md` 章节。
2. 跑测试与 `build.bat --package`，得到最终 `build/packages/VoxType-v<ver>-win-x64-unsigned.msi` 与 7z。
   - 注意 `package_voxtype.ps1::Test-ExistingArtifact` 的同版本 digest 保护：代码改了但版本没 bump 会直接抛错。正确做法是**先 bump 版本**，或把旧产物移到 `build/artifacts/stale-v<ver>-<原因>/`，**不要**去改脚本或删其他版本的产物。
3. 在 `build/packages/` 内生成并签名清单：
   ```
   pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/sign_update_manifest.ps1
   ```
4. 用 `scripts/verify_update_manifest.ps1` 和 `VoxType.exe --update-self-test` 各自复核一遍（后者用的是**编译进 EXE 的生产公钥**，证据更强）。
5. 创建 GitHub **Draft** Release，tag `v<ver>`（与既有 `v0.9.27` 风格一致），一次性上传全部资产：
   - `VoxType-v<ver>-win-x64-unsigned.msi` 及其 `.sha256` / `.input.sha256`
   - `VoxType-v<ver>-win-x64-portable-unsigned.7z` 及其 `.sha256` / `.input.sha256`
   - `update-manifest.json`、`update-manifest.json.sig`
   - `SHA256SUMS.txt`、`RELEASE_NOTES.md`
6. 从 Draft 的下载链接重新核对每个资产的名称、大小、哈希与签名。
7. **一次性**把 Draft 发布为 latest。发布后不得再补传或覆盖 `update-manifest.json` 与其签名——`releases/latest/download/` 里的清单会被客户端立即消费，任何「先发一半再补」的窗口都是攻击面。
8. 发布后从公开地址再做一次只读验证：
   `https://github.com/melody0709/VoxType/releases/latest/download/update-manifest.json`。

### 8.2 版本一致性检查（建议加进发版脚本）

`sign_update_manifest.ps1` 必须交叉校验并拒绝以下任一不一致：

- `resource.h` 的 `MAJOR.MINOR.PATCH` ≠ 目标 tag ≠ 清单 `version`；
- 清单 `installer.url` 与实际 MSI 文件名不一致；
- `installer.sha256`/`sizeBytes` 与磁盘上的 MSI 不一致；
- 生成的签名用仓库公钥复核失败。

### 8.3 可选：后续加 CI

当前 `melody0709/VoxType` 没有 `.github/workflows/`，保持本地发版即可。若将来要加，只应做「构建 + 可选 Authenticode + 发布级校验」，**不产出清单、不创建 Release**，与参考项目的 `signed-release.yml` 分工一致（清单签名必须留在本地，因为私钥永不上 CI）。

---

## 9. 分阶段实施

### Phase 0：信任根与离线可测工具链

- 生成 ECDSA P-256 密钥对；公钥以 `BCRYPT_ECCPUBLIC_BLOB` 形式落地为源码内字节数组。
- 实现 `update_signature`（CNG 验签 + SHA-256）与 `update_manifest`（解析 + 全字段校验 + 版本比较），二者不依赖网络与 Windows UI。
- 实现 `VoxType.exe --update-self-test --manifest ... --signature ... --installer ... --public-key ... --report ...`，输出 JSON 报告，退出码 0/2。
- 写 `scripts/generate_update_signing_key.ps1`、`sign_update_manifest.ps1`、`verify_update_manifest.ps1`、`test_update_verification.ps1`。
- **验证**：`test_update_verification.ps1` 全绿；`--update-self-test` 在真实打包产物上跑通。

这一阶段结束时，**更新能力已经可以完全离线验证**，不需要网络也不需要 UAC。

### Phase 1：只读检查（无下载、无安装）

- `update_state`（节流 + 一次性提醒）、`update_http::FetchLimited`、`update_paths`。
- `UpdateController` 骨架：`CheckNow` + 阶段状态 + 消息回投。
- 托盘菜单加 `Check for Updates…`；tooltip 显示 `0.9.27 → 0.9.28`；气泡提示一次。
- 启动延迟 25 秒自动检查 + 30 分钟 `SetTimer`。

### Phase 2：下载与暂存

- `DownloadInstaller`（增量哈希 + 进度）、staging 提交、pending 目录、RAII 清理。
- 托盘菜单显示下载进度；完成后进入 `Ready`。
- 启动时 `RestorePending()`，损坏 pending 安全清理。

### Phase 3：安装

- `update_helper.cpp`：参数分派（`wWinMain` 互斥量之前）、`verify_pending`、只读锁 + 锁后复核、`wait_for_parent_exit`、单实例守卫、`msiexec`、重启、回执、helper 自删。
- 失败路径：UAC 取消 / 安装失败 / 3010 三种分支。
- `ConsumeReceipt()`：新版本首次启动提示一次。

### Phase 4：Settings → Update 页

- 第 6 个 tab，`AddUpdateControl` + `ShowSettingsPage` 分支 + Load/Save。
- 含 `enableAutoUpdate` 开关、版本对比、发布说明链接、进度条、错误详情、跳过/取消跳过。
- 编译后按 AGENTS.md 要求检查裁切与重叠（`scripts/validate_settings_layout.ps1` 也要覆盖新页）。

### Phase 5：发版脚本与文档

- 收尾 `doc/update-signing-and-updates_zh.md`、`UPGRADE_CONTRACT.md` 增补、`README.md` / `CHANGELOG.md`。
- 用一次真实的「低版本 → 高版本」演练走完整条链路。

---

## 10. 验收与测试

### 10.1 单元测试（无需网络，可进 `build.bat --test`）

对齐参考项目 `updater.rs` 的测试矩阵：

- URL 校验：接受合法 HTTPS；拒绝 `http://`、带用户名、带密码、带 fragment。
- 版本比较：`0.9.28 > 0.9.27`；拒绝 `0.9`、`0.9.28-beta`、非数字。
- 清单校验负例：schema 不符、product 不符、channel 不符、时间非 RFC 3339、文件名与版本不匹配、`../` 路径穿越、SHA-256 格式错误、`sizeBytes` 为 0 或越界、`minimumWindowsBuild` 超过当前系统。
- 签名负例：篡改清单拒绝、篡改签名拒绝、错误公钥拒绝、**生产公钥必须拒绝测试密钥**。
- pending 校验：缺清单/缺签名/缺 MSI/大小不符/哈希不符 全部必须失败。
- 状态文件：缺字段回填默认；`schemaVersion` 缺失视为损坏；未知 schema 回退默认。
- 节流：无记录时到期；5 小时未到期、6 小时到期；启动路径 9 分钟不到期、10 分钟到期。
- 一次提醒：`dismissedVersion` 只隐藏该版本；更高版本重新展示；重启后仍生效。
- 回执：成功且版本匹配 → 返回一次；第二次返回空；版本不匹配 → 空；失败 → 空。
- 残留清理：pending 里的旧版本 MSI 与 `.tmp` 中间文件被删除，当前版本三件套保留。

### 10.2 集成测试（本机，不触发 UAC）

- `--update-self-test` 对真实打包产物 + 真实签名跑通。
- helper 的失败路径：把 pending 里的 MSI 换成无效文件，验证 helper 在 `verify_pending` 阶段就拒绝，且不会启动 msiexec。

### 10.3 实机闭环（必须在隔离 Windows VM）

- 引导版（低版本）升级到更高测试版本：正常成功路径。
- UAC 取消 → 保留 pending，当前版本被重新拉起。
- 安装失败 → 保留 pending，可重试。
- `3010` → 记录「需重启 Windows」回执。
- 升级前用任务管理器杀掉进程 / 强制关机 → 重启后 pending 正确恢复或清理。
- 便携版确认**不会**出现自动安装入口。
- 升级前后 `%LOCALAPPDATA%\VoxType` 的 `config.json`、`models\`、`log\` 完全保留。

### 10.4 回归

- 录音热键（尤其 CapsLock 短按补发与长按恢复）不受影响。
- Settings 5 个既有页的布局、裁切、高 DPI 表现不回退；新增 Update 页在 100%/125%/150%/200% 缩放下无重叠。
- 纯云端模式空闲内存不回退（不新增长驻线程，不预载）。
- Debug Mode 下控制台不刷更新日志。

---

## 11. 风险与开放问题

### 11.1 需要用户拍板的问题

| # | 问题 | 建议 |
| --- | --- | --- |
| Q1 | 接受 ECDSA P-256 替代 Minisign 吗？ | 建议接受（§4.1）。若坚持工具链一致，改走 TweetNaCl + BLAKE2b 备选路线 |
| Q2 | 是否引入 `msi.dll` 做产品码检测 | 建议引入（系统 DLL，不进载荷）。替代：只比对 `HKLM\Software\VoxType\InstallFolder` 与当前 exe 目录 |
| Q3 | 自动检查默认开还是关 | 建议默认开（只下载几百字节的清单），自动**下载** MSI 默认关 |
| Q4 | 是否需要 beta 通道 | 建议本轮不做，但 schema 里保留 `channel` 字段 |
| Q5 | 提示文案用中文还是跟随系统语言 | 现有托盘菜单是英文（`Settings…` / `Reload ASR Engine` / `Debug Mode`），建议保持一致用英文 |
| Q6 | 是否顺手补 `.github/workflows/` | 建议本轮不做，先把手动发版链路走通 |

### 11.2 已知风险

| 风险 | 缓解 |
| --- | --- |
| 单实例守卫与 helper 的时序写错会导致应用起不来 | 把「关闭守卫 → 启动新版本」的顺序写成显式注释与断言；实机闭环必测 |
| 用户取消了 UAC 却以为更新失败 | 退出码 1602/1223 单独识别为「已取消」，文案要中性（「已取消更新，当前版本继续运行」） |
| 无 Authenticode 时「未知发布者」提示可能让用户犹豫 | 文档说明这是预期现象；不写任何绕过代码；将来有证书时自动受益 |
| 托盘气泡在 Win11 上可能被专注助手/通知设置屏蔽 | 不能只靠气泡：托盘菜单项与 Settings 页必须独立承载完整信息 |
| pending 里残留旧版本 MSI 长期占磁盘 | 提交新版本时清理所有非当前版本材料（`.msi` / `.sig` / `.tmp`） |
| 私钥丢失 | 无法安全轮换；必须停止自动更新并发布需人工安装的新引导版。文档必须写明这一点 |
| 版本改动影响「同版本 digest 保护」 | 每次发版必须 bump `APP_VERSION_PATCH`；MSI 也不允许同版本 Major Upgrade |

### 11.3 实施顺序建议

Phase 0 与 Phase 1 可以独立完成并合并，此时应用已经能「发现新版本」，风险最低、收益可见。Phase 2/3 涉及进程生命周期改动，建议单独一个 PR，并在 Phase 3 完成后立即做一次 VM 闭环演练再继续 Phase 4/5。

---

## 附录 A：参考实现关键位置索引

| 内容 | 路径 |
| --- | --- |
| 更新核心（1613 行，含 30+ 单测） | `stock_new/src/updater.rs` |
| UI 编排与状态机 | `stock_new/src/ui/background_operations.rs` |
| 托盘与激活参数 | `stock_new/src/native_tray.rs:200,222` |
| 主程序入口的参数分派顺序 | `stock_new/src/main.rs:49-54` |
| 信任模型与发版顺序文档 | `stock_new/docs/release-signing-and-updates.md` |
| 清单生成与签名脚本 | `stock_new/scripts/sign-update-manifest.ps1` |
| 密钥生成脚本 | `stock_new/scripts/generate-update-signing-key.ps1` |
| 发布校验工作流（只构建不发布） | `stock_new/.github/workflows/signed-release.yml` |
| helper 恢复路径测试 | `stock_new/scripts/test-update-helper-recovery.ps1` |

## 附录 B：VoxType 侧需动的既有位置索引

| 内容 | 路径 / 行 |
| --- | --- |
| 版本宏 | `src/app/resource.h:5-12` |
| 自定义消息 ID（下一个可用 `WM_APP + 14`） | `src/app/globals.h:48-64` |
| Timer ID（下一个可用 7） | `src/app/globals.h:72-78` |
| 托盘命令 ID（下一个可用 1007） | `src/app/globals.h:224-229` |
| 控件 ID（下一个可用 2230） | `src/app/globals.h:345-356` |
| `Config` 结构 | `src/app/globals.h:358` |
| 托盘菜单构造 | `src/app/main.cpp:2158` |
| 主窗口消息处理 | `src/app/main.cpp:2180` |
| `WM_DESTROY` 清理链 | `src/app/main.cpp:2594-2612` |
| `wWinMain`（互斥量在 2659 行，参数分派须插在它之前） | `src/app/main.cpp:2655` |
| 消息循环 | `src/app/main.cpp:2742` |
| 可写数据目录（portable 分支） | `src/audio/engine.cpp:187-207` |
| Settings tab 创建（5 个） | `src/ui/settings.cpp:2819-2835` |
| Settings 页路由 | `src/ui/settings.cpp:1175` |
| 主目标链接库 | `CMakeLists.txt:95-110` |
| 主目标源文件列表 | `CMakeLists.txt:35-71` |
| MSI 升级契约 | `packaging/windows/UPGRADE_CONTRACT.md` |
| 产品身份（不得重生成） | `packaging/windows/ProductIdentity.wxi` |
| 打包脚本与同版本 digest 保护 | `scripts/package_voxtype.ps1:164` |
