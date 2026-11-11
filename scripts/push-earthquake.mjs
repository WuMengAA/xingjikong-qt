// 星集控 · 地震速报快照推送 CLI（第十九批 · 2026-11-11）
// 与 push-weather.mjs 完全对称：把一份合规的地震速报快照经既有
// POST /api/instructions 通用指令通路推给指定教室机，云端零改动。
// 诚实边界：USGS feed 是事后速报不是秒级到达前预警，本 CLI 只负责送达与回执，
//   不内置抓取器（抓取与格式变换在 earthquake-usgs.mjs，由运维/定时任务驱动）。
//
// 用法：
//   node push-earthquake.mjs --uid class_xlzx_2028_08 --id aka2026wizarx --mag 5.2
//        --place "阿拉斯加" --time-ms 1730000000000 [--updated-ms N] [--alert orange]
//        [--status automatic] [--depth-km 10] [--tsunami] [--url U]
//        [--source ops.push-earthquake] [--base-url ...] [--token ...]
//        [--dry-run] [--self-test]
//
// 契约（与被控端 EarthquakePush::push 逐字对齐）：
//   params = { id, mag, place, timeMs, updatedMs?, alert?, status?, depthKm?, tsunami?, url?, source }
//   回执 = executeAction 的 out.data = EarthquakePush::receiptOf(PushResult)
//        = { ok, changed, revision, id, source, detail, notify?, event? }
//   去重、坏字段拒收、store 一字不动、revision 单调递增全部由被控端单一真源裁决。
//
// 行业镜像（HTTP 200 实读，不编造）：
//   USGS GeoJSON Summary Format（feed/v1.0/geojson.php）「Updated every minute」；
//   RFC 9110 §9.2.2 Idempotent Methods —— 同一 id 重复推不得重复弹窗（sameSnapshot 去重）；
//   OASIS CAP 1.2 告警文档是一份结构化快照而非轮询派生量。
//   不是到达前预警：PAGER alert 是灾后评估；ICL 无公开免费端点，不冒充。
//
// 零第三方依赖：node:http / node:https / node:assert / node:url。
import http from "node:http";
import https from "node:https";
import assert from "node:assert/strict";
import { URL, pathToFileURL } from "node:url";

const kAction = "earthquake_push";
const kDefaultSource = "ops.push-earthquake";
const kDefaultBase = "https://control.245959623.xyz";

function parseArgs(argv) {
  const o = {};
  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i];
    if (!a.startsWith("--")) continue;
    const key = a.slice(2);
    if (key === "self-test" || key === "dry-run") { o[key] = true; continue; }
    if (key === "tsunami") { o.tsunami = true; continue; }
    const v = argv[++i];
    if (v === undefined) throw new Error("缺参数值: --" + key);
    o[key] = v;
  }
  return o;
}

export function buildPayload(opts) {
  const uid = String(opts.uid || "").trim();
  const id = String(opts.id || "").trim();
  const place = String(opts.place || "").trim();
  if (!uid) throw new Error("缺 --uid");
  if (!id) throw new Error("缺 --id");
  if (!place) throw new Error("缺 --place");
  const mag = Number(opts.mag);
  if (!Number.isFinite(mag) || mag < 0 || mag > 12) throw new Error("--mag 必须是 0..12 数字");
  const timeMs = Number(opts["time-ms"]);
  if (!Number.isFinite(timeMs) || timeMs <= 0) throw new Error("--time-ms 必须是正 epoch 毫秒");
  const params = { id: id, mag: mag, place: place, timeMs: timeMs };
  if (opts["updated-ms"] !== undefined) {
    const n = Number(opts["updated-ms"]);
    if (!Number.isFinite(n)) throw new Error("--updated-ms 非法");
    params.updatedMs = n;
  }
  if (opts.alert !== undefined) {
    const a = String(opts.alert).trim().toLowerCase();
    if (["green", "yellow", "orange", "red"].indexOf(a) < 0) throw new Error("--alert 非法枚举");
    params.alert = a;
  }
  if (opts.status !== undefined) {
    const s = String(opts.status).trim().toLowerCase();
    if (["automatic", "reviewed", "manual", "deleted"].indexOf(s) < 0) throw new Error("--status 非法枚举");
    params.status = s;
  }
  if (opts["depth-km"] !== undefined) {
    const d = Number(opts["depth-km"]);
    if (!Number.isFinite(d)) throw new Error("--depth-km 非法");
    params.depthKm = d;
  }
  if (opts.tsunami !== undefined) params.tsunami = true;
  if (opts.url !== undefined) params.url = String(opts.url).trim();
  params.source = String(opts.source || kDefaultSource).trim() || kDefaultSource;
  return { uid: uid, action: kAction, params: params };
}

