# 共享灵动岛副本校验（两端构建期门禁）
#
# 用意：岛的唯一天源是 Stelarith-shared-qml/island/。两端在 POST_BUILD 把它
# copy_directory 到各自产物目录（被控端 qml/island/、管理端 qml/Stelarith/island/）。
# 拷贝是「复制」语义 —— 若产物里残留旧件、或有人手改了副本，构建**不会报错**，
# 但运行期会是另一份岛（最难查的一类：日志正常、界面就是不对）。
#   本脚本在拷贝之后立刻逐文件比对 MD5：不一致就让构建 FAIL。
#
# 用法（由各工程 CMakeLists 的 POST_BUILD 调用）：
#   cmake -DSRC=<真源目录> -DDST=<产物副本目录> -P check-island-parity.cmake

if(NOT DEFINED SRC OR NOT DEFINED DST)
  message(FATAL_ERROR "[island-parity] 需要 -DSRC=<真源> 与 -DDST=<产物副本>")
endif()
if(NOT IS_DIRECTORY "${SRC}")
  message(FATAL_ERROR "[island-parity] 真源目录不存在：${SRC}")
endif()
if(NOT IS_DIRECTORY "${DST}")
  message(FATAL_ERROR
    "[island-parity] 产物副本不存在：${DST}\n"
    "  说明 POST_BUILD 的 copy_directory 没跑到（或目标路径写错）。")
endif()

file(GLOB_RECURSE _src_files RELATIVE "${SRC}" "${SRC}/*")
file(GLOB_RECURSE _dst_files RELATIVE "${DST}" "${DST}/*")
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

# 反向：产物里不该有真源没有的件（本地残留会静默覆盖运行期行为）
foreach(_f ${_dst_files})
  if(NOT EXISTS "${SRC}/${_f}")
    list(APPEND _bad "${_f} —— 产物多出（真源无此件，疑为本地残留）")
  endif()
endforeach()

list(LENGTH _src_files _n)
if(_bad)
  string(REPLACE ";" "\n    " _badtxt "${_bad}")
  message(FATAL_ERROR
    "[island-parity] 共享灵动岛副本与真源不一致：\n    ${_badtxt}\n"
    "  真源：${SRC}\n"
    "  产物：${DST}\n"
    "  ⚠️ 不要手改产物副本 —— 要改就改 Stelarith-shared-qml/island/ 再重新构建。")
endif()

message(STATUS "[island-parity] OK —— ${_n} 件与真源逐一一致（${DST}）")
