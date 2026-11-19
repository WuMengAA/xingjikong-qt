// 校区选择 / 注册 HTTP 服务（2026-11-16 · 隔离运行；不碰现役 8788）。
// 只提供「列校区」「班级选择校区」两个只读/登记计划端点；不写生产库、不重启服务。
// 2026-11-19 增：新建校区 / 加入校区（纯内存状态，重启即复位；仅隔离端口演示与单测用）。
import http from "node:http";
import { URL } from "node:url";
import { campusServers, findCampusServer, chooseCampusServer, xianlinClassPlan } from "./campus-registry.js";
import { normalizeServerBase } from "./campus-registry.js";
import { teacherGatewaySummary, isTeacherActionAllowed, isAdminOrBatch } from "./teacher-gateway.js";
import { validateNewCampus, applyJoinCampus, campusesForUid, initialCampusState } from "./campus-onboarding.js";

const PORT = Number(process.env.CAMPUS_PORT || 18099);
const HOST = process.env.CAMPUS_HOST || "127.0.0.1";
// 独立内存状态：从既有硬编码咸林中学起步；不落盘、不碰生产库。
let onboarding = initialCampusState(campusServers()[0] ? {
  id: campusServers()[0].id, name: campusServers()[0].name,
  publicBase: campusServers()[0].publicBase, classCount: campusServers()[0].classroomCount || 1,
} : null);

function send(res, status, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(status, { "content-type": "application/json; charset=utf-8", "content-length": Buffer.byteLength(body) });
  res.end(body);
}
function readBody(req) {
  return new Promise((resolve) => {
    let raw = "";
    req.on("data", (c) => { raw += c; if (raw.length > 4096) req.destroy(); });
    req.on("end", () => { try { resolve(JSON.parse(raw || "{}")); } catch { resolve({}); } });
  });
}

const server = http.createServer(async (req, res) => {
  const u = new URL(req.url || "/", `http://${req.headers.host || HOST}`);
  if (req.method === "GET" && u.pathname === "/api/campus/servers") {
    return send(res, 200, { ok: true, servers: campusServers(), classes: xianlinClassPlan().map((x) => ({ index: x.index, name: x.name })) });
  }
  if (req.method === "GET" && u.pathname === "/api/campus/teacher-gateway") {
    return send(res, 200, { ok: true, gateway: teacherGatewaySummary() });
  }
  if (req.method === "POST" && u.pathname === "/api/campus/classes/choose") {
    const b = await readBody(req);
    const r = chooseCampusServer({ campusId: b.campusId, classCode: b.classCode, className: b.className });
    return send(res, r.ok ? 200 : 400, r);
  }
  if (req.method === "POST" && u.pathname === "/api/campus/teacher-gate") {
    const b = await readBody(req);
    const a = String(b.action || "");
    if (isAdminOrBatch(a) || !isTeacherActionAllowed(a)) {
      return send(res, 403, { ok: false, error: "教师网关只允许扫码绑定与班级读取，拒绝超管/批量/建班操作", action: a });
    }
    return send(res, 200, { ok: true, action: a, gateway: teacherGatewaySummary() });
  }
  // ── 2026-11-19 新建 / 加入校区（隔离内存态）─────────────────────────
  if (req.method === "GET" && u.pathname === "/api/campus/onboarding/state") {
    return send(res, 200, { ok: true, state: onboarding });
  }
  if (req.method === "POST" && u.pathname === "/api/campus/create") {
    const b = await readBody(req);
    const r = validateNewCampus(b, normalizeServerBase);
    if (!r.ok) return send(res, 400, r);
    if (onboarding.campuses.some((c) => c.id === r.campus.id))
      return send(res, 409, { ok: false, error: "校区 id 已存在" });
    onboarding = { ...onboarding, campuses: onboarding.campuses.concat(r.campus) };
    return send(res, 200, { ok: true, campus: r.campus });
  }
  if (req.method === "POST" && u.pathname === "/api/campus/join") {
    const b = await readBody(req);
    const r = applyJoinCampus(onboarding, b);
    if (!r.ok) return send(res, 400, r);
    onboarding = r.state;
    return send(res, 200, { ok: true, member: r.member, created: r.created });
  }
  if (req.method === "GET" && u.pathname === "/api/campus/mine") {
    return send(res, 200, { ok: true, campuses: campusesForUid(onboarding, u.searchParams.get("uid")) });
  }
  return send(res, 404, { ok: false, error: "not found" });
});

server.listen(PORT, HOST, () => {
  console.log(`[campus] ${HOST}:${PORT} · ${campusServers().length} 个校区 · ${xianlinClassPlan().length} 个班`);
});
