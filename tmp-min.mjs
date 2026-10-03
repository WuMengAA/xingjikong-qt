import WebSocket from 'ws';
const a = new WebSocket('ws://127.0.0.1:8788/ws/agent');
a.on('open', () => {
  console.log('open ok');
  a.send(JSON.stringify({ type: 'register', client: 'min', version: 'min-1', uid: 'min-uid', fps: 1, token: 'dev-cloud-token' }));
  console.log('register sent');
});
a.on('message', (d) => console.log('MSG <-', d.toString().slice(0, 160)));
a.on('close', (c, r) => console.log('close code=' + c + ' ' + String(r).slice(0, 60)));
a.on('error', (e) => console.log('err ' + e.message));
a.on('unexpected-response', (_req, res) => console.log('unexpected-response status=' + res.statusCode));
setTimeout(() => { console.log('--- 6s 到，结束 ---'); process.exit(0); }, 6000);
