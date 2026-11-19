// 校区新建 / 加入纯逻辑单测（2026-11-19）。
import assert from "node:assert/strict";
import { normalizeCampusId, normalizeMemberRole, normalizeMemberUid, validateNewCampus, applyJoinCampus, campusesForUid, initialCampusState, CAMPUS_MEMBER_SCHEMA } from "../src/campus-onboarding.js";
let pass=0, fail=0; function ok(n,c,e=""){ if(c){pass++;console.log("PASS "+n);} else {fail++;console.error("FAIL "+n+" "+e);} }
const norm = (v) => /^https?:\/\/[^\s/?#]+$/i.test(String(v||"").trim()) ? String(v).trim() : "";

// ── 规范化 ──
ok("校区 id 小写归一", normalizeCampusId("  XL-ZX  ") === "xl-zx");
ok("坏校区 id 拒绝", normalizeCampusId("a") === "" && normalizeCampusId("bad/id") === "");
ok("角色白名单", normalizeMemberRole("TEACHER") === "teacher" && normalizeMemberRole("root") === "");
ok("uid 长度边界", normalizeMemberUid("abc123") === "abc123" && normalizeMemberUid("a") === "");

// ── 新建校区 ──
const n1 = validateNewCampus({ id:"XL-ZX", name:"咸林中学", shortName:"咸林", publicBase:"https://x.example.cn", classCount:12 }, norm);
ok("新建规范化", n1.ok && n1.campus.id === "xl-zx" && n1.campus.publicBase === "https://x.example.cn");
ok("坏地址拒绝", !validateNewCampus({ id:"a1", name:"x", publicBase:"https://x/a", classCount:1 }, norm).ok);
ok("班级数夹紧拒绝", !validateNewCampus({ id:"a1", name:"x", publicBase:"https://x", classCount:0 }, norm).ok);
ok("空名拒绝", !validateNewCampus({ id:"a1", name:"  ", publicBase:"https://x", classCount:1 }, norm).ok);

// ── 加入校区（幂等）──
let st = initialCampusState({ id:"xl-zx", name:"咸林中学", publicBase:"https://x.example.cn", classCount:12 });
ok("初始 1 校区 0 成员", st.campuses.length === 1 && st.members.length === 0 && st.schema === CAMPUS_MEMBER_SCHEMA);
const n2 = validateNewCampus({ id:"XL-ZX", name:"咸林中学", publicBase:"https://x.example.cn", classCount:12 }, norm);
ok("纯逻辑层新建校区", n2.ok && n2.campus.id === "xl-zx" && n2.campus.shortName === "咸林中学");
const j1 = applyJoinCampus(st, { uid:"dev00001", campusId:"XL-ZX", role:"TEACHER", name:"1班讲台", joinedAt:"2026-11-19" });
ok("加入成功", j1.ok && j1.created && j1.member.role === "teacher");
ok("未知校区拒绝", !applyJoinCampus(st, { uid:"dev00001", campusId:"nope", role:"teacher" }).ok);
ok("未知角色拒绝（不默认 admin）", !applyJoinCampus(st, { uid:"dev00001", campusId:"xl-zx", role:"root" }).ok);
ok("坏 uid 拒绝", !applyJoinCampus(st, { uid:"x", campusId:"xl-zx", role:"teacher" }).ok);
const j2 = applyJoinCampus(j1.state, { uid:"dev00001", campusId:"xl-zx", role:"admin" });
ok("重复加入只更新角色", j2.ok && !j2.created && j2.state.members.length === 1 && j2.state.members[0].role === "admin");
const j3 = applyJoinCampus(j2.state, { uid:"dev00002", campusId:"xl-zx", role:"member" });
ok("不同 uid 是不同成员", j3.ok && j3.state.members.length === 2);
ok("成员可见校区", campusesForUid(j3.state,"dev00002").length === 1 && campusesForUid(j3.state,"nobody00").length === 0);
ok("原状态不被改写（纯函数）", st.members.length === 0);

console.log("PASS "+pass+" / FAIL "+fail); if(fail) process.exit(1);
