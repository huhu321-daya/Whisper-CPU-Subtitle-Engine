# Whisper CPU Subtitle Engine

一个**纯 CPU 实时语音转字幕引擎**实现，可作为独立的人工智能语音识别组件，供支持该 IPC 接口的媒体播放器调用。

基于 [whisper.cpp](https://github.com/ggerganov/whisper.cpp) 静态链接，无需独立 DLL、无需 GPU，在核显机器上即可实时把播放中的音频转成字幕，并在画面浮窗与信息栏同步显示时间戳。

> 本项目为**独立实现**，不包含任何第三方播放器的二进制文件或源码。

---

## 特性

- **纯 CPU 运行** — 不依赖 CUDA / Vulkan / OpenCL，核显与老机器同样可用
- **静态链接** — 单个 exe 自包含，不需要额外分发 whisper DLL
- **实时字幕** — 播放中的音频边播边识别，画面浮窗显示字幕
- **时间戳对齐** — 按音频帧实时推算播放位置，字幕与画面同步
- **低延迟** — 3 秒识别窗口 + `audio_ctx=768`，实测延迟约 5~6 秒
- **多语言** — 支持 whisper 全部语言，通过控制报文自动切换
- **MIT 许可** — 代码可自由使用、修改、再分发

## 效果

| 项目 | 说明 |
|---|---|
| 画面字幕 | 浮窗显示识别文本 |
| 信息栏 | `[00:00:12.340 --> 00:00:15.120]识别文本` |
| 适用场景 | 直播流、无字幕片源、外语视频 |
| 硬件要求 | x86-64 CPU，支持 SSE2（推荐 AVX/F16C） |
| 内存占用 | 约 550 MB（tiny 模型，含 96 MB 解码计算缓冲） |

---

## 环境要求

| 项 | 要求 |
|---|---|
| 操作系统 | Windows 10 / 11 x64 |
| CPU | x86-64，建议 AVX + F16C（编译时用 `-march=ivybridge`） |
| 模型 | ggml 格式，如 `ggml-tiny.bin` / `ggml-base.bin` |
| 编译工具链 | [w64devkit](https://github.com/skeeto/w64devkit)（自带 g++ 与静态库） |

### 模型下载

```
https://huggingface.co/ggerganov/whisper.cpp/tree/main
```

推荐 `ggml-tiny.bin`（77 MB，速度最快）或 `ggml-base.bin`（148 MB，准确率更高）。

---

## 安装（普通用户）

本仓库**不包含编译产物**。成品 exe 请到 **Releases** 页面下载。

1. 从 [Releases](releases) 下载最新版 `main64.exe`
2. 放到宿主程序指定的引擎目录（与该目录下已有引擎同名，直接替换即可）
3. 下载模型 `ggml-tiny.bin` 到宿主程序的模型目录
4. 启动播放，在音频设置里选择本引擎

详细步骤见 [docs/RELEASE.md](docs/RELEASE.md)。

---

## 构建（开发者）

本仓库只包含引擎自身的代码（`src/cpu_engine.cpp`）与编译所需的接口头文件。
**上游 whisper.cpp 源码不随仓库分发**（体积与许可证原因），需自行获取。

### 依赖清单

| 依赖 | 版本 | 获取方式 |
|---|---|---|
| whisper.cpp 源码 | **v1.9.5** | 见下方 clone 命令，**必须带 `--recursive`** |
| w64devkit | 1.20+ | https://github.com/skeeto/w64devkit/releases |
| 模型文件 | ggml 格式 | https://huggingface.co/ggerganov/whisper.cpp/tree/main |

> **版本必须匹配**：`include/whisper-version.h` 与 `include/ggml/ggml-version.h` 固化了 `1.9.5` / `0.27.0`。若改用其他版本的 whisper.cpp，请同步修改这两个文件，否则可能因接口变化编译失败。

### 一键构建

```powershell
# 1. 拉取上游源码（--recursive 不可省略，ggml 是子模块）
git clone --depth 1 --branch v1.9.5 --recursive https://github.com/ggerganov/whisper.cpp

# 2. 解压 w64devkit 到 ./tools/w64devkit/
#    下载地址：https://github.com/skeeto/w64devkit/releases

# 3. 编译
powershell -ExecutionPolicy Bypass -File build.ps1
```

产物输出到 `build/main64.exe`。若源码与工具链不在默认位置，用参数指定：

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 `
    -WhisperDir D:\src\whisper.cpp `
    -W64Devkit  D:\tools\w64devkit\bin `
    -Arch       ivybridge
```

### 手工编译

逐步命令见 [docs/BUILD.md](docs/BUILD.md)。若自行编写编译脚本，注意以下两个**不在标准编译清单中但必须编入**的目录：

| 目录 | 为何必需 |
|---|---|
| `ggml/src/ggml-cpu/tiled/` | `ggml-cpu.c` 调用 `ggml_tiled_wdata_*` 与 `ggml_compute_forward_mul_mat_*_tiled` |
| `src/aneforge/` | `whisper.cpp` 无条件调用 `whisper_aneforge_init/encode/free` |

遗漏这两个目录会在**链接阶段**报 `undefined reference`，而非编译阶段，容易被误判为工具链问题。

### 关键参数

```bash
-DGGML_USE_CPU=1        # 必须：启用 CPU 后端注册
-UWHISPER_USE_COREML    # 必须：取消 CoreML 宏
-UWHISPER_USE_VULKAN    # 关闭 Vulkan 后端
-UWHISPER_USE_CUDA      # 关闭 CUDA 后端
-march=ivybridge -mavx -mf16c
```

> **注意**：关闭宏必须用 `-U`，不能写 `-DXXX=0`。预处理器判断的是宏是否被定义，`=0` 仍视为已定义，会导致 CoreML 代码参与编译并在模型初始化阶段失败。

### 链接

```bash
g++ -O2 -march=ivybridge -mavx -mf16c -DNDEBUG \
    -DGGML_USE_CPU=1 \
    -UWHISPER_USE_COREML -UWHISPER_USE_VULKAN -UWHISPER_USE_CUDA \
    -Iwhisper.cpp/include -Iwhisper.cpp/ggml/include \
    src/cpu_engine.cpp \
    -Wl,--start-group \
    libwhisper.a libggml-cpu.a libggml-base.a \
    -Wl,--end-group \
    -static -o main64.exe \
    -lshlwapi -lole32 -lrpcrt4 -lws2_32
```

**注意**：`-Wl,--start-group` 中的逗号在 PowerShell 下会被解释为参数分隔符，需用数组传参或用 cmd。

产物约 4.69 MB（PE32+ / Console），导入表仅依赖 `KERNEL32` / `msvcrt` / `ADVAPI32`，无任何第三方运行时依赖。

> Releases 中的成品约 4.34 MB，与本地产物体积略有差异，符号表完全一致，功能等价。原因见 [BUILD.md](docs/BUILD.md) 的「体积差异说明」。

---

## 部署

将 `main64.exe` 放到宿主程序指定的引擎目录。

**关于文件名**：部分宿主的引擎加载器按固定名称查找可执行文件。如果改名后无法被识别，请查阅该程序的扩展组件配置说明。

**关于依赖**：本引擎静态链接 whisper.cpp，运行时不需要任何外部推理库，单个 exe 即为完整组件。

---

## 通信协议

引擎与播放器通过**命名管道**通信，管道名由命令行参数 `-IPC <name>` 指定。

### 消息格式

每条消息固定 12 字节头 + body：

```
偏移 0   uint32  magic   固定 0x11111111
偏移 4   uint32  type    消息类型
偏移 8   uint32  len     body 字节数
偏移 12  ...     body
```

### 接收（播放器 → 引擎）

| type | 长度 | 含义 |
|---|---|---|
| `0x10` | 524 | INIT，body 前段为 UTF-16LE 模型路径 |
| `0x20` | ≥ 40 | 控制，偏移 36 处 `uint32` = 当前播放位置（ms），前段为 UTF-16LE 语言标识 |
| `0x21` | 变长 | 音频，float32 PCM / 16 kHz / 单声道 |

### 发送（引擎 → 播放器）

| type | body | 说明 |
|---|---|---|
| `0x11` | 1 字节 `0x00` | ACK，**必须在加载模型前发送** |
| `0x10000` | 文本 | 日志 / 信息栏通道 |
| `0x20000` | 24 字节 | 时间戳 |
| `0x20001` | 文本 | 字幕文本 |
| `0x22` | 文本 | 字幕文本副本 |

### 时间戳结构

```c
int64_t ts[3] = { t0, startMs, endMs };
// t0      whisper 原始段起始（10ms 单位）
// startMs 起始毫秒（绝对播放位置）
// endMs   结束毫秒，最少 = startMs + 200
```

### 三个必须遵守的点

1. **`0x20000` 的 24 字节必须作为 body 发送**，不能拆成裸报文 —— 这是字幕能否显示的根本
2. **`0x11` ACK 必须在模型加载之前发送**，且 body 为 1 字节，否则播放器不推送音频
3. **`0x20` 偏移 36 的 `uint32` 是时间轴基准**，字幕时间戳必须基于它

---

## 实现细节

### 主循环

```
连接管道 → 收 INIT(0x10) → 立即回 ACK(0x11)
  → 加载模型（whisper_init_from_file_with_params, use_gpu=false）
  → 循环：
      0x20 → 更新语言 / 时间基准
      0x21 → PCM 追加到缓冲
        缓冲 >= 3 秒 → whisper_full 识别 → 推送字幕报文 → 清空缓冲
```

### 时间戳推算

`0x20` 报文约每 4 秒才更新一次，若直接用会滞后 0~4 秒。引擎按**音频帧数实时推算**当前播放位置：

```c
liveMs = baseMs + (已收音频样本数 * 1000 / 16000) - (缓冲样本数 * 1000 / 16000);
```

这样每段字幕的起点即对应缓冲区开头在播放时间轴上的真实位置。

### 低延迟参数

| 参数 | 值 | 作用 |
|---|---|---|
| 缓冲窗口 | 3 秒 | 缩短等待 |
| `audio_ctx` | 768 | 减少编码器计算量（默认 1500） |
| `no_context` | true | 各段独立，避免上下文拖累 |
| `max_tokens` | 32 | 限制单段长度 |

实测在 i5-3210M 上延迟约 5~6 秒。

---

## 常见问题

**Q: 播放器提示 `Cannot create engine -1`**
文件名不是 `main64.exe`，或放置路径不符合宿主程序的引擎目录约定。

**Q: 引擎启动但无字幕**
检查是否满足上面「三个必须遵守的点」，尤其是 `0x20000` 的 24 字节是否作为 body 发送。

**Q: 引擎列表显示该引擎不可用 / 提示缺少组件**
本引擎不依赖任何外部推理库。部分宿主依据目录内的库文件判断引擎可用性，将本引擎放入满足该判断条件的目录即可，无需额外组件。

**Q: 延迟偏高**
- 减小缓冲窗口至 2 秒（可能影响断句完整性）
- 降低 `audio_ctx` 至 512
- 使用 `ggml-tiny` 模型

**Q: 识别准确率不足**
- 换用 `ggml-base.bin` 或 `ggml-small.bin`
- 增大缓冲窗口至 5 秒
- 关闭 `no_context`

---

## 项目结构

```
.
├── src/
│   └── cpu_engine.cpp          引擎源码（单文件）
├── include/
│   ├── whisper.h               编译期接口声明
│   ├── whisper-version.h       版本号（1.9.5）
│   └── ggml/
│       ├── ggml.h
│       ├── ggml-backend.h
│       ├── ggml-alloc.h
│       ├── ggml-cpp.h
│       ├── ggml-cpu.h
│       ├── gguf.h
│       └── ggml-version.h      版本号（0.27.0）
├── docs/
│   ├── BUILD.md                手工编译详解
│   └── RELEASE.md              安装与发布说明
├── dist/
│   └── main64.exe              待发布的成品（不进仓库，仅上传 Releases）
├── build.ps1                   一键编译脚本
├── README.md
├── LICENSE
└── .gitignore
```

> `include/` 下的头文件来自上游 whisper.cpp / ggml（见「许可证」章节），仅提供编译期接口声明，实际实现由编译时引入的 whisper.cpp 源码提供。

`include/` 中两个版本头文件为上游构建时自动生成的产物，此处固化以便直接编译，需与所用 whisper.cpp 版本保持一致。

> 本仓库**不含** whisper.cpp 源码与编译产物。前者需按下方「构建」章节获取，后者发布于 Releases。

---

## 构建

需要自行准备两项外部资源（许可证与体积原因未随仓库分发）：

1. **whisper.cpp 源码**（含 ggml 子模块）放到 `./whisper.cpp/`
2. **w64devkit** 解压到 `./tools/w64devkit/`，或通过参数指定路径

然后执行：

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

产物输出到 `build/main64.exe`。自定义路径：

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 `
    -WhisperDir D:\src\whisper.cpp `
    -W64Devkit  D:\tools\w64devkit\bin `
    -Arch       ivybridge
```

逐步手工编译的命令见 [docs/BUILD.md](docs/BUILD.md)。

---

## 许可证

本项目代码采用 **MIT License**。

依赖项：

| 项目 | 许可证 |
|---|---|
| [whisper.cpp](https://github.com/ggerganov/whisper.cpp) | MIT |
| [ggml](https://github.com/ggerganov/ggml) | MIT |
| [w64devkit](https://github.com/skeeto/w64devkit) | MIT |

### 第三方头文件

`include/` 目录下的头文件来自 [whisper.cpp](https://github.com/ggerganov/whisper.cpp)（v1.9.5）与 [ggml](https://github.com/ggerganov/ggml)（v0.27.0），版权归其原作者所有，按 MIT License 分发。此处收录仅为固定编译期接口，便于在无网络或离线环境下构建。

若需获取完整源码及原始许可证文本，请访问上述上游仓库。

> 本仓库**不包含**任何播放器厂商的二进制文件、源代码或资源。本实现仅用于在兼容环境中提供语音识别功能。

---

## 免责声明

- 本项目为独立实现的语音识别组件，与任何播放器厂商无关联、无授权关系
- 使用时请遵守所用播放器的最终用户许可协议
- 仅供学习研究与个人使用，作者不对任何使用后果承担责任