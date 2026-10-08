# ccz-audio-fix

曹操传 MOD 音效流畅启动器。它把旧版游戏主线程中的
`sndPlaySoundA` 文件播放请求转移到后台，并直接使用 MOD 自带的
`WAV` 文件，减少攻击、技能、出场和移动音效造成的画面停顿。

最终产物是一个 32 位单文件 Windows EXE，不需要安装 Python、
.NET 或额外运行库。

## 使用方法

1. 把 `Ekd5-audio-fix.exe` 放进 MOD 主目录。
2. 如果游戏程序名是 `Ekd5.exe`，直接运行启动器。
3. 如果游戏程序使用其他名称，创建 `ccz_audio_fix.ini`：

```ini
[game]
exe="你的游戏程序.exe"
```

目标程序选择优先级：

1. 拖到启动器上的 EXE
2. `ccz_audio_fix.ini` 中配置的 EXE
3. 启动器同目录的 `Ekd5.exe`

配置文件支持 UTF-8（有无 BOM 均可）、UTF-16 LE 和系统 ANSI 编码。

## 处理方式

- 启动器创建游戏进程并注入内置的 32 位音效 Hook DLL。
- Hook 动态定位游戏使用的 `sndPlaySoundA` 地址，不依赖固定版本地址。
- 启动时预读 `WAV\*.wav`，保持源文件的采样率、位深和声道格式。
- 音效请求通过后台队列播放，游戏主线程立即返回。
- 同一个音效仍在播放时忽略重复触发，避免移动马蹄声被连续重播。
- 不修改游戏 EXE、存档、WAV、注册表或系统服务。
- 游戏退出后，启动器清理释放到临时目录的 Hook DLL。

## 项目结构

```text
src/
  common/      启动器与 Hook 共用的通信协议
  hook/        WAV 缓存、播放队列及 sndPlaySoundA Hook
  launcher/    配置读取、目标解析、进程启动和 DLL 注入
resources/     Windows 资源、清单和版本信息
assets/        启动器图标
scripts/       本地和 CI 构建测试脚本
tests/         可重复执行的配置解析回归测试
```

## 构建

需要安装带“使用 C++ 的桌面开发”组件的 Visual Studio。构建脚本会通过
`vswhere` 自动寻找最新安装版本，并生成 32 位程序。

```bat
scripts\build.bat
```

输出文件：

```text
release\Ekd5-audio-fix.exe
```

## 测试

```bat
scripts\test.bat
```

测试会验证无 BOM UTF-8 配置文件能够正确解析中文游戏文件名。
GitHub Actions 也会在 Windows x86 MSVC 环境中执行构建和测试。

## 兼容范围

当前实现面向 32 位、使用 `sndPlaySoundA` 播放 `WAV` 音效的曹操传
MOD。启动器会在无法找到兼容接口时停止游戏并显示错误，不会在未知
目标上静默运行。
