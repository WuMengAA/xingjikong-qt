// 校区注册 + 教师网关纯函数单测（2026-11-16）
import assert from 'node:assert/strict';
import { XIANLIN_CAMPUS, campusServers, findCampusServer, chooseCampusServer, xianlinClassPlan, normalizeServerBase } from '../src/campus-registry.js';
import { teacherScanPlan, teacherGatewaySummary, isTeacherActionAllowed, isAdminOrBatch } from '../src/teacher-gateway.js';
let pass=0, fail=0; function ok(n,c,e=''){ if(c){pass++;console.log('PASS '+n);} else {fail++;console.error('FAIL '+n+' '+e);} }
ok('校区名', XIANLIN_CAMPUS.name === '渭南市华州区咸林中学校区');
ok('12 班', XIANLIN_CAMPUS.classroomCount === 12 && xianlinClassPlan().length === 12);
ok('服务器清单只 1 个校区', campusServers().length === 1);
const s = findCampusServer('xianlin');
ok('校区服务器带 WS 地址', !!s && s.agentWsUrl.endsWith('/ws/agent') && s.viewerWsUrl.endsWith('/ws/viewer'));
ok('对外地址不接受路径', normalizeServerBase('https://x/a') === '');
ok('选择校区只回服务端地址', chooseCampusServer({campusId:'xianlin',classCode:'cls-abcdef',className:'1班'}).envelope.publicBase === s.publicBase);
ok('坏班级码拒绝', chooseCampusServer({campusId:'xianlin',classCode:'x'}).ok === false);
ok('未知校区拒绝', chooseCampusServer({campusId:'nowhere',classCode:'cls-abcdef'}).ok === false);
const p = teacherScanPlan({ticketCode:'K7QM3X',uid:'dev-01'});
ok('教师扫码只走 class-scan', p.ok && p.channel === 'class-scan' && p.requireManage === false);
ok('教师网关不建班不批量', p.allowCreateClass === false && p.allowBatch === false);
ok('坏码拒绝', !teacherScanPlan({ticketCode:'BAD',uid:'dev-01'}).ok);
ok('超管/批量被识别', isAdminOrBatch('platform-admin') && isAdminOrBatch('batch-bind') && isAdminOrBatch('create-class'));
ok('白名单只放老师动作', isTeacherActionAllowed('class-scan-bind') && !isTeacherActionAllowed('create-class'));
ok('网关摘要无超管/批量', teacherGatewaySummary().superAdmin === false && teacherGatewaySummary().batch === false);
console.log('PASS '+pass+' / FAIL '+fail); if(fail) process.exit(1);
