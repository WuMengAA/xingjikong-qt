// 星集控 · USGS 地震速报适配器（USGS GeoJSON Summary → earthquake_push）
// 第十九批 / 2026-11-11。为什么有这个文件：push-earthquake.mjs 只负责「送达」，
// 真实数据源这一环由运维/定时任务驱动。本文件把 USGS 公开 feed 变成合规快照，
// 不引入密钥、不改任何被控端/云端生产代码。
//
// ★ 一手依据（HTTP 200 实读，不编造）：
//   https://earthquake.usgs.gov/earthquakes/feed/v1.0/geojson.php
//     「GeoJSON Summary Format」properties 含 mag/place/time/updated/alert/status/tsunami/
//     type/id，geometry.coordinates=[lon,lat,depthKm]，Feeds 含 Past Hour/Day/7Days/30Days，
//     「Updated every minute」。
//   https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_hour.geojson
//     200，metadata{count:7,api:2.8.2}，features[].properties.alert 多为 null。
//   https://earthquake.usgs.gov/earthquakes/feed/policy.php → 404（不引用未核实条款）。
//
// ★ 诚实边界（写进指挥台，不藏）：
//   ① USGS feed 是【事后地震目录/速报】，properties.alert 是 PAGER 灾后评估，
//      不是秒级【到达前预警】。中国地震预警网（ICL）无公开免费开发者端点。
//      本适配器只做速报注入框架，不冒充秒级 EEW。
//   ② alert 为 null 时**不臆造**成绿色/黄色：直接不下发该字段，由被控端保持缺省。
//   ③ mag 为 null 的 feature 直接跳过（不兜底成 0 级）。
//   ④ 免费 feed 有更新频率与调用礼貌约束，调用方自行控制节奏（本文件不内置常驻抓取器）。
//
// 链路（三件都是既有单一真源，本文件只做「地震台 JSON → 合规快照」这一段纯变换）：
//   USGS all_hour.geojson ──(parseUsgs/pickStrongest)──▶ Snapshot
//     ──(push-earthquake.mjs buildPayload/buildRequest)──▶ POST /api/instructions
//     ──(云端通用透传，index.js 零改动)──▶ 被控端 executeAction(earthquake_push)
//     ──(EarthquakePush::push 去重/校验/store + 通知/规则变量/事件联动)──▶ 灵动岛
//
// 用法：
//   node earthquake-usgs.mjs --self-test
//   node earthquake-usgs.mjs --dry-run --uid class_xlzx_2028_08
//   node earthquake-usgs.mjs --fetch-and-push --uid class_xlzx_2028_08 [--min-mag 4.5]
//        [--feed-url <url>] [--token <CLOUD_VIEWER_TOKEN>] [--base-url <url>]
//
// 零第三方依赖：node:https / node:assert / node:url + 同目录 push-earthquake.mjs。
import https from "node:https";
import assert from "node:assert/strict";
import { URL, pathToFileURL } from "node:url";
import { buildPayload, buildRequest } from "./push-earthquake.mjs";

export const kSource = "usgs.geojson";
export const kDefaultFeed = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_hour.geojson";
export const kDisclaimer = "USGS 事后速报 / PAGER 灾后评估，非秒级到达前预警；ICL 无公开免费端点。";

