// 输入注入实现（Windows: SendInput；其它平台: 空实现）。详见 input_inject.h。
//
// 键盘映射策略（v1，子集）：
//   · keySym ∈ [0x20,0xFF]（可打印 Latin-1）：用 VkKeyScanW 求 VK（含 WCHAR 提示的 shift 态，
//     但 v1 不单独补发 shift——避免与客户端显式发的 Shift keysym 叠加造成卡键；客户端一般已按
//     物理键发对应 keysym）。
//   · 特殊键（BackSpace/Tab/Return/Esc/方向/Home/End/PageUp/Down/Print/F1–F12/Shift/Ctrl/Alt/Win）
//     走静态表。
//   · 组合键、死键、IME 联调补全。

#include "input_inject.h"

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static uint32_t s_prevMask = 0; // 上一次按钮位，用于只对变化的键发 down/up

static void sendMouse(uint32_t buttonMask, int x, int y) {
    const int sx = GetSystemMetrics(SM_CXSCREEN);
    const int sy = GetSystemMetrics(SM_CYSCREEN);

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = sx ? static_cast<LONG>(x * 65535 / sx) : 0;
    in.mi.dy = sy ? static_cast<LONG>(y * 65535 / sy) : 0;
    in.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;

    const uint32_t changed = buttonMask ^ s_prevMask;
    if (changed & 0x1) in.mi.dwFlags |= (buttonMask & 0x1) ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    if (changed & 0x2) in.mi.dwFlags |= (buttonMask & 0x2) ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    if (changed & 0x4) in.mi.dwFlags |= (buttonMask & 0x4) ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    s_prevMask = buttonMask;

    SendInput(1, &in, sizeof(in));
}

static void sendKey(bool down, uint32_t keySym) {
    WORD vk = 0;
    switch (keySym) {
        case 0xFF08: vk = VK_BACK; break;      // BackSpace
        case 0xFF09: vk = VK_TAB; break;       // Tab
        case 0xFF0D: vk = VK_RETURN; break;    // Return
        case 0xFF1B: vk = VK_ESCAPE; break;    // Escape
        case 0xFF50: vk = VK_HOME; break;      // Home
        case 0xFF51: vk = VK_LEFT; break;      // Left
        case 0xFF52: vk = VK_UP; break;        // Up
        case 0xFF53: vk = VK_RIGHT; break;     // Right
        case 0xFF54: vk = VK_DOWN; break;      // Down
        case 0xFF55: vk = VK_PRIOR; break;     // PageUp
        case 0xFF56: vk = VK_NEXT; break;      // PageDown
        case 0xFF57: vk = VK_END; break;       // End
        case 0xFF63: vk = VK_SNAPSHOT; break;  // PrintScreen
        case 0xFF8D: vk = VK_RETURN; break;    // KP_Enter
        case 0xFFE1: vk = VK_LSHIFT; break;    // Shift(左)
        case 0xFFE2: vk = VK_RSHIFT; break;    // Shift(右)
        case 0xFFE3: vk = VK_LCONTROL; break;  // Control(左)
        case 0xFFE4: vk = VK_RCONTROL; break;  // Control(右)
        case 0xFFE9: vk = VK_LMENU; break;     // Alt(左)
        case 0xFFEA: vk = VK_RMENU; break;     // Alt(右)
        case 0xFFE7: vk = VK_LWIN; break;      // Meta(左)
        case 0xFFE8: vk = VK_RWIN; break;      // Meta(右)
        default:
            if (keySym >= 0xFFBE && keySym <= 0xFFC9) vk = static_cast<WORD>(VK_F1 + (keySym - 0xFFBE)); // F1..F12
            else if (keySym >= 0x20 && keySym <= 0xFF) vk = static_cast<WORD>(VkKeyScanW(static_cast<WCHAR>(keySym)) & 0xFF);
            break;
    }
    if (vk == 0) return;

    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

void InputInject::mouseEvent(uint32_t buttonMask, int x, int y) { sendMouse(buttonMask, x, y); }
void InputInject::keyEvent(bool down, uint32_t keySym) { sendKey(down, keySym); }

#else // ── 非 Windows：空实现 ──
void InputInject::mouseEvent(uint32_t, int, int) {}
void InputInject::keyEvent(bool, uint32_t) {}
#endif
