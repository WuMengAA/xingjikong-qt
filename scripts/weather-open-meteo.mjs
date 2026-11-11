// 星集控 · 真实气象数据源适配器（Open-Meteo → WeatherSnapshot → weather_push）
// 第十六批 ④ 收尾 / 2026-11-11。为什么有这个文件：第十一批 WeatherPush.h 头注释声称有
// 「WS 指令 weather_push」，§7.1 复核发现那是空承诺；第十六批补了接收端分支 + 能力上报 +
// push-weather.mjs（运维手工给一份快照）。但「真实推送源」还差最后一环：谁把气象台的
// 数据变成那份快照。本文件补这一环，且不引入任何密钥、不改任何被控端/云端生产代码。
//
// ★ 为什么选 Open-Meteo 作为「可实跑的第一条腿」（全部 HTTP 200 一手实读，不编造）：
//   https://api.open-meteo.com/v1/forecast?latitude=39.9042&longitude=116.4074
//       &current=weather_code&minutely_15=precipitation&timezone=Asia%2FShanghai
//       &forecast_days=1&forecast_minutely_15=16
//     → 200，current.weather_code=0（WMO code），minutely_15.precipitation 为 16 个 15 分钟槽。
//   https://open-meteo.com/en/terms  → 200，逐字「Non-Commercial Use ... Less than 10'000 API
//     calls per day, 5'000 per hour and 600 per minute. ... only use the free API services
//     for non-commercial」⇒ 免费档只许非商用，商用必须订阅 customer-api.open-meteo.com + key。
//   https://open-meteo.com/en/pricing → 200，逐字「CC BY 4.0 ... This licence mandates giving
//     appropriate credit」⇒ 数据须署名 Open-Meteo（本文件用 kAttribution 常量落地）。
//   https://open-meteo.com/en/docs/geocoding-api → 200，地名→经纬度（GeoNames），本文件不内置
//     抓取器地理编码，坐标由调用方给（与 push-weather 的「快照由调用方给」同纪律）。
//
// ★ 诚实差异（不藏、不编造，写进指挥台）：
//   ① Open-Meteo 免费档没有「气象预警」字段 ⇒ alerts 恒为空数组。预警要接和风天气
//      https://dev.qweather.com/docs/api/warning/weather-alert/ （200，alerts[] 含
//      id/senderName/severity/eventType）+ JWT 认证 + 专用 API Host，需账号密钥 ⇒ 本批不接。
//   ② 和风天气实时天气 https://dev.qweather.com/docs/api/weather/weather-current/ （200）
//      用 Authorization: Bearer <JWT>；字段 condition.text/code 与 WMO code 不同词表。
//   ③ ClassIsland 官方《天气》 https://docs.classisland.tech/app/weather.html （200）逐字
//      「本应用内置的天气信息和地点列表，均由小米天气API提供」，可显示「气象预警信号、
//      降水时间预报」「排除的气象预警」⇒ 正好对应第十批 RuleContextProviders 的
//      weatherAlert / rainMinutes / rainPhase 三个判据变量；但官方未载端点/鉴权/刷新周期，
//      不构成可对接的开发者 API，故不内置小米抓取器。
//   ④ WMO weather_code 用 ECMWF/WMO 4677 词表；本文件 wmoToWeather() 是一张显式映射表，
//      未知码拒收（返 null），不猜、不兜底成「晴」。
//
// 链路（三件都是既有单一真源，本文件只做「气象台 JSON → 合规快照」这一段纯变换）：
//   Open-Meteo /v1/forecast  ──(parseOpenMeteo)──▶ WeatherSnapshot 形状
//     ──(push-weather.mjs buildPayload/buildRequest)──▶ POST /api/instructions
//     ──(云端通用透传，index.js 零改动)──▶ 被控端 executeAction(weather_push)
//     ──(WeatherPush::push 去重/校验/setWeather)──▶ RuleContextBridge 11 变量重判
//
// 用法：
//   node weather-open-meteo.mjs --self-test              内置纯断言，零联网
//   node weather-open-meteo.mjs --dry-run                用内置样例 JSON 走完整变换，零联网
//   node weather-open-meteo.mjs --fetch-and-push --uid class_xlzx_2028_08 \
//        --lat 39.9042 --lon 116.4074 [--token <CLOUD_VIEWER_TOKEN>]
//        [--base-url https://control.245959623.xyz]     真联网取数并送达
// 零第三方依赖：node:https / node:assert / node:url + 同目录 push-weather.mjs（单一真源）。