/** 纯函数：一条 GeoJSON feature → 合规快照；坏条目返回 null（不兜底、不臆造）。 */
export function summarizeFeature(f) {
  if (!f || typeof f !== "object") return null;
  const p = f.properties;
  if (!p || typeof p !== "object") return null;
  const id = String(p.id || f.id || "").trim();
  if (!id) return null;
  // ★ Number(null) === 0，会把 USGS 的「mag 为 null」静默兜底成 0 级地震。
  //   诚实边界③要求跳过不兜底，故先显式拒 null/undefined（T8 专防此漂移）。
  if (p.mag === null || p.mag === undefined) return null;
  const mag = Number(p.mag);
  if (!Number.isFinite(mag) || mag < 0 || mag > 12) return null;
  const place = String(p.place || "").trim();
  if (!place) return null;
  const timeMs = Number(p.time);
  if (!Number.isFinite(timeMs) || timeMs <= 0) return null;
  const s = { id: id, mag: mag, place: place, timeMs: timeMs, source: kSource };
  const updatedMs = Number(p.updated);
  if (Number.isFinite(updatedMs) && updatedMs > 0) s.updatedMs = updatedMs;
  const alert = String(p.alert || "").trim().toLowerCase();
  if (["green", "yellow", "orange", "red"].indexOf(alert) >= 0) s.alert = alert;
  const status = String(p.status || "").trim().toLowerCase();
  if (["automatic", "reviewed", "manual", "deleted"].indexOf(status) >= 0) s.status = status;
  const g = f.geometry;
  if (g && Array.isArray(g.coordinates) && g.coordinates.length >= 3) {
    const d = Number(g.coordinates[2]);
    if (Number.isFinite(d) && d >= -1 && d <= 1000) s.depthKm = d;
  }
  if (p.tsunami === 1 || p.tsunami === true) s.tsunami = true;
  const url = String(p.url || "").trim();
  if (url) s.url = url;
  return s;
}

/** 纯函数：从整份 feed 里挑一条「震级最大」的合规速报；全不合格返回 null。 */
export function pickStrongest(doc, minMag = 0) {
  if (!doc || !Array.isArray(doc.features)) return null;
  let best = null;
  for (const f of doc.features) {
    const s = summarizeFeature(f);
    if (!s || s.mag < minMag) continue;
    if (!best || s.mag > best.mag || (s.mag === best.mag && s.timeMs > best.timeMs)) best = s;
  }
  return best;
}

/** 纯函数：快照 → push-earthquake.mjs 的 opts 形状（键名逐字对齐 CLI）。 */
export function toPushOpts(uid, s) {
  if (!s) throw new Error("没有可推送的速报");
  const o = { uid: uid, id: s.id, mag: s.mag, place: s.place,
              "time-ms": s.timeMs, source: s.source };
  if (s.updatedMs !== undefined) o["updated-ms"] = s.updatedMs;
  if (s.alert !== undefined) o.alert = s.alert;
  if (s.status !== undefined) o.status = s.status;
  if (s.depthKm !== undefined) o["depth-km"] = s.depthKm;
  if (s.tsunami) o.tsunami = true;
  if (s.url !== undefined) o.url = s.url;
  return o;
}

/** 纯函数：构造 feed URL（可覆盖，便于隔离测试与切换 4.5+/significant feed）。 */
export function buildFeedUrl(opts) {
  return String(opts["feed-url"] || kDefaultFeed);
}

const kSample = {
  metadata: { count: 2, api: "2.8.2" },
  features: [
    { type: "Feature", id: "aka2026wizarx",
      properties: { mag: 2.1, place: "10 km NE of Somewhere", time: 1730000000000,
                    updated: 1730000060000, alert: null, status: "automatic",
                    tsunami: 0, url: "https://earthquake.usgs.gov/earthquakes/eventpage/aka2026wizarx" },
      geometry: { type: "Point", coordinates: [-150.1, 61.2, 12.5] } },
    { type: "Feature", id: "us7000abcd",
      properties: { mag: 5.4, place: "78 km S of Another Place", time: 1730000300000,
                    updated: 1730000360000, alert: "orange", status: "reviewed",
                    tsunami: 1, url: "https://earthquake.usgs.gov/earthquakes/eventpage/us7000abcd" },
      geometry: { type: "Point", coordinates: [-72.3, -35.1, 33.0] } }
  ]
};

