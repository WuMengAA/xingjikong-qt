// 校区选择 / 注册 HTTP 服务（2026-11-16 · 隔离运行；不碰现役 8788）。
// 只提供「列校区」「班级选择校区」两个只读/登记计划端点；不写生产库、不重启服务。
import http from 'node:http';
import { URL } from 'node:url';
import { campusServers, findCampusServer, chooseCampusServer, xianlinClassPlan } from './campus-registry.js';
import { teacherGatewaySummary, isTeacherActionAllowed, isAdminOrBatch } from './teacher-gateway.js';

const PORT = Number(process.env.CAMPUS_PORT || 18099);
const HOST = process.env.CAMPUS_HOST || '127.0.0.1';

function send(res, status, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(status, { 'content-type': 'application/json; charset=utf-8', 'content-length': Buffer.byteLength(body) });
  res.end(body);
}
function readBody(req) {
  return new Promise((resolve) => {
    let raw = '';
    req.on('data', (c) => { raw += c; if (raw.length > 4096) req.destroy(); });
    req.on('end', () => { try { resolve(JSON.parse(raw || '{}')); } catch { resolve({}); } });
  });
}

const server = http.createServer(async (req, res) => {
  const u = new URL(req.url || '/', `http://${req.headers.host || HOST}`);
  if (req.method === 'GET' && u.pathname === '/api/campus/servers') {
    return send(res, 200, { ok: true, servers: campusServers(), classes: xianlinClassPlan().map((x) => ({ index: x.index, name: x.name })) });
  }
  if (req.method === 'GET' && u.pathname === '/api/campus/teacher-gateway') {
    return send(res, 200, { ok: true, gateway: teacherGatewaySummary() });
  }
  if (req.method === 'POST' && u.pathname === '/api/campus/classes/choose') {
    const b = await readBody(req);
    const r = chooseCampusServer({ campusId: b.campusId, classCode: b.classCode, className: b.className });
    return send(res, r.ok ? 200 : 400, r);
  }
  if (req.method === 'POST' && u.pathname === '/api/campus/teacher-gate') {
    const b = await readBody(req);
    const a = String(b.action || '');
    if (isAdminOrBatch(a) || !isTeacherActionAllowed(a)) {
      return send(res, 403, { ok: false, error: '教师网关只允许扫码绑定与班级读取，拒绝超管/批量/建班操作', action: a });
    }
    return send(res, 200, { ok: true, action: a, gateway: teacherGatewaySummary() });
  }
  return send(res, 404, { ok: false, error: 'not found' });
});

server.listen(PORT, HOST, () => {
  console.log(`[campus] ${HOST}:${PORT} · ${campusServers().length} 个校区 · ${xianlinClassPlan().length} 个班`);
});