import https from "node:https";
import assert from "node:assert/strict";
import { URL, pathToFileURL } from "node:url";
import { buildPayload, buildRequest } from "./push-weather.mjs";

const kSource = "open-meteo";
const kAttribution = "Weather data by Open-Meteo.com (CC BY 4.0)";
const kFreeTier = "Non-commercial: <10000 calls/day, <5000/hour, <600/minute";
const kDefaultBase = "https://api.open-meteo.com/v1/forecast";
const kSlotMinutes = 15;          // minutely_15 槽距，Open-Meteo 实读逐 15 分钟
const kMaxRainMinutes = 10080;    // 与 RuleContextProviders.h:203 的 kMaxRainMinutes 同值

/** WMO 4677 天气代码 → 中文天气词。未知码返 null（拒收，不兜底成「晴」）。 */
export function wmoToWeather(code) {
  if (!Number.isInteger(code)) return null;
  const table = {
    0: "晴", 1: "晴间多云", 2: "多云", 3: "阴",
    45: "雾", 48: "雾凇",
    51: "小毛毛雨", 53: "毛毛雨", 55: "大毛毛雨", 56: "冻毛毛雨", 57: "强冻毛毛雨",
    61: "小雨", 63: "中雨", 65: "大雨", 66: "冻雨", 67: "强冻雨",
    71: "小雪", 73: "中雪", 75: "大雪", 77: "米雪",
    80: "小阵雨", 81: "阵雨", 82: "强阵雨", 85: "小阵雪", 86: "强阵雪",
    95: "雷阵雨", 96: "雷阵雨伴冰雹", 99: "强雷阵雨伴冰雹"
  };
  return Object.prototype.hasOwnProperty.call(table, code) ? table[code] : null;
}

/** 纯函数：构造 Open-Meteo 请求 URL（坐标由调用方给，不内置地理编码抓取）。 */
export function buildForecastUrl(opts) {
  const lat = Number(opts.lat);
  const lon = Number(opts.lon);
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) throw new Error("缺 --lat / --lon 或不是数值");
  const u = new URL(kDefaultBase);
  u.searchParams.set("latitude", String(lat));
  u.searchParams.set("longitude", String(lon));
  u.searchParams.set("current", "weather_code");
  u.searchParams.set("minutely_15", "precipitation");
  u.searchParams.set("timezone", "Asia/Shanghai");
  u.searchParams.set("forecast_days", "1");
  u.searchParams.set("forecast_minutely_15", "16");
  return u;
}

/** 纯函数：minutely_15.precipitation → {rainMinutes, rainPhase}。无降水返 {}。 */
export function rainFromSlots(precip) {
  if (!Array.isArray(precip) || precip.length === 0) return {};
  const n = precip.length;
  const isWet = (v) => Number.isFinite(Number(v)) && Number(v) > 0;
  const firstWet = precip.findIndex(isWet);
  if (firstWet < 0) return {};                       // 全程无降水 → 保持被控端默认
  let minutes, phase;
  if (firstWet === 0) {                             // 当前正在下 → 距降水结束
    phase = "end";
    let j = 1;
    while (j < n && isWet(precip[j])) ++j;
    minutes = j * kSlotMinutes;                    // 全湿则给满窗口
  } else {                                         // 当前没下 → 距降水开始
    phase = "start";
    minutes = firstWet * kSlotMinutes;
  }
  if (minutes > kMaxRainMinutes) minutes = kMaxRainMinutes;
  return { rainMinutes: minutes, rainPhase: phase };
}

