# 共享 QML 副本校验（两端构建期门禁）
#
# 用意：共享视觉层的**唯一天源**在 Stelarith-shared-qml/{island,ui}/。两端在
# POST_BUILD 把它 copy_directory 到各自产物目录。拷贝是「复制」语义 —— 若产物里
# 残留旧件、或有人手改了副本，构建**不会报错**，但运行期会是另一份（最难查的一类：
# 日志正常、界面就是不对）。本脚本在拷贝之后立刻逐文件比对 MD5：不一致就让构建 FAIL。
#
# 用法（由各工程 CMakeLists 的 POST_BUILD 调用）：
#   cmake -DSRC=<真源目录> -DDST=<产物副本目录> [-DLABEL=ui] [-DSUBSET=ON]
#     -P check-island-parity.cmake
#
# 两种模式：
#   · 默认（严格，双向）：产物里**多出**真源没有的件也算「本地残留」⇒ FAIL。
#     适用于 DST 是**专供共享件**的目录（如 qml/island/ 只放岛）。
#   · -DSUBSET=ON（子集）：只查「真源每一件都在产物里且内容一致」，**不**追究产物多出。
#     适用于 DST 与工程自有件**混放**的目录（如 qml/components/ 里除共享的 Md3 基座
#     外还有各端专属组件）—— 那种目录本来就该有多出来的件，不是残留。
#
# ⚠️ 2026-10-16（island 系统性升级 · U1）：本脚本原为岛专用（消息前缀 [island-parity]），
#    U1 起共享 UI 基座（Md3 设计系统）也走它 ⇒ 参数化 LABEL（消息前缀）与 SUBSET（模式），
#    文件名保留不动（既有调用点少改一处是一处）。

if(NOT DEFINED SRC OR NOT DEFINED DST)
  message(FATAL_ERROR "[shared-qml-parity] 需要 -DSRC=<真源> 与 -DDST=<产物副本>")
endif()
if(NOT DEFINED LABEL OR LABEL STREQUAL "")
  set(LABEL "shared-qml")
endif()
set(_pfx "[${LABEL}-parity]")

if(NOT IS_DIRECTORY "${SRC}")
  message(FATAL_ERROR "${_pfx} 真源目录不存在：${SRC}")
endif()
if(NOT IS_DIRECTORY "${DST}")
  message(FATAL_ERROR
    "${_pfx} 产物副本不存在：${DST}\n"
    "  说明 POST_BUILD 的 copy_directory 没跑到（或目标路径写错）。")
endif()

file(GLOB_RECURSE _src_files RELATIVE "${SRC}" "${SRC}/*")
set(_bad "")

# 正向：真源每一件都必须在产物里且内容一致
foreach(_f ${_src_files})
  if(NOT EXISTS "${DST}/${_f}")
    list(APPEND _bad "${_f} —— 产物缺件")
  else()
    file(MD5 "${SRC}/${_f}" _s)
    file(MD5 "${DST}/${_f}" _d)
    if(NOT _s STREQUAL _d)
      list(APPEND _bad "${_f} —— 内容不一致（真源 ${_s} / 产物 ${_d}）")
    endif()
  endif()
endforeach()

# 反向：产物里不该有真源没有的件（本地残留会静默覆盖运行期行为）。
# SUBSET 模式跳过 —— DST 与工程自有件混放时，多出来的（非残留）本就正常。
if(NOT SUBSET)
  file(GLOB_RECURSE _dst_files RELATIVE "${DST}" "${DST}/*")
  foreach(_f ${_dst_files})
    if(NOT EXISTS "${SRC}/${_f}")
      list(APPEND _bad "${_f} —— 产物多出（真源无此件，疑为本地残留）")
    endif()
  endforeach()
endif()

list(LENGTH _src_files _n)
if(_bad)
  string(REPLACE ";" "\n    " _badtxt "${_bad}")
  message(FATAL_ERROR
    "${_pfx} 共享副本与真源不一致：\n    ${_badtxt}\n"
    "  真源：${SRC}\n"
    "  产物：${DST}\n"
    "  ⚠️ 不要手改产物副本 —— 要改就改 Stelarith-shared-qml/ 下的真源再重新构建。")
endif()

if(SUBSET)
  message(STATUS "${_pfx} OK —— ${_n} 件与真源逐一一致（子集模式，${DST}）")
else()
  message(STATUS "${_pfx} OK —— ${_n} 件与真源逐一一致（${DST}）")
endif()
