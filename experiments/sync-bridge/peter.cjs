// Your scenario: click -> C++ handler -> C++ reads window.peter_pan while the page is blocked.
// Second run: the handler pauses for DELAY seconds first, like a developer at a breakpoint.
const { chromium } = require('playwright'); const { spawn } = require('child_process'); const path = require('path');
const port = process.argv[2], delay = Number(process.argv[3] || 0);
(async () => {
  const native = spawn('python3', [path.join(__dirname, 'native.py'), port], { stdio: 'inherit' });
  await new Promise(r => setTimeout(r, 800));
  const browser = await chromium.launch(); const page = await browser.newPage();
  const msgs = []; page.on('console', m => msgs.push(m.text())); page.on('pageerror', e => msgs.push('pageerror ' + e.message));
  try {
    await page.goto(`http://127.0.0.1:${port}/`);
    await page.evaluate(d => { window.peterDelay = d; setTimeout(() => document.getElementById('peter').click(), 0); }, delay);
    const t0 = Date.now();
    await page.waitForFunction(() => window.peterResult !== undefined, null, { timeout: (delay + 120) * 1000, polling: 200 });
    console.log(`delay ${delay}s -> handler returned ${await page.evaluate(() => window.peterResult)} after ${((Date.now() - t0) / 1000).toFixed(1)}s;`,
                `page shows "${await page.textContent('#out')}"; console: ${msgs.length ? msgs.join(' | ') : '(nothing)'}`);
    await page.evaluate(() => { window.peter_pan = false; window.peterResult = undefined; window.peterDelay = 0; document.getElementById('peter').click(); });
    console.log(`after setting window.peter_pan = false -> handler returned ${await page.evaluate(() => window.peterResult)}`);
  } finally { await browser.close(); native.kill(); }
})();