function selfTest() {
  let n = 0;
  const t = (cond, msg) => { assert.ok(cond, msg); ++n; };
  const p = summarizeFeature(kSample.features[1]);
  t(p.id === "us7000abcd", "T1 id 透传");
  t(p.mag === 5.4 && p.place.indexOf("Another") >= 0, "T2 mag/place 透传");
  t(p.timeMs === 1730000300000 && p.updatedMs === 1730000360000, "T3 time/updated 毫秒透传");
  t(p.alert === "orange" && p.status === "reviewed", "T4 alert/status 枚举透传");
  t(p.depthKm === 33.0 && p.tsunami === true, "T5 depthKm/tsunami 透传");
  t(p.url.indexOf("us7000abcd") > 0 && p.source === kSource, "T6 url/source 透传");
  const p0 = summarizeFeature(kSample.features[0]);
  t(p0.alert === undefined, "T7 alert=null 不臆造成枚举");
  t(summarizeFeature({ properties: { mag: null, place: "x", time: 1, id: "a" } }) === null,
    "T8 mag=null 跳过不兜底");
  t(summarizeFeature({ properties: { mag: 1, place: "", time: 1, id: "a" } }) === null,
    "T9 place 空跳过");
  t(summarizeFeature({ properties: { mag: 1, place: "x", time: 1 } }) === null,
    "T10 缺 id 跳过");
  const best = pickStrongest(kSample);
  t(best && best.id === "us7000abcd", "T11 挑震级最大的一条");
  t(pickStrongest(kSample, 6) === null, "T12 minMag 门槛生效");
  t(pickStrongest({}) === null && pickStrongest(null) === null, "T13 空 feed 返回 null");
  const opts = toPushOpts("class_xlzx_2028_08", best);
  t(opts.uid === "class_xlzx_2028_08" && opts.id === "us7000abcd", "T14 toPushOpts uid/id");
  t(opts["time-ms"] === 1730000300000 && opts["depth-km"] === 33.0, "T15 toPushOpts 键名逐字对齐 CLI");
  const payload = buildPayload(opts);
  t(payload.action === "earthquake_push", "T16 复用 CLI 单一真源得到 earthquake_push");
  t(payload.params.alert === "orange" && payload.params.tsunami === true, "T17 载荷字段完整");
  const req = buildRequest(Object.assign({}, opts, { "base-url": "http://127.0.0.1:8788", token: "t" }));
  t(req.url.pathname === "/api/instructions", "T18 端点逐字 /api/instructions");
  t(buildFeedUrl({}) === kDefaultFeed, "T19 默认 feed 是 all_hour.geojson");
  t(buildFeedUrl({ "feed-url": "https://x/y.geojson" }) === "https://x/y.geojson", "T20 feed 可覆盖");
  t(kDisclaimer.indexOf("非秒级到达前预警") > 0, "T21 诚实边界逐字在案");
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
  const o = {};
  const argv = process.argv.slice(2);
  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i];
    if (!a.startsWith("--")) continue;
    const k = a.slice(2);
    if (k === "self-test" || k === "dry-run" || k === "fetch-and-push") { o[k] = true; continue; }
    o[k] = argv[++i];
  }
  if (o["self-test"]) { selfTest(); return 0; }
  if (o["dry-run"]) {
    const best = pickStrongest(kSample, Number(o["min-mag"] || 0));
    console.log(JSON.stringify({ disclaimer: kDisclaimer, feed: buildFeedUrl(o),
      strongest: best, push: buildPayload(toPushOpts(o.uid || "class_xlzx_2028_08", best)) }, null, 2));
    return 0;
  }
  if (o["fetch-and-push"]) {
    const got = await httpsGet(buildFeedUrl(o));
    if (got.status !== 200) throw new Error("USGS HTTP " + got.status);
    const best = pickStrongest(JSON.parse(got.body), Number(o["min-mag"] || 0));
    if (!best) { console.log(kDisclaimer); console.log("NO_QUAKE_ABOVE_THRESHOLD"); return 0; }
    const po = Object.assign({}, toPushOpts(o.uid, best), {
      "base-url": String(o["base-url"] || "https://control.245959623.xyz"),
      token: String(o.token || process.env.CLOUD_VIEWER_TOKEN || "")
    });
    const res = await httpsPost(buildRequest(po));
    console.log(kDisclaimer);
    console.log("snapshot=" + JSON.stringify(best));
    console.log("push HTTP " + res.status + " " + res.body);
    return (res.status >= 200 && res.status < 300) ? 0 : 1;
  }
  console.log("用法：--self-test | --dry-run | --fetch-and-push --uid <uid> [--min-mag 4.5]");
  return 0;
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().then((c) => process.exit(c)).catch((e) => { console.error("earthquake-usgs: " + e.message); process.exit(2); });
}
