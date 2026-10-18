#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// RFB / VNC 标准认证用的 DES（ECB，单数据块）。
//
// 为什么自己写一份：Qt / 系统都没给 DES；OpenSSL 的 EVP 在管理端不一定编进来。
// VNC 认证的算法是「标准 DES ECB」但有个 VNC 专属小动作：把密码的 **每个字节 bit 顺序反转**
// 当作 64 位密钥（DES 本身忽略每字节第 8 位，正好吃掉反转后的那一位）。
// 见 rfb_client.cpp 的 doVncAuth()。
//
// 本文件 **不依赖 Qt**，可独立编译做单元自测（tools/test_rfb_des.cpp 对 NIST 向量）。
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <cstring>

// 本文件可在「带 Qt」与「纯 C++ 单测」两种环境编译：带 Qt 时 quint8 由 Qt 提供，
// 纯单测（tools/test_rfb_des.cpp）下自己补一个等价别名。
#ifndef QT_VERSION
using quint8 = std::uint8_t;
#endif

namespace RfbDes {

static inline int  desGetBit(const quint8 *b, int i) { return (b[(i - 1) >> 3] >> (7 - ((i - 1) & 7))) & 1; }
static inline void desSetBit(quint8 *b, int i, int v) {
    if (v) b[(i - 1) >> 3] |=  quint8(1) << (7 - ((i - 1) & 7));
    else   b[(i - 1) >> 3] &= ~(quint8(1) << (7 - ((i - 1) & 7)));
}

// 把 src 按 table（1-based 位序，n 项）搬到 dst
static inline void desPerm(const quint8 *src, const int *table, int n, quint8 *dst) {
    for (int i = 1; i <= n; ++i) desSetBit(dst, i, desGetBit(src, table[i - 1]));
}

// 28 位循环左移（C/D 各 28 位，存在 64 位块的第 [0..27] / [28..55]）
static inline void desRot28(quint8 *k56, int lo, int hi, int shift) {
    for (int s = 0; s < shift; ++s) {
        int first = desGetBit(k56, lo);
        for (int i = lo; i < hi; ++i) desSetBit(k56, i, desGetBit(k56, i + 1));
        desSetBit(k56, hi, first);
    }
}

static const int kPC1[56] = {
    57,49,41,33,25,17,9, 1,58,50,42,34,26,18,
    10,2,59,51,43,35,27, 19,11,3,60,52,44,36,
    63,55,47,39,31,23,15, 7,62,54,46,38,30,22,
    14,6,61,53,45,37,29, 21,13,5,28,20,12,4
};
static const int kPC2[48] = {
    14,17,11,24,1,5, 3,28,15,6,21,10,
    23,19,12,4,26,8, 16,7,27,20,13,2,
    41,52,31,37,47,55, 30,40,51,45,33,48,
    44,49,39,56,34,53, 46,42,50,36,29,32
};
static const int kRot[16] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};
static const int kE[48] = {
    32,1,2,3,4,5, 4,5,6,7,8,9, 8,9,10,11,12,13,
    12,13,14,15,16,17, 16,17,18,19,20,21,
    20,21,22,23,24,25, 24,25,26,27,28,29, 28,29,30,31,32,1
};
static const int kP[32] = {
    16,7,20,21,29,12,28,17, 1,15,23,26,5,18,31,10,
    2,8,24,14,32,27,3,9, 19,13,30,6,22,11,4,25
};
static const int kIP[64] = {
    58,50,42,34,26,18,10,2, 60,52,44,36,28,20,12,4,
    62,54,46,38,30,22,14,6, 64,56,48,40,32,24,16,8,
    57,49,41,33,25,17,9,1, 59,51,43,35,27,19,11,3,
    61,53,45,37,29,21,13,5, 63,55,47,39,31,23,15,7
};
static const int kFP[64] = {
    40,8,48,16,56,24,64,32, 39,7,47,15,55,23,63,31,
    38,6,46,14,54,22,62,30, 37,5,45,13,53,21,61,29,
    36,4,44,12,52,20,60,28, 35,3,43,11,51,19,59,27,
    34,2,42,10,50,18,58,26, 33,1,41,9,49,17,57,25
};

