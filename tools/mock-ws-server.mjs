// D2 本地靶子：最小 WebSocket 接收端，只为验证「被控端真的把帧发出来了」。
// 真云端在 D3（挂到 SvelteKit 站点）。这个脚本不进产品。
//
// 跑法（借用网站仓的 ws 包，免安装）：
//   NODE_PATH=<website>/node_modules node tools/mock-ws-server.mjs
import { WebSocketServer } from 'ws';

const port = Number(process.env.MOCK_WS_PORT || 8787);
const wss = new WebSocketServer({ port, host: '127.0.0.1' });

let frames = 0;
let bytes = 0;
let text = 0;

wss.on('connection', (ws) => {
  console.log(`[mock] 被控端已连接（当前连接数 ${wss.clients.size}）`);
  ws.on('message', (data, isBinary) => {
    if (isBinary) {
      frames += 1;
      bytes += data.length;
      console.log(`[mock] 帧#${frames} 收到 ${data.length} B｜累计 ${bytes} B`);
    } else {
      text += 1;
      console.log(`[mock] 文本#${text} ${data.toString()}`);
    }
  });
  ws.on('close', () => console.log('[mock] 被控端断开'));
  ws.on('error', (e) => console.error('[mock] 错误', e.message));
});

console.log(`[mock] ws://127.0.0.1:${port} 已在听（等被控端来连）`);

setInterval(() => {
  console.log(`[mock] 汇总：帧 ${frames} 张 / ${bytes} B / 文本 ${text} 条`);
}, 5000);