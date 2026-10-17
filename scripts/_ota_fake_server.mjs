#!/usr/bin/env node
/**
 * 假 OTA 清单服务（探针夹具，零依赖）—— 2026-10-17 为「周期性 OTA 复查」立
 *
 * 用途：验证端侧 `otaSelfCheck()` 这条新路径：
 *   ① 到点真的发了 `GET /api/public/ota?product=agent&channel=<渠道>&current=<本机版本>`；
 *   ② 拿到清单后走的是**同一条** applyUpdateInfo 判定（日志出现「发现新版本」）；
 *   ③ 不会因为"发现新版本"就自己下载（mandatory=false ⇒ 只提示，等用户点）。
 *
 * ⚠️ 为什么必须用假服务：真清单的版本号已经等于本机版本（"已是最新"），
 *    证明不了"能发现"；而把真清单改成更高版本会触发**真实 OTA**（会把生产 exe 换掉）。
 *    假服务的 URL 指向一个不存在的文件，即使误触发下载也只会失败、不会装任何东西。
 *
 * 用法：
 *   node scripts/_ota_fake_server.mjs            # 默认 :8799、假版本 9.9.9、非强制
 *   FAKE_VERSION=9.9.9 FAKE_MANDATORY=0 PORT=8799 node scripts/_ota_fake_server.mjs
 *   curl http://127.0.0.1:8799/__hits            # 看被查了几次、带的 channel/current
 */
import http from "node:http";

const PORT = Number(process.env.PORT || 8799);
const VERSION = process.env.FAKE_VERSION || "9.9.9";
const MANDATORY = process.env.FAKE_MANDATORY === "1";
const hits = [];

const server = http.createServer((req, res) => {
	const u = new URL(req.url, `http://127.0.0.1:${PORT}`);
	if (u.pathname === "/api/public/ota") {
		const rec = {
			at: new Date().toISOString(),
			channel: u.searchParams.get("channel"),
			current: u.searchParams.get("current"),
			product: u.searchParams.get("product"),
		};
		hits.push(rec);
		console.log(
			`[fake-ota] 命中 channel=${rec.channel} current=${rec.current} product=${rec.product}`
		);
		const body = JSON.stringify({
			apiVersion: "1.0",
			ok: true,
			product: rec.product || "agent",
			published: true,
			hasUpdate: true,
			latest: {
				version: VERSION,
				// 指向不存在的文件：万一被判成"该自动装"，下载会失败，绝不会真升级
				url: `http://127.0.0.1:${PORT}/this-file-does-not-exist.exe`,
				size: 1234,
				sha256: "0".repeat(64),
				notes: "假清单（探针夹具，不是真实版本）",
				mandatory: MANDATORY,
			},
		});
		res.writeHead(200, {
			"content-type": "application/json; charset=utf-8",
			"cache-control": "no-store",
		});
		res.end(body);
		return;
	}
	if (u.pathname === "/__hits") {
		res.writeHead(200, { "content-type": "application/json; charset=utf-8" });
		res.end(JSON.stringify(hits, null, 2));
		return;
	}
	res.writeHead(404, { "content-type": "text/plain" });
	res.end("not found");
});

server.listen(PORT, "127.0.0.1", () => {
	console.log(`[fake-ota] listening :${PORT} version=${VERSION} mandatory=${MANDATORY}`);
});
