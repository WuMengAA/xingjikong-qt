// 星集控 · 天气快照推送 CLI（第十六批 ④ · 2026-11-11）
//
// 为什么有这个文件：第十一批 WeatherPush.h 头注释声称有「① WS 指令 weather_push」，
// 但 §7.1 复核发现全仓只有那一句注释 —— 接收端分支、能力上报、生产者三样全缺。
// 本批补齐：execute_action.cpp 的 weather_push 分支 + action_caps.cpp 名册 +
// 这个 CLI。它是「真实推送源」：运维 / 定时任务把一份合规快照经既有
// POST /api/instructions 通用指令通路推给指定教室机，云端零改动（index.js 无 action 白名单）。
//
// 刻意不内置天气抓取器：官方《天气》未载刷新周期 / 开发者端点 / 鉴权（第十批如实记账），
// 不编造。快照由调用方给（气象台导出 / 插件 / 手工），CLI 只负责送达与回执。
//
// 用法：
//   node push-weather.mjs --uid class_xlzx_2028_08 --weather 晴 --alerts 大风蓝色,暴雨橙色
//        [--rain-minutes 30] [--rain-phase start|end] [--source ops.push-weather]
//        [--base-url https://control.245959623.xyz] [--token <CLOUD_VIEWER_TOKEN>]
//        [--dry-run]       只打印将发出的请求，不联网
//        [--self-test]     内置纯断言，不联网（探针实跑用这个）
//   token 缺省读环境变量 CLOUD_VIEWER_TOKEN。
//
// 契约（与被控端 WeatherPush::push 逐字对齐）：
//   params = { weather:string, alerts:string[], rainMinutes?:int>=0,
//              rainPhase?:'start'|'end', source:string }
//   回执 = executeAction 的 out.data = WeatherPush::receiptOf(PushResult)
//        = { ok, changed, revision, source, detail }
//   去重、坏字段拒收、store 一字不动、revision 单调递增全部由被控端单一真源裁决。
//
// 行业镜像（不编造，HTTP 200 实读）：
//   RFC 8030  Web Push：「a standardized method of event delivery」—— 生产者与订阅者解耦；
//   RFC 9110 §9.2.2  Idempotent Methods —— 同一份快照重复推不得产生额外副作用，
//                    本链靠 WeatherPush::sameSnapshot 去重落地这条；
//   OASIS CAP 1.2   告警文档是「一份结构化快照」而非轮询出来的派生量。
//   （CAP 1.2 页面本次 fetch failed，如实记账，不引用其未核实条文。）
//
// 零第三方依赖：只 node:http / node:https / node:assert / node:url。
import http from "node:http";
import https from "node:https";
import assert from "node:assert/strict";
import { URL, pathToFileURL } from "node:url";

const kAction = "weather_push";
const kDefaultSource = "ops.push-weather";
const kDefaultBase = "https://control.245959623.xyz";

function parseArgs(argv) {
  const o = {};
  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i];
    if (!a.startsWith("--")) continue;
    const key = a.slice(2);
    if (key === "self-test" || key === "dry-run") { o[key] = true; continue; }
    const v = argv[++i];
    if (v === undefined) throw new Error("缺参数值: --" + key);
    o[key] = v;
  }
  return o;
}

function splitAlerts(s) {
  if (!s) return [];
  return String(s).split(",").map(x => x.trim()).filter(Boolean);
}

/** 纯函数：构造 /api/instructions 的 body。探针可直接断言它的形状。 */
export function buildPayload(opts) {
  const uid = String(opts.uid || "").trim();
  const weather = String(opts.weather || "").trim();
  if (!uid) throw new Error("缺 --uid");
  if (!weather) throw new Error("缺 --weather");
  const params = { weather: weather, alerts: splitAlerts(opts.alerts) };
  if (opts["rain-minutes"] !== undefined) {
    const n = Number(opts["rain-minutes"]);
    if (!Number.isInteger(n) || n < 0) throw new Error("--rain-minutes 必须是非负整数");
    params.rainMinutes = n;
  }
  if (opts["rain-phase"] !== undefined) {
    const p = String(opts["rain-phase"]);
    if (p !== "start" && p !== "end") throw new Error("--rain-phase 只能是 start 或 end");
    params.rainPhase = p;
  }
  params.source = String(opts.source || kDefaultSource).trim() || kDefaultSource;
  return { uid: uid, action: kAction, params: params };
}