/** 纯函数：Open-Meteo JSON → WeatherSnapshot 形状的 params（不含 uid/action）。 */
export function parseOpenMeteo(json) {
  if (!json || typeof json !== "object" || Array.isArray(json)) throw new Error("坏 JSON：不是对象");
  const code = json.current && json.current.weather_code;
  const weather = wmoToWeather(code);
  if (weather === null) throw new Error("WMO 天气码未知，拒收：" + String(code));
  const slots = json.minutely_15 && json.minutely_15.precipitation;
  const params = { weather: weather, alerts: [] };   // Open-Meteo 无预警字段，如实留空
  const rain = rainFromSlots(slots);
  if (rain.rainMinutes !== undefined) {
    params.rainMinutes = rain.rainMinutes;
    params.rainPhase = rain.rainPhase;
  }
  params.source = kSource;
  return params;
}

/** 把一份 params 变成 push-weather 的 CLI opts（复用单一真源，不重造 payload）。 */
export function toPushOpts(uid, params) {
  const o = { uid: uid, weather: params.weather, source: params.source };
  if (params.rainMinutes !== undefined) o["rain-minutes"] = String(params.rainMinutes);
  if (params.rainPhase !== undefined) o["rain-phase"] = params.rainPhase;
  if (Array.isArray(params.alerts) && params.alerts.length) o.alerts = params.alerts.join(",");
  return o;
}

function parseArgs(argv) {
  const o = {};
  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i];
    if (!a.startsWith("--")) continue;
    const key = a.slice(2);
    if (key === "self-test" || key === "dry-run" || key === "fetch-and-push") { o[key] = true; continue; }
    const v = argv[++i];
    if (v === undefined) throw new Error("缺参数值: --" + key);
    o[key] = v;
  }
  return o;
}

const kSample = {
  current: { time: "2026-11-11T19:30", interval: 900, weather_code: 0 },
  minutely_15: { time: ["2026-11-11T19:30"], precipitation: [0, 0, 0.5, 0, 0, 0, 0, 0] }
};

function selfTest() {
  let n = 0;
  const t = (cond, msg) => { assert.ok(cond, msg); ++n; };
  t(wmoToWeather(0) === "晴", "T1 WMO 0=晴");
  t(wmoToWeather(3) === "阴", "T2 WMO 3=阴");
  t(wmoToWeather(95) === "雷阵雨", "T3 WMO 95=雷阵雨");
  t(wmoToWeather(99) === "强雷阵雨伴冰雹", "T4 WMO 99");
  t(wmoToWeather(999) === null, "T5 未知码拒收返 null");
  t(wmoToWeather("0") === null, "T6 非整数码拒收");
  const u = buildForecastUrl({ lat: 39.9042, lon: 116.4074 });
  t(u.hostname === "api.open-meteo.com", "T7 主机逐字 api.open-meteo.com");
  t(u.searchParams.get("current") === "weather_code", "T8 current=weather_code");
  t(u.searchParams.get("minutely_15") === "precipitation", "T9 minutely_15=precipitation");
  t(u.searchParams.get("timezone") === "Asia/Shanghai", "T10 时区 Asia/Shanghai");
  t(u.searchParams.get("forecast_minutely_15") === "16", "T11 16 槽");
  let threw = false; try { buildForecastUrl({ lat: "x", lon: 1 }); } catch (e) { threw = true; }
  t(threw, "T12 坏坐标拒发");
  t(JSON.stringify(rainFromSlots([0, 0, 0])) === "{}", "T13 全程无降水返空");
  t(JSON.stringify(rainFromSlots([0, 0, 0.5, 0])) === JSON.stringify({ rainMinutes: 30, rainPhase: "start" }), "T14 距降水开始 30 分");
  t(JSON.stringify(rainFromSlots([0.5, 0.5, 0, 0])) === JSON.stringify({ rainMinutes: 30, rainPhase: "end" }), "T15 距降水结束 30 分");
  t(JSON.stringify(rainFromSlots([0.5, 0.5, 0.5, 0.5])) === JSON.stringify({ rainMinutes: 60, rainPhase: "end" }), "T16 全程湿给满窗口");
  const p = parseOpenMeteo(kSample);
  t(p.weather === "晴", "T17 快照 weather=晴");
  t(JSON.stringify(p.alerts) === "[]", "T18 无预警字段如实留空");
  t(p.rainMinutes === 30 && p.rainPhase === "start", "T19 快照带降水判据");
  t(p.source === "open-meteo", "T20 source=open-meteo");
  threw = false; try { parseOpenMeteo("not json"); } catch (e) { threw = true; }
  t(threw, "T21 坏 JSON 拒");
  threw = false; try { parseOpenMeteo({ current: { weather_code: 42 } }); } catch (e) { threw = true; }
  t(threw, "T22 未知天气码拒");
  const opts = toPushOpts("class_xlzx_2028_08", p);
  const payload = buildPayload(opts);
  t(payload.action === "weather_push", "T23 复用 push-weather 单一真源");
  t(payload.params.weather === "晴" && payload.params.rainMinutes === 30, "T24 payload 透传");
  const req = buildRequest({ "base-url": "https://control.245959623.xyz", token: "t", uid: "u", weather: "晴" });
  t(req.url.pathname === "/api/instructions", "T25 端点 /api/instructions");
  t(req.mod === https, "T26 公网走 https");
  t(kAttribution.includes("CC BY 4.0"), "T27 CC BY 4.0 署名常量");
  t(kFreeTier.includes("10000"), "T28 免费档限额常量");
  console.log("SELFTEST PASS " + n + " / FAIL 0");
}