static const int kS[8][64] = {
    {14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7, 0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
     4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0, 15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
    {15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10, 3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
     0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15, 13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
    {10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8, 13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
     13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7, 1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
    {7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15, 13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
     10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4, 3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
    {2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9, 14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
     4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14, 11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
    {12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11, 10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
     9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6, 4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
    {4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1, 13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
     1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2, 6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
    {13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7, 1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,
     7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8, 2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}
};

// 单个 64 位块 ECB 加密（key 为 8 字节原始密钥；VNC 调用方负责先反转每位字节）
static inline void desEncryptBlock(const quint8 key[8], const quint8 in[8], quint8 out[8])
{
    quint8 k56[8], sub[16][8];
    memset(k56, 0, sizeof(k56));
    memset(out, 0, 8);

    // 1) 密钥调度：PC1 → 56 位 → 16 轮循环左移 → PC2
    quint8 tmp[8]; memset(tmp, 0, sizeof(tmp));
    desPerm(key, kPC1, 56, k56);
    for (int r = 0; r < 16; ++r) {
        desRot28(k56, 1, 28, kRot[r]);     // C（位 1..28）
        desRot28(k56, 29, 56, kRot[r]);    // D（位 29..56）
        memset(sub[r], 0, sizeof(sub[r]));
        desPerm(k56, kPC2, 48, sub[r]);
    }

    // 2) 初始置换 IP
    quint8 m[8]; memset(m, 0, sizeof(m));
    desPerm(in, kIP, 64, m);

    // 3) 16 轮 Feistel（L0/R0 = m 的前/后 32 位）
    quint8 L[8], R[8], nL[8], nR[8], f[8], ex[8], px[8];
    for (int half = 0; half < 2; ++half) {
        for (int i = 1; i <= 32; ++i) {
            if (half == 0) desSetBit(L, i, desGetBit(m, i));
            else           desSetBit(R, i, desGetBit(m, i + 32));
        }
    }
    for (int r = 0; r < 16; ++r) {
        // f(R, K) = P( S( E(R) xor K ) )
        memset(ex, 0, sizeof(ex));
        desPerm(R, kE, 48, ex);
        for (int i = 1; i <= 48; ++i)
            desSetBit(ex, i, desGetBit(ex, i) ^ desGetBit(sub[r], i));
        memset(px, 0, sizeof(px));
        for (int sb = 0; sb < 8; ++sb) {
            int base = sb * 6;
            int b1 = desGetBit(ex, base + 1), b6 = desGetBit(ex, base + 6);
            int row = (b1 << 1) | b6;
            int col = (desGetBit(ex, base + 2) << 3) | (desGetBit(ex, base + 3) << 2)
                    | (desGetBit(ex, base + 4) << 1) | desGetBit(ex, base + 5);
            int v = kS[sb][row * 16 + col];
            for (int bi = 0; bi < 4; ++bi) desSetBit(px, sb * 4 + bi + 1, (v >> (3 - bi)) & 1);
        }
        memset(f, 0, sizeof(f));
        desPerm(px, kP, 32, f);
        // nR = L xor f ; nL = R
        for (int i = 1; i <= 32; ++i)
            desSetBit(nR, i, desGetBit(L, i) ^ desGetBit(f, i));
        memcpy(nL, R, sizeof(R));
        memcpy(L, nL, sizeof(L));
        memcpy(R, nR, sizeof(R));
    }
    // 4) 末轮交换：输出 = R16 || L16
    memset(m, 0, sizeof(m));
    for (int i = 1; i <= 32; ++i) {
        desSetBit(m, i, desGetBit(R, i));          // R16
        desSetBit(m, i + 32, desGetBit(L, i));     // L16
    }
    // 5) 逆初始置换 FP
    desPerm(m, kFP, 64, out);
}

// VNC 认证响应：challenge(16) → response(16)
// 密码填充到 8 字节，每字节 bit 反转作为 DES 密钥，对 challenge 做 2 次 ECB 加密。
static inline void vncAuthResponse(const char *password, const quint8 challenge[16], quint8 response[16])
{
    quint8 key[8];
    memset(key, 0, sizeof(key));
    // VNC 密码最多 8 字节，超出截断、不足补 0。
    // 注意：必须按 strlen 截断，绝不能读 password[8] 之外的字节（越界读到垃圾会污染第 8 密钥字节，
    // 导致与真实 VNC 服务端校验失败）。之前的实现 password[i] 在短密码时会读穿 null 终止符。
    int plen = 0;
    if (password) { while (password[plen] != '\0' && plen < 8) ++plen; }
    for (int i = 0; i < 8; ++i) {
        quint8 c = (quint8)(i < plen ? password[i] : 0);
        // 字节 bit 反转
        c = quint8((c & 0xF0) >> 4) | quint8((c & 0x0F) << 4);
        c = quint8((c & 0xCC) >> 2) | quint8((c & 0x33) << 2);
        c = quint8((c & 0xAA) >> 1) | quint8((c & 0x55) << 1);
        key[i] = c;
    }
    quint8 in[8], out[8];
    for (int i = 0; i < 16; i += 8) {
        memcpy(in, challenge + i, 8);
        desEncryptBlock(key, in, out);
        memcpy(response + i, out, 8);
    }
}

} // namespace RfbDes