/** 纯函数：把 payload 变成一次可发出的 HTTP 请求描述（不联网）。 */
export function buildRequest(opts) {
  const payload = buildPayload(opts);
  const base = String(opts["base-url"] || kDefaultBase).replace(/\/+$/, "");
  const token = String(opts.token || process.env.CLOUD_VIEWER_TOKEN || "").trim();
  const url = new URL(base + "/api/instructions");
  const body = JSON.stringify(payload);
  const headers = {
    "Content-Type": "application/json",
    "Content-Length": Buffer.byteLength(body),
    "Authorization": "Bearer " + token
  };
  return { payload: payload, url: url, body: body, headers: headers,
           mod: url.protocol === "https:" ? https : http };
}

function selfTest() {
  let n = 0;
  const t = (cond, msg) => { assert.ok(cond, msg); ++n; };
  const p = buildPayload({ uid: "  class_xlzx_2028_08  ", weather: "  晴  ",
                           alerts: " 大风蓝色 , , 暴雨橙色 " });
  t(p.action === "weather_push", "T1 action 常量逐字 weather_push");
  t(p.uid === "class_xlzx_2028_08", "T2 uid 原样 trim");
  t(p.params.weather === "晴", "T3 weather trim");
  t(JSON.stringify(p.params.alerts) === JSON.stringify(["大风蓝色", "暴雨橙色"]),
    "T4 alerts 逗号切分 + trim + 去空");
  t(p.params.source === kDefaultSource, "T5 source 缺省值");
  let threw = false; try { buildPayload({ weather: "晴" }); } catch (e) { threw = true; }
  t(threw, "T6 缺 uid 拒发");
  threw = false; try { buildPayload({ uid: "u" }); } catch (e) { threw = true; }
  t(threw, "T7 缺 weather 拒发");
  threw = false; try { buildPayload({ uid: "u", weather: "晴", "rain-minutes": "1.5" }); } catch (e) { threw = true; }
  t(threw, "T8 rainMinutes 非整数拒发");
  threw = false; try { buildPayload({ uid: "u", weather: "晴", "rain-phase": "mid" }); } catch (e) { threw = true; }
  t(threw, "T9 rainPhase 非法拒发");
  const p2 = buildPayload({ uid: "u", weather: "雨", "rain-minutes": "30", "rain-phase": "end",
                            source: "plugin.weather" });
  t(p2.params.rainMinutes === 30 && p2.params.rainPhase === "end",
    "T10 合法枚举与整数透传");
  t(p2.params.source === "plugin.weather", "T11 自定义 source");
  const req = buildRequest({ uid: "u", weather: "晴", "base-url": "http://127.0.0.1:8788", token: "tok123" });
  t(req.url.pathname === "/api/instructions", "T12 端点逐字 /api/instructions");
  t(req.headers.Authorization === "Bearer tok123", "T13 Bearer 鉴权头");
  t(req.mod === http, "T14 http 走 http 模块");
  t(JSON.stringify(JSON.parse(req.body)) === JSON.stringify(req.payload), "T15 body 可回读");
  t(buildRequest({ uid: "u", weather: "晴", "base-url": kDefaultBase }).mod === https,
    "T16 默认公网走 https 模块");
  console.log("SELFTEST PASS " + n + " / FAIL 0");
}

function post(req) {
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
  const req = buildRequest(o);
  if (o["dry-run"]) {
    console.log(JSON.stringify({ url: req.url.toString(), method: "POST",
      headers: { Authorization: "Bearer ***", "Content-Type": "application/json" },
      body: req.payload }, null, 2));
    return 0;
  }
  const res = await post(req);
  console.log("HTTP " + res.status + " " + res.body);
  return (res.status >= 200 && res.status < 300) ? 0 : 1;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().then((c) => process.exit(c)).catch((e) => {
    console.error("push-weather: " + e.message);
    process.exit(2);
  });
}