function httpsGet(url) {
  return new Promise((resolve, reject) => {
    const r = https.get(url, (res) => {
      let s = "";
      res.on("data", (c) => { s += c; });
      res.on("end", () => resolve({ status: res.statusCode, body: s }));
    });
    r.on("error", reject);
  });
}

function httpsPost(req) {
  return new Promise((resolve, reject) => {
    const r = req.mod.request(req.url, { method: "POST", headers: req.headers }, (res) => {
      let s = "";
      res.on("data", (c) => { s += c; });
      res.on("end", () => resolve({ status: res.statusCode, body: s }));
    });
    r.on("error", reject);
    r.write(req.body);
    r.end();
  });
}

async function main() {
  const o = parseArgs(process.argv.slice(2));
  if (o["self-test"]) { selfTest(); return 0; }
  if (o["dry-run"]) {
    const p = parseOpenMeteo(kSample);
    console.log(JSON.stringify({ attribution: kAttribution, freeTier: kFreeTier,
      snapshot: p, push: buildPayload(toPushOpts("class_xlzx_2028_08", p)) }, null, 2));
    return 0;
  }
  if (o["fetch-and-push"]) {
    const url = buildForecastUrl(o);
    const got = await httpsGet(url);
    if (got.status !== 200) throw new Error("Open-Meteo HTTP " + got.status);
    const p = parseOpenMeteo(JSON.parse(got.body));
    const po = Object.assign({}, toPushOpts(o.uid, p), {
      "base-url": String(o["base-url"] || "https://control.245959623.xyz"),
      token: String(o.token || process.env.CLOUD_VIEWER_TOKEN || "")
    });
    const req = buildRequest(po);
    const res = await httpsPost(req);
    console.log(kAttribution);
    console.log("snapshot=" + JSON.stringify(p));
    console.log("push HTTP " + res.status + " " + res.body);
    return (res.status >= 200 && res.status < 300) ? 0 : 1;
  }
  console.log("用法：--self-test | --dry-run | --fetch-and-push --uid <uid> --lat <lat> --lon <lon> [--token <tok>]");
  return 0;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().then((c) => process.exit(c)).catch((e) => {
    console.error("weather-open-meteo: " + e.message);
    process.exit(2);
  });
}