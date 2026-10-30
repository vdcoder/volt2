// Browser-side floor: the fastest possible server, replying "return" at once.
const http = require('http'); const { chromium } = require('playwright');
const page = `<script>function ping(){const x=new XMLHttpRequest();x.open('POST','/b',false);x.send('{}');return x.responseText;}</script>`;
const reply = JSON.stringify({ type: 'return', value: 'pong' });
const server = http.createServer((req, res) => {
  if (req.method === 'GET') { res.end(page); return; }
  req.on('data', () => {}); req.on('end', () => { res.setHeader('Content-Type', 'application/json'); res.end(reply); });
});
server.on('connection', s => s.setNoDelay(true));
server.listen(8710, '127.0.0.1', async () => {
  const b = await chromium.launch(); const p = await b.newPage();
  await p.goto('http://127.0.0.1:8710/');
  for (const n of [200, 1000, 1000]) {
    const ms = await p.evaluate(n => { const t = performance.now(); for (let i = 0; i < n; i++) ping(); return performance.now() - t; }, n);
    console.log(`Node server floor: ${(ms / n).toFixed(3)} ms per synchronous call (n=${n})`);
  }
  await b.close(); server.close();
});