export function buildRequest(opts) {
  const payload = buildPayload(opts);
  let base = String(opts["base-url"] || kDefaultBase);
  while (base.length > 0 && base.endsWith("/")) base = base.slice(0, -1);
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
  const base = { uid: "  class_xlzx_2028_08  ", id: "aka2026wizarx", mag: "5.2",
                 place: "  阿拉斯加  ", "time-ms": "1730000000000" };
  const p = buildPayload(base);
  t(p.action === "earthquake_push", "T1 action 常量逐字 earthquake_push");
  t(p.uid === "class_xlzx_2028_08", "T2 uid trim");
  t(p.params.id === "aka2026wizarx", "T3 id trim");
  t(p.params.place === "阿拉斯加", "T4 place trim");
  t(p.params.mag === 5.2, "T5 mag 转数值");
  t(p.params.timeMs === 1730000000000, "T6 timeMs 转数值");
  t(p.params.source === kDefaultSource, "T7 source 缺省");
  let threw = false; try { buildPayload({ id: "a", mag: 1, place: "p", "time-ms": 1 }); } catch (e) { threw = true; }
  t(threw, "T8 缺 uid 拒发");
  threw = false; try { buildPayload({ uid: "u", mag: 1, place: "p", "time-ms": 1 }); } catch (e) { threw = true; }
  t(threw, "T9 缺 id 拒发");
  threw = false; try { buildPayload({ uid: "u", id: "a", mag: 1, "time-ms": 1 }); } catch (e) { threw = true; }
  t(threw, "T10 缺 place 拒发");
  threw = false; try { buildPayload({ uid: "u", id: "a", mag: 99, place: "p", "time-ms": 1 }); } catch (e) { threw = true; }
  t(threw, "T11 mag 越界拒发");
  threw = false; try { buildPayload({ uid: "u", id: "a", mag: 1, place: "p", "time-ms": -5 }); } catch (e) { threw = true; }
  t(threw, "T12 timeMs 非正拒发");
  const p2 = buildPayload({ uid: "u", id: "a", mag: 3, place: "p", "time-ms": 100,
                            alert: "orange", status: "reviewed", "depth-km": "12.5",
                            tsunami: true, url: "https://e.usgs.gov/x", source: "usgs" });
  t(p2.params.alert === "orange" && p2.params.status === "reviewed", "T13 枚举透传");
  t(p2.params.depthKm === 12.5 && p2.params.tsunami === true, "T14 深度/海啸透传");
  t(p2.params.url === "https://e.usgs.gov/x" && p2.params.source === "usgs", "T15 url/source 透传");
  threw = false; try { buildPayload({ uid: "u", id: "a", mag: 1, place: "p", "time-ms": 1, alert: "blue" }); } catch (e) { threw = true; }
  t(threw, "T16 alert 非法枚举拒发");
  const req = buildRequest({ uid: "u", id: "a", mag: 1, place: "p", "time-ms": 1,
                             "base-url": "http://127.0.0.1:8788", token: "tok123" });
  t(req.url.pathname === "/api/instructions", "T17 端点逐字 /api/instructions");
  t(req.headers.Authorization === "Bearer tok123", "T18 Bearer 鉴权头");
  t(req.mod === http, "T19 http 走 http 模块");
  t(JSON.stringify(JSON.parse(req.body)) === JSON.stringify(req.payload), "T20 body 可回读");
  t(buildRequest({ uid: "u", id: "a", mag: 1, place: "p", "time-ms": 1 }).mod === https,
    "T21 默认公网走 https 模块");
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
    console.error("push-earthquake: " + e.message);
    process.exit(2);
  });
}
