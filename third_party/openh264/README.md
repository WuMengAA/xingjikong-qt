# OpenH264（Cisco）· Windows x64 预编译件

星集控被控端 **H264 软件编码兜底**用的第三方二进制。2026-11-01 引入。

## 为什么有它

被控端 H264 推流原本只用 **Media Foundation 硬件编码**（`src/media/H264EncoderMf.cpp`，
零第三方依赖）。2026-11-01 在**真实桌面会话（Session 1）**上实测本机：

- 枚举到 1 个 `H264 Encoder MFT`，输出/输入类型协商**都成功**，`ProcessInput` 前 18 帧也成功；
- 但 `ProcessOutput` 恒返 `MF_E_TRANSFORM_NEED_MORE_INPUT`，**drain / EOS 之后仍 0 产出**；
- 深度探针已排除「喂帧不够 / 异步未 drain / profile 缺失」三因；
- 本机 GPU = AMD Radeon(TM) Graphics（APU）+ GameViewer Virtual Display Adapter。

⇒ **MF 硬件 H264 编码在这类环境（虚拟化 / 无独显编码）协商得成却不产帧**，
教室机若是同类环境会一模一样失败。故加一条纯软件路径兜底：
`src/media/H264EncoderOh264.cpp`（运行时动态加载本 dll）。

OpenH264 是 **Cisco 的 BSD 许可**实现（不是 GPL），与项目红线
「FFmpeg 必须 LGPL + --disable-gpl，libx264/x265 禁用」不冲突。

## 文件

| 文件 | 说明 |
|---|---|
| `openh264-2.7.0-win64.dll` | Windows x64 编码库（运行时**动态加载**，不链接） |
| `codec_api.h` `codec_def.h` `codec_ver.h` `codec_app_def.h` | OpenH264 公开头（Cisco BSD） |
| `LICENSE` | Cisco BSD 许可原文（再分发必须随附） |

## 来源与校验

- 官方 CDN：`https://ciscobinary.openh264.org/openh264-2.7.0-win64.dll.bz2`
  （GitHub releases 上**没有**直链，2026-11-01 实测 404 —— 走 Cisco 官方 CDN）
- 头文件：`https://raw.githubusercontent.com/cisco/openh264/v2.7.0/codec/api/wels/`
- dll sha256：

```
0ef5e7f3b38753b15c13afa0418d4d7805fa228e6adfd3e425bbf758cff1c288
openh264-2.7.0-win64.dll
```

```
sha256(openh264-2.7.0-win64.dll)
0ef5e7f3b38753b15c13afa0418d4d7805fa228e6adfd3e425bbf758cff1c288
```

> 更新版本时的做法：换 URL 下新的 `.dll.bz2` → bz2 解压 → 同步替换上表 4 个头文件
> （版本必须与 dll 一致，否则 ABI 不匹配）→ 更新本文件与 `codec_ver.h` → 重新出包。

## 集成方式（关键，别改回链接方式）

**必须运行时 `LoadLibrary` + `GetProcAddress`**，不能链接 `.lib`：
用 `dumpbin` 从 dll 生成的 import lib，其导入名是 `openh264-2.7.0-win64`，
程序启动按该名去找 dll 会失败（0xC0000135 找不到模块）。
动态加载还顺带得到「dll 缺失时可优雅降级」——软编不是必需品，缺了回落 VNC。

## 许可

Cisco BSD 2-Clause（见 `LICENSE`）。可再分发，条件是随附版权声明与许可文本。
