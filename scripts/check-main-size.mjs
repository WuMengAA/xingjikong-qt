#!/usr/bin/env node
// main.cpp 膨胀门禁（2026-10-30 · 用户禁令：main 拆分、禁止继续膨胀）
//
// 为什么要做成门禁而不是写进文档：
//   文档里的「禁止」不会失败，所以没人会遵守。这里的判据只有一个——
//   main.cpp 行数只要比台账多一行就 exit 1。想加业务就得先把业务放进
//   src/<domain>/ 里、再把 main 里那段删掉，行数自然不涨。
//
// 用法：
//   node scripts/check-main-size.mjs           # 校验（CI/手动）
//   node scripts/check-main-size.mjs --update  # 拆分后下调台账（只允许变小）
import fs from "node:fs";
import path from "node:path";
import url from "node:url";

const HERE = path.dirname(url.fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "..");
const BASELINE = path.join(HERE, "fixtures", "main-size-baseline.json");

const update = process.argv.includes("--update");

function countLines(p) {
  const txt = fs.readFileSync(p, "utf8");
  // 与 wc -l 同口径：按换行计，末尾无换行不算多一行
  let n = 0;
  for (let i = 0; i < txt.length; i++) if (txt[i] === "\n") n++;
  if (txt.length && !txt.endsWith("\n")) n++;
  return n;
}

const base = JSON.parse(fs.readFileSync(BASELINE, "utf8"));
let failed = false;
const rows = [];

for (const [rel, limit] of Object.entries(base.files)) {
  const abs = path.join(ROOT, rel);
  if (!fs.existsSync(abs)) {
    rows.push(`  SKIP  ${rel}（不存在）`);
    continue;
  }
  const now = countLines(abs);
  const delta = now - limit;
  if (delta > 0) {
    rows.push(`  FAIL  ${rel}  ${now} 行 > 台账 ${limit}（多 ${delta} 行）`);
    failed = true;
  } else {
    rows.push(`  PASS  ${rel}  ${now} 行 ≤ 台账 ${limit}`);
  }
  if (update && now < limit) base.files[rel] = now;
}

console.log("── main.cpp 膨胀门禁 ──");
rows.forEach((r) => console.log(r));

if (update && !failed) {
  fs.writeFileSync(BASELINE, JSON.stringify(base, null, 2) + "\n", "utf8");
  console.log("  台账已下调（只记录变小后的值）");
}

if (failed) {
  console.log("\n✗ main.cpp 又变大了。业务逻辑请放 src/<domain>/，main 只做装配。");
  console.log("  若确实需要（例如装配代码增加），先拆分等量代码再提交，不要上调台账。");
  process.exit(1);
}
console.log("\n✓ 通过：main.cpp 未膨胀。");
