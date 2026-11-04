// Browser test for demos/diff.cpp, for either build: after each random change, the page must be
// exactly the tree C++ rendered, and volt2 must track exactly the nodes on the page.
//   node tests/diff.test.cjs http://127.0.0.1:8080/
const { chromium } = require('./browser.cjs');

const url = process.argv[2];
const steps = 400;
let failures = 0;
function check(condition, message) {
  console.log(`  ${condition ? 'ok  ' : 'FAIL'} ${message}`);
  if (!condition) failures++;
}

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') errors.push(m.text()); });
  try {
    await page.goto(url);
    await page.waitForSelector('#fuzz', { timeout: 15000 });
    console.log(`  (${await page.evaluate(() => volt2.mode)} build)`);
    const result = await page.evaluate((steps) => {
      // The same text as detail::outline: attributes sorted by name.
      const escape = (text, quote) => text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(quote ? /"/g : /$^/, '&quot;');
      const outline = (node) => {
        if (node.nodeType === 3) return escape(node.nodeValue, false);
        const tag = node.tagName.toLowerCase();
        const attributes = [...node.attributes].map((a) => [a.name, a.value]).sort((a, b) => (a[0] < b[0] ? -1 : 1));
        return '<' + tag + attributes.map(([n, v]) => ' ' + n + '="' + escape(v, true) + '"').join('') + '>' +
          [...node.childNodes].map(outline).join('') + '</' + tag + '>';
      };
      const count = (node) => 1 + [...node.childNodes].reduce((sum, child) => sum + count(child), 0);
      const mount = document.getElementById('volt2');
      for (let i = 0; i < steps; i++) {
        window.fuzzStep(1 + (i % 3));
        const shown = outline(mount.firstChild);
        const wanted = window.fuzzExpected();
        if (shown !== wanted) return { step: i, shown, wanted };
        if (volt2.nodeCount() !== count(mount)) return { step: i, tracked: volt2.nodeCount(), onPage: count(mount) };
      }
      return { steps, size: outline(mount.firstChild).length };
    }, steps);
    check(result.steps === steps, `after each of ${steps} random changes the page is exactly the rendered tree, and every tracked node is on it: ${JSON.stringify(result).slice(0, 600)}`);
    check(errors.length === 0, `no errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  } catch (e) {
    failures++;
    console.log(`  FAIL ${e.message}`);
  } finally {
    await browser.close();
  }
  console.log(failures ? `FAILED (${failures})` : 'PASS');
  process.exitCode = failures ? 1 : 0;
})();
