# 构建说明

本文件说明如何从源码构建 `main64.exe`。

> **重要**：本文的编译参数经过实测验证。其中 `-UWHISPER_USE_COREML` 与 `-DGGML_USE_CPU=1` 两项不可省略，省略会导致引擎在模型加载阶段失败（详见下方「常见编译陷阱」）。

---

## 前置条件

### 1. 编译器

推荐 [w64devkit](https://github.com/skeeto/w64devkit)（自带 g++、静态库、make，解压即用）。

### 2. whisper.cpp 源码

```powershell
git clone --depth 1 --branch v1.9.5 --recursive https://github.com/ggerganov/whisper.cpp
```

`--recursive` 不可省略，ggml 是子模块，漏掉会导致源码树不完整。

本构建实际用到的文件：

```
whisper.cpp/
├── include/whisper.h
├── src/
│   ├── whisper.cpp
│   └── aneforge/whisper-aneforge.cpp     ← 必需，易漏
└── ggml/
    ├── include/                          ← ggml.h / ggml-backend.h 等
    └── src/
        ├── ggml.c / ggml.cpp / ggml-alloc.c / ggml-quants.c
        ├── ggml-backend.cpp / ggml-backend-meta.cpp
        ├── ggml-backend-reg.cpp / ggml-backend-dl.cpp
        ├── ggml-opt.cpp / ggml-threading.cpp / gguf.cpp
        └── ggml-cpu/
            ├── ggml-cpu.cpp / ggml-cpu.c / ops.cpp
            ├── binary-ops.cpp / unary-ops.cpp / vec.cpp
            ├── quants.c / traits.cpp / hbm.cpp / repack.cpp
            ├── tiled/tiled.cpp              ← 必需，易漏
            ├── tiled/tiled-kernel.cpp       ← 必需，易漏
            └── arch/x86/
                ├── quants.c
                ├── repack.cpp
                └── cpu-feats.cpp
```

> **两个易漏目录**：`src/aneforge/` 与 `ggml/src/ggml-cpu/tiled/`。它们不在任何标准编译清单里，但 `whisper.cpp` 与 `ggml-cpu.c` 无条件引用了其中的函数。遗漏时**编译阶段无报错**，只在链接阶段报 `undefined reference`，常被误判为工具链问题。

### 3. 版本头文件

`whisper.cpp` 与 `ggml` 依赖两个构建时自动生成的头文件，源码树中通常不含，本项目已固化为：

| 文件 | 内容 |
|---|---|
| `include/whisper-version.h` | `#define WHISPER_VERSION "1.9.5"` |
| `include/ggml/ggml-version.h` | `#define GGML_VERSION "0.27.0"` |

编译时用 `-include include/whisper-version.h` 引入，`ggml-version.h` 则通过 `-Iinclude/ggml` 由 `ggml.c` 自行包含。

**版本号必须与所用 whisper.cpp 一致**，否则可能因接口差异编译失败。

---

## 编译参数（关键部分）

本项目的静态链接方式与常规 cmake 构建有本质区别：**不依赖任何预先构建的 `.a` 包**，而是把用到的每一个 `.cpp` / `.c` 源文件逐个编译成 `.o`，最后统一链接。

这样做可以精确控制哪些目标文件参与链接，避免静态库未被引用导致的后端注册表为空问题。

### 全局编译宏

| 宏 | 必须 | 作用 |
|---|---|---|
| `-DGGML_USE_CPU=1` | **是** | 启用 CPU 后端注册。缺失会导致 `ggml_backend_reg_count()` 返回 0，模型初始化崩溃 |
| `-UWHISPER_USE_COREML` | **是** | 显式取消 CoreML 宏。见下方陷阱说明 |
| `-UWHISPER_USE_VULKAN` | 是 | 取消 Vulkan 后端 |
| `-UWHISPER_USE_CUDA` | 是 | 取消 CUDA 后端 |
| `-DNDEBUG` | 建议 | 关闭断言，提升性能 |
| `-march=ivybridge` | 建议 | 目标指令集，覆盖 SSE4.2 / AVX / F16C |
| `-mf16c` | 建议 | 启用半精度浮点指令，F16 模型推理依赖此指令 |

### 必须编译的源文件清单

ggml-cpu 模块在较新版本中已拆分为多个子模块，逐个编译才能覆盖全部算子。

| 类别 | 源文件 | 目标文件 |
|---|---|---|
| whisper 核心 | `src/whisper.cpp` | `whisper.o` |
| 可选编码器 | `src/aneforge/whisper-aneforge.cpp` | `aneforge.o` |
| ggml 基础 | `ggml/src/ggml.c` | `ggml.o` |
| | `ggml/src/ggml.cpp` | `ggml-cpp.o` |
| | `ggml/src/ggml-alloc.c` | `ggml-alloc.o` |
| | `ggml/src/ggml-backend.cpp` | `ggml-backend.o` |
| | `ggml/src/ggml-backend-reg.cpp` | `ggml-backend-reg.o` |
| | `ggml/src/ggml-backend-meta.cpp` | `ggml-backend-meta.o` |
| | `ggml/src/ggml-backend-dl.cpp` | `ggml-backend-dl.o` |
| | `ggml/src/ggml-quants.c` | `ggml-quants.o` |
| | `ggml/src/ggml-threading.cpp` | `ggml-threading.o` |
| | `ggml/src/ggml-opt.cpp` | `ggml-opt.o` |
| gguf 读取 | `ggml/src/gguf.cpp` | `gguf.o` |
| ggml-cpu 入口 | `ggml/src/ggml-cpu/ggml-cpu.cpp` | `cpu-entry.o` |
| ggml-cpu 算子 | `ggml/src/ggml-cpu/ggml-cpu.c` | `cpu-core.o` |
| | `ggml/src/ggml-cpu/ops.cpp` | `cpu-ops.o` |
| | `ggml/src/ggml-cpu/binary-ops.cpp` | `cpu-bin.o` |
| | `ggml/src/ggml-cpu/unary-ops.cpp` | `cpu-uni.o` |
| | `ggml/src/ggml-cpu/vec.cpp` | `cpu-vec.o` |
| | `ggml/src/ggml-cpu/quants.c` | `cpu-quants.o` |
| | `ggml/src/ggml-cpu/traits.cpp` | `cpu-traits.o` |
| | `ggml/src/ggml-cpu/hbm.cpp` | `cpu-hbm.o` |
| tiled 分块算子 | `ggml/src/ggml-cpu/tiled/tiled.cpp` | `tiled.o` |
| | `ggml/src/ggml-cpu/tiled/tiled-kernel.cpp` | `tiled-kernel.o` |
| x86 内核 | `ggml/src/ggml-cpu/arch/x86/quants.c` | `arch-x86-quants.o` |
| | `ggml/src/ggml-cpu/arch/x86/cpu-feats.cpp` | `arch-x86-feats.o` |

共 **26** 个源文件。完整清单可直接参考 `build.ps1` 中的 `$SOURCES` 表。

> 清单中**不含** `repack.cpp` 与 `arch/x86/repack.cpp`，原因见下方「关于 repack」。

> **三处最易遗漏**：
> - `arch-x86-quants.o`（AVX/F16C 量化矩阵乘法内核）缺失会导致链接阶段大量 `ggml_vec_dot_*` 未定义
> - `tiled.o` / `tiled-kernel.o` 缺失会导致 `ggml_tiled_wdata_*` 未定义
> - `aneforge.o` 缺失会导致 `whisper_aneforge_*` 未定义
>
> 以上均属**链接阶段**报错。真正会导致产物无法运行的是编译宏 `-DGGML_USE_CPU=1` 遗漏——那会让 `ggml_backend_reg_count()` 返回 0，模型初始化时崩溃在断言处，且编译链接全程无任何报错。

---

## 编译步骤

以下命令以 PowerShell 为例，`$W64` 指向 w64devkit 的 `bin` 目录。

### 步骤 1：编译各目标文件

```powershell
$W64 = 'tools\w64devkit\bin'
$WS  = 'whisper.cpp'

$COMMON = @(
  '-O2','-march=ivybridge','-mavx','-mf16c','-DNDEBUG',
  '-DGGML_USE_CPU=1',
  '-UWHISPER_USE_COREML','-UWHISPER_USE_VULKAN','-UWHISPER_USE_CUDA',
  "-I$WS\include","-I$WS\ggml\include",
  "-I$WS\ggml\src","-I$WS\ggml\src\ggml-cpu","-I$WS\src"
)
```

对每个源文件分别执行，**按扩展名选择编译器**：

```powershell
# .cpp 用 g++
& "$W64\g++.exe" $COMMON -c "<源文件>.cpp" -o "<目标>.o"

# .c  用 gcc
& "$W64\gcc.exe"  $COMMON -c "<源文件>.c"   -o "<目标>.o"
```

> **`.c` 文件不能用 `g++` 编译**。`g++` 按 C++ 规则检查，而 ggml 的 C 源文件大量存在 `void*` 隐式转换，会报数十个 `invalid conversion from 'void*' to 'xxx*' [-fpermissive]` 直接失败。必须用 `gcc.exe`。

### 步骤 2：链接引擎

本项目**不打包静态库**，直接把全部 `.o` 交给链接器。这样可避免静态库内未被引用的目标文件被丢弃，从而保证 CPU 后端注册代码一定参与链接。

```powershell
$LINK = @(
  '-O2','-march=ivybridge','-mavx','-mf16c','-DNDEBUG',
  '-DGGML_USE_CPU=1',
  '-UWHISPER_USE_COREML','-UWHISPER_USE_VULKAN','-UWHISPER_USE_CUDA',
  '-include', 'include\whisper-version.h',
  "-I$WS\include","-I$WS\ggml\include",
  '-Iinclude','-Iinclude\ggml','-Isrc',
  'src\cpu_engine.cpp',
  '-Wl,--start-group'
) + $allObjs + @(
  '-Wl,--end-group',
  '-static','-o','main64.exe',
  '-lshlwapi','-lole32','-lrpcrt4','-lws2_32'
)
& "$W64\g++.exe" @LINK
```

`-Wl,--start-group` 必须包裹所有 `.o`。ggml 与 whisper 之间存在大量循环依赖，不加这一对参数会报符号未定义。

> PowerShell 中不能用 `@allObjs` 直接嵌入数组字面量（会报 `SplattingNotPermitted`），也建议避免使用 `ar.exe` 打包——w64devkit 自带的 `ar` 解析参数时只取首字符，遇到 `rcs` 或盘符路径会失败。

链接阶段的编译参数必须与编译阶段完全一致，否则 `whisper.h` 中的结构体布局可能不一致。

---

## 参数说明

| 参数 | 作用 |
|---|---|
| `-O2` | 优化级别 |
| `-march=ivybridge` | 目标指令集；如需更广兼容可改为 `-march=x86-64-v2`（但将失去 AVX） |
| `-Wl,--start-group ... --end-group` | 解决静态库间的循环依赖 |
| `-static` | 静态链接运行时，产物自包含，无需分发 DLL |
| `-lshlwapi -lole32 -lrpcrt4 -lws2_32` | ggml 与 IPC 依赖的 Windows 系统库 |

---

## 常见编译陷阱

### 陷阱一：`-DXXX=0` 不等于关闭

这是最容易踩的坑。C 预处理器判断的是**宏是否被定义**，而不是它的值：

```c
#ifdef WHISPER_USE_COREML   // 只看"有没有定义"
```

因此 `-DWHISPER_USE_COREML=0` **仍然会让该宏处于"已定义"状态**，所有 CoreML 代码照常参与编译。若此时没有可用的 CoreML 实现，`whisper_init_from_file_with_params()` 会返回 `NULL`，引擎报 `FAIL load model` 后退出。

**正确写法**是 `-UWHISPER_USE_COREML`（取消定义）。

检查方式：

```powershell
& "$W64\nm.exe" -C main64.exe | Select-String 'coreml'
```

输出为空才说明 CoreML 代码确实未被编译进来。

### 陷阱二：CPU 后端注册表为空

若模型初始化时在 `ggml.h` 的断言处崩溃，或日志显示 `ggml_backend_reg_count()` 为 0，通常是以下原因：

1. 编译 `ggml-backend-reg.cpp` 时缺少 `-DGGML_USE_CPU=1`
2. `arch/x86/quants.c` 未参与编译
3. 链接时 `--start-group` 范围未包含全部目标文件，导致 `ggml_backend_cpu_reg` 被裁掉

在引擎 `main()` 开头显式调用一次 `ggml_backend_cpu_reg()`，可强制链接器保留该目标文件。

### 陷阱三：PowerShell 逗号解析

PowerShell 会把 `-Wl,--start-group` 中的逗号当作参数分隔符，导致编译失败。解决办法：

**方法一：用数组传参**（本文示例均采用此法）

**方法二：写成 Makefile**，直接 `make`，避免 PowerShell 解析问题。

---

## 产物验证

```
文件名   main64.exe
大小     约 4.69 MB（脚本产出）/ 约 4.34 MB（Releases 成品）
格式     PE32+（x86-64）
子系统   Console
导入表   KERNEL32.dll / msvcrt.dll / ADVAPI32.dll
```

### 关于 repack

ggml 有一个可选的权重预重排机制 `repack`：加载时把量化权重重排成更利于 SIMD 的布局。它由两个源文件实现：

```
ggml/src/ggml-cpu/repack.cpp
ggml/src/ggml-cpu/arch/x86/repack.cpp
```

**本项目不编译这两个文件。** 原因有三：

1. 它仅对量化模型生效。标准 Whisper 模型是 F16 权重，矩阵乘走 `ggml_vec_dot_f16`，不经过该路径。
2. 上游默认关闭（cmake 的 `GGML_CPU_REPACK` 选项默认为 OFF），不定义 `GGML_USE_CPU_REPACK` 时 `ggml-cpu.cpp` 完全不引用 repack 符号，链接器会丢弃整块代码。
3. 编译这两个文件会使产物增大约 0.4 MB，但没有任何实际收益。

因此 `$SOURCES` 中没有 `cpu-repack` 与 `arch-x86-repack` 两项。**请不要自行加回去**——加了不会出错，只是白白增大体积。

验证方式：产物中不应存在 repack 的量化模板族符号。

```powershell
& "$W64\nm.exe" -C main64.exe | Select-String 'tensor_traits'
```

正常情况下仅命中少量符号；若输出数百条 `tensor_traits<block_q1_0, ...>` 之类的记录，说明误编了 repack。

### 体积差异说明

脚本产物约 4.69 MB，Releases 中的成品约 4.34 MB，相差约 0.34 MB。

差异**不在源文件清单**：两版符号表逐一比对完全一致，没有任何一方独有或缺失的函数。差异来自同一批函数在编译优化上的取舍（函数内联边界、循环展开程度不同），体现在 `.text` 段的长度不同。

两版功能等价，体积不同不影响使用。若追求与发布版完全一致，需要完全复刻发布时的全套编译参数，目前没有保留该记录，因此脚本产出会略大一些。

> 这一差异是可接受的：脚本的目的是让任何人从源码编出**能正常工作**的引擎，而非复刻某个特定二进制的字节内容。

---

## 部署

将产物放到宿主程序指定的引擎目录，文件名保持为 `main64.exe`：部分宿主的引擎加载器按固定名称查找可执行文件，改名后可能无法被识别。

完整的安装步骤、模型准备与故障排查见 [RELEASE.md](RELEASE.md)。

运行后在系统临时目录生成 `main64_engine.log`：

```
%TEMP%\main64_engine.log
```

日志中的关键节点：

| 日志行 | 含义 |
|---|---|
| `CONNECTED` | IPC 握手成功 |
| `MODEL LOADED OK` | 模型初始化完成 |
| `TRANSCRIBE <n> samples` | 收到音频并开始识别 |
| `SUB [x/y] <起>-<止>ms: <文本>` | 识别出字幕结果 |