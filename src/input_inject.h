#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// 输入注入（被控端 in-agent VNC Server 用）
//
// 把 RFB 客户端的指针/键盘事件翻译成被控端本机输入：
//   · 鼠标：RFB buttonMask（bit0=左 bit1=中 bit2=右）+ 绝对坐标 → SendInput 绝对定位。
//   · 键盘：RFB/X11 keysym → Windows VK（VkKeyScanW + 特殊键表）→ SendInput。
//
// 跨平台：非 Windows 提供空实现（待补 X11 / uinput）。
// ⚠️ v1 keymap 为常用子集（可打印 Latin-1 + 方向/功能/编辑键 + Shift/Ctrl/Alt/Win）；
//   死键、IME、组合键等联调补全。详见 input_inject.cpp 头注释。
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>

class InputInject {
public:
    /// @param buttonMask RFB 按钮位：bit0=左 bit1=中 bit2=右
    /// @param x,y       绝对屏幕坐标（像素）
    static void mouseEvent(uint32_t buttonMask, int x, int y);

    /// @param down  true=按下 false=抬起
    /// @param keySym RFB/X11 keysym（ Latin-1 区与 Unicode 码点对齐）
    static void keyEvent(bool down, uint32_t keySym);
};
