# 天气真实数据源适配器（Open-Meteo）· 2026-11-11

## 这一环补的是什么

第十一批 `src/automation/WeatherPush.h` 头注释声称有「① WS 指令 weather_push」路径，但 §7.1 复核发现全仓只有那一句注释（接收端分支、能力上报、生产者三样全缺）。第十六批补齐了接收端 `execute_action.cpp` 分支、`action_caps.cpp` 名册、`receiptOf()` 回执形状，以及 `push-weather.mjs`（运维手工给一份合规快照经既有 `POST /api/instructions` 通用透传达）。

**还差最后一环**：谁把气象台的数据变成那份快照。本批补这一环：`scripts/weather-open-meteo.mjs` 把 Open-Meteo 的实况 + 15 分钟降水槽，纯变换成 `WeatherPush::push` 接受的 params 形状，再复用 `push-weather.mjs` 的 `buildPayload` / `buildRequest` 单一真源送达。被控端与云端生产代码**零改动**。

## 为什么选 Open-Meteo 作为可实跑的第一条腿

全部 HTTP 200 一手实读，不编造：

- `https://api.open-meteo.com/v1/forecast?latitude=39.9042&longitude=116.4074&current=weather_code&minutely_15=precipitation&timezone=Asia/Shanghai&forecast_days=1&forecast_minutely_15=16` → 200，`current.weather_code=0`（WMO 码），`minutely_15.precipitation` 为 16 个 15 分钟槽。
- `https://open-meteo.com/en/terms` → 200，逐字「Non-Commercial Use ... Less than 10'000 API calls per day, 5'000 per hour and 600 per minute ... only use the free API services for non-commercial」。免费档只许非商用；商用须订阅 `customer-api.open-meteo.com` + key。
- `https://open-meteo.com/en/pricing` → 200，逐字「CC BY 4.0 ... This licence mandates giving appropriate credit」。数据须署名，落地为 `kAttribution` 常量。
- `https://open-meteo.com/en/docs/geocoding-api` → 200，地名→经纬度（GeoNames）。本适配器不内置地理编码抓取，坐标由调用方给（与 push-weather「快照由调用方给」同纪律）。

## 诚实差异（不藏、不编造）

1. Open-Meteo 免费档**没有气象预警字段** ⇒ `alerts` 恒为空数组。预警要接和风天气 `https://dev.qweather.com/docs/api/warning/weather-alert/`（200，`alerts[]` 含 `id` / `senderName` / `severity` / `eventType`）+ JWT 认证 + 专用 API Host，需账号密钥 ⇒ 本批不接。
2. 和风天气实时天气 `https://dev.qweather.com/docs/api/weather/weather-current/`（200）用 `Authorization: Bearer <JWT>`；字段 `condition.text` / `code` 与 WMO 4677 是不同词表。
3. ClassIsland 官方《天气》`https://docs.classisland.tech/app/weather.html`（200）逐字「本应用内置的天气信息和地点列表，均由小米天气API提供」，可显示「气象预警信号、降水时间预报」并支持「排除的气象预警」—— 正好对应第十批 `RuleContextProviders.h` 的 `weatherAlert` / `rainMinutes` / `rainPhase` 三个判据变量；但官方未载开发者端点 / 鉴权 / 刷新周期，**不构成可对接的开发者 API**，故不内置小米抓取器。
4. WMO `weather_code` 用 ECMWF/WMO 4677 词表；`wmoToWeather()` 是一张显式映射表，**未知码返 null（拒收）**，不猜、不兜底成「晴」。

## 链路

```
Open-Meteo /v1/forecast  ──(parseOpenMeteo)──▶ WeatherSnapshot 形状
  ──(push-weather.mjs buildPayload/buildRequest)──▶ POST /api/instructions
  ──(云端通用透传 index.js 零改动)──▶ 被控端 executeAction(weather_push)
  ──(WeatherPush::push 去重/校验/setWeather)──▶ RuleContextBridge 11 变量重判
```

## 用法

```
node weather-open-meteo.mjs --self-test     内置 28 条纯断言，零联网
node weather-open-meteo.mjs --dry-run      内置样例走完整变换，零联网
node weather-open-meteo.mjs --fetch-and-push --uid class_xlzx_2028_08 \
     --lat 39.9042 --lon 116.4074 [--token <CLOUD_VIEWER_TOKEN>] \
     [--base-url https://control.245959623.xyz]     真联网取数并送达
```

零第三方依赖：`node:https` / `node:assert` / `node:url` + 同目录 `push-weather.mjs`（单一真源）。

## 判据映射（与第十批 `RuleContextProviders.h` 对齐）

| Open-Meteo 字段 | 适配器输出 | 被控端判据变量 |
|---|---|---|
| `current.weather_code` | `params.weather`（中文词） | `weather` |
| `minutely_15.precipitation` 首个 >0 槽 | `params.rainMinutes` + `params.rainPhase=start` | `rainMinutes` |
| `minutely_15.precipitation` 当前槽 >0 | `params.rainMinutes` + `params.rainPhase=end` | `rainMinutes` |
| 无降水槽 | 不写 `rainMinutes` / `rainPhase` | 保持被控端默认 |
| （免费档无此字段） | `params.alerts = []` | `weatherAlert` |

`kMaxRainMinutes = 10080` 与 `Stelarith-control-qt/src/island/rules/RuleContextProviders.h:203` 的 `kMaxRainMinutes` 同值；`validateWeather` 的 5 条上限仍由被控端单一真源裁决，适配器不重复判定。

## 验证证据（本批实跑）

- `--self-test`：`SELFTEST PASS 28 / FAIL 0`，`SELFTEST_EXIT=0`。
- `--dry-run`：样例 JSON 变换出 `{weather:晴, alerts:[], rainMinutes:30, rainPhase:start, source:open-meteo}`，`DRY_EXIT=0`。
- 反向验证（一次性脚本，改坏 4 处语义）：`nowmo` / `norain` / `nophase` / `nosource` **4/4 精确判红**，还原后 baseline 28/0 复绿，`REVERSE_OK=True`。证明 28 条断言不是恒真假绿。
- 隔离端到端（真 Open-Meteo 200 取数 → 真 POST → 127.0.0.1 随机端口假服务器）：`E2E PASS 10 / FAIL 0`，`ADAPTER_EXIT=0`，假服务器收到 `POST /api/instructions` + `Bearer` + 合规 `weather_push` body；全程 `token=literal-fake`，不读 `CLOUD_VIEWER_TOKEN`，不打生产域名。
- 14 门禁 **13 绿 1 红**：唯一红 `test-file-size` 3 项全在 `Stelarith-viewer-qt` 他线在飞 WIP（`viewerbackend.cpp` 2282→2405、`viewerbackend.h` 1098→1126、`ClassDeviceTile.qml` 210/200）；`island.cpp` 2820 已由第十六批 ③b 据实回写转「历史超限·未恶化」warn。本批 0 新增红。

## 未验证（如实记账）

- 未向生产云端 / 真教室机推送过一份真实天气快照（隔离端到端用的是本机假服务器）。
- 未对接气象预警源（需产品裁决数据源 + 账号密钥）。
- 免费档非商用条款尚未经法务确认；若产品转商用，须切 `customer-api.open-meteo.com` + key，并在 UI 落地 CC BY 4.0 署名。
- 未做刷新周期调度（适配器是一次调用一次推送；定时任务由运维侧决定）。