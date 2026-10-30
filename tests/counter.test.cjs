// Browser test for examples/counter, for either build.
//   node tests/counter.test.cjs http://127.0.0.1:8080/
// Needs Playwright (npm i playwright; npx playwright install chromium).
const { chromium } = require('playwright');

const url = process.argv[2];
if (!url) {
  console.error('usage: node tests/counter.test.cjs <url>');
  process.exit(2);
}

let failures = 0;
function check(condition, message) {
  console.log(`  ${condition ? 'ok  ' : 'FAIL'} ${message}`);
  if (!condition) failures++;
}

// Handlers re-render nothing yet, but give the page a moment anyway rather than racing it.
async function settled(read, expected) {
  let last;
  for (const end = Date.now() + 5000; Date.now() < end; await new Promise((r) => setTimeout(r, 25))) {
    last = await read();
    if (expected instanceof RegExp ? expected.test(last) : last === expected) return last;
  }
  return last;
}

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const problems = [];
  page.on('pageerror', (e) => problems.push(`pageerror: ${e.message}`));
  page.on('console', (m) => {
    if (m.type() === 'error' || m.type() === 'warning') problems.push(`${m.type()}: ${m.text()}`);
  });
  const text = (selector) => () => page.textContent(selector);

  try {
    await page.goto(url);
    await page.waitForSelector('#counter', { timeout: 15000 });
    check((await page.title()) === 'volt2 counter', 'C++ set the page title');

    for (let i = 0; i < 3; i++) await page.click('#counter');
    check((await settled(text('#counter'), 'Clicked 3 times')) === 'Clicked 3 times', 'counter: C++ state survives between clicks');

    await page.evaluate(() => { window.peter_pan = true; window.twice = (x) => x * 2; });
    await page.click('#ask');
    let answer = await settled(text('#answer'), 'peter_pan is true, twice(21) is 42');
    check(answer === 'peter_pan is true, twice(21) is 42', `handler read a page global and called a page function: "${answer}"`);
    await page.evaluate(() => { window.peter_pan = false; delete window.twice; });
    await page.click('#ask');
    answer = await settled(text('#answer'), 'peter_pan is false');
    check(answer === 'peter_pan is false', `...and sees the page change: "${answer}"`);

    await page.click('#outer');
    const log = await settled(text('#log'), 'outer-start;inner;outer-end');
    check(log === 'outer-start;inner;outer-end', `nested call ran inside click(): "${log}"`);

    await page.click('#fail');
    const errors = await settled(text('#errors'), /not a function/);
    check(/^C\+\+ caught: TypeError: .*not a function/.test(errors), `JS error became a C++ exception: "${errors}"`);

    const sum = await page.evaluate(() => window.cppAdd(2, 3.5));
    check(sum === 5.5, `page JS called a C++ function: cppAdd(2, 3.5) = ${sum}`);
    const thrown = await page.evaluate(() => { try { window.cppThrow(); return 'no error'; } catch (e) { return e.message; } });
    check(thrown === 'thrown in C++', `C++ exception became a JS Error: "${thrown}"`);
    const badArg = await page.evaluate(() => { try { return window.cppAdd('a', 1); } catch (e) { return e.message; } });
    check(/expected a number, got string/.test(badArg), `bad argument type is reported: "${badArg}"`);

    const before = await page.evaluate(() => volt2.handleCount());
    await page.click('#stress');
    await settled(text('#stress'), 'done');
    for (let i = 0; i < 5; i++) await page.click('#counter');
    const after = await page.evaluate(() => volt2.handleCount());
    check(after <= before + 2, `handles are released: ${before} before, ${after} after 300 reads and 5 clicks`);

    check(problems.length === 0, `no page errors or console warnings${problems.length ? ': ' + problems.join(' | ') : ''}`);
  } catch (e) {
    failures++;
    console.log(`  FAIL ${e.message}`);
  } finally {
    await browser.close();
  }
  console.log(failures ? `FAILED (${failures})` : 'PASS');
  process.exitCode = failures ? 1 : 0;
})();
