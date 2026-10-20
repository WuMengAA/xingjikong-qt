# check-qml-modules-deployed.cmake
#
# 用途：构建后断言「源码里 import 的每个 Qt QML 模块」在部署目录里**真的有实现**。
#
# 为什么必须有（2026-10-20 出包现场实证）：
#   被控端此前没有 windeployqt 构建规则 ⇒ `build/qml/QtQuick/Controls` 是个**空目录**
#   （目录在、文件 0 个），exe 旁也没有 Qt6QuickControls2.dll。而共享 Md3Btn 从
#   2026-10-19 起 `import QtQuick.Controls`，被 5 个设置页 import ⇒ 装到机器上
#   面板一打开就是 `qtquickcontrols2plugin.dll 找不到指定的模块`。
#   症状与 2026-10-16 真机事故同源：**声明合法、编译零警告、qmllint 零 Error**，
#   只有运行时点开面板才炸。这类缺件必须在构建期拦死。
#
# 判据：模块路径（QtQuick.Controls → qml/QtQuick/Controls）下 GLOB 到 0 个文件即 FAIL。
#
# 用法（POST_BUILD 里由 CMakeLists 调用）：
#   cmake -DDEPLOY_QML=<exe目录>/qml -DREQUIRED_MODULES="QtQuick.Controls;QtQuick.Layouts" \
#         -P scripts/check-qml-modules-deployed.cmake

if(NOT DEFINED DEPLOY_QML OR "${DEPLOY_QML}" STREQUAL "")
  message(FATAL_ERROR "[qml-deploy] 缺参数：-DDEPLOY_QML=<部署的 qml 目录>")
endif()
if(NOT DEFINED REQUIRED_MODULES OR "${REQUIRED_MODULES}" STREQUAL "")
  message(STATUS "[qml-deploy] 无 Qt QML 模块依赖，跳过校验")
  return()
endif()

set(_missing "")
foreach(_mod IN LISTS REQUIRED_MODULES)
  string(STRIP "${_mod}" _mod)
  if("${_mod}" STREQUAL "")
    continue()
  endif()
  string(REPLACE "." "/" _rel "${_mod}")
  set(_dir "${DEPLOY_QML}/${_rel}")
  file(GLOB_RECURSE _impl "${_dir}/*")
  if(NOT _impl)
    list(APPEND _missing "${_mod}  →  ${_dir}（空目录或不存在）")
  endif()
endforeach()

if(_missing)
  string(REPLACE ";" "\n    " _txt "${_missing}")
  message(FATAL_ERROR
    "[qml-deploy] 以下 QML 模块在部署目录里没有实现，装到干净机器上会 'is not a type'：\n    ${_txt}\n"
    "  常见原因：① windeployqt 没跑到（缺规则/缺 tool）；② 没传 --qmldir 指向源码 QML 目录，"
    "windeployqt 扫不到该模块。\n"
    "  处置：删掉 build/qml 下对应目录后重新构建；或在 build/ 手工跑 "
    "windeployqt --qmldir <源码qml目录> <exe> 再重编。")
endif()

message(STATUS "[qml-deploy] QML 模块部署完整：${REQUIRED_MODULES}")
