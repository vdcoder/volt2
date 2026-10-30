const { chromium } = require('playwright');
const { spawn } = require('child_process');
const path = require('path');
const port = process.argv[2] || '8700';

(async () => {
  const native = spawn('python3', [path.join(__dirname, 'native.py'), port], { stdio: 'inherit' });
  await new Promise(r => setTimeout(r, 800));
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const consoleMsgs = [];
  page.on('console', m => consoleMsgs.push(`${m.type()}: ${m.text()}`));
  try {
    await page.goto(`http://127.0.0.1:${port}/`);
    await page.click('#go');
    const result = await page.evaluate(() => window.result);
    const trace = await page.evaluate(() => window.trace);
    const nativeLog = await page.evaluate(() => native('log'));
    console.log('result      :', JSON.stringify(result));
    console.log('page trace  :', trace.join(' > '));
    console.log('native trace:', nativeLog.join(' > '));
    console.log('DOM #out    :', await page.textContent('#out'));

    const n = 1000;
    const ms = await page.evaluate(n => { const t = performance.now(); for (let i = 0; i < n; i++) native('ping'); return performance.now() - t; }, n);
    console.log(`round trip  : ${(ms / n).toFixed(3)} ms average over ${n} synchronous calls`);
    console.log('console     :', consoleMsgs.length ? consoleMsgs.join(' | ') : '(nothing)');
    console.log('browser     :', browser.version());
  } finally {
    await browser.close();
    native.kill();
  }
})();
