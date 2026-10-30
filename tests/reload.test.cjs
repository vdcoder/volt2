// Development-build test: page reloads and a second tab replacing the first.
//   node tests/reload.test.cjs http://127.0.0.1:8080/
const { chromium } = require('playwright');

const url = process.argv[2];
let failures = 0;
function check(condition, message) {
  console.log(`  ${condition ? 'ok  ' : 'FAIL'} ${message}`);
  if (!condition) failures++;
}
async function settled(read, expected) {
  let last;
  for (const end = Date.now() + 8000; Date.now() < end; await new Promise((r) => setTimeout(r, 25))) {
    last = await read();
    if (expected instanceof RegExp ? expected.test(last) : last === expected) return last;
  }
  return last;
}
function watch(page, sink) {
  page.on('pageerror', (e) => sink.push(e.message));
}

(async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext();
  try {
    const first = await context.newPage();
    const firstErrors = [];
    watch(first, firstErrors);
    await first.goto(url);
    await first.click('#counter');
    await first.click('#counter');
    check((await first.textContent('#counter')) === 'Clicked 2 times', 'first tab counts');

    await first.reload();
    await first.waitForSelector('#counter');
    check((await first.textContent('#counter')) === 'Clicked 0 times', 'reload runs the site again from scratch');
    await first.click('#counter');
    check((await settled(() => first.textContent('#counter'), 'Clicked 1 times')) === 'Clicked 1 times', 'handlers work after reload');
    check((await first.evaluate(() => volt2.handleCount())) < 20, 'reload starts with a small handle table');

    // A second tab takes over; the first one is told so on its next call.
    const second = await context.newPage();
    const secondErrors = [];
    watch(second, secondErrors);
    await second.goto(url);
    await second.click('#counter');
    check((await second.textContent('#counter')) === 'Clicked 1 times', 'second tab works');
    await first.click('#counter');
    const replaced = await settled(() => firstErrors.join(' | '), /newer page replaced this one/);
    check(/newer page replaced this one/.test(replaced), `first tab is told it was replaced: "${replaced}"`);

    // Replace the page while a C++ handler is in the middle of its work.
    await second.evaluate(() => setTimeout(() => document.getElementById('stress').click(), 0));
    await new Promise((r) => setTimeout(r, 150)); // the handler is now making its 300 round trips
    const third = await context.newPage();
    await third.goto(url);
    await third.click('#counter');
    check((await settled(() => third.textContent('#counter'), 'Clicked 1 times')) === 'Clicked 1 times', 'a page loaded mid-handler works');
    const interrupted = await settled(() => secondErrors.join(' | '), /newer page replaced this one/);
    check(/newer page replaced this one/.test(interrupted), `the interrupted handler failed cleanly in its tab: "${interrupted}"`);
    await third.click('#outer');
    check((await settled(() => third.textContent('#log'), 'outer-start;inner;outer-end')) === 'outer-start;inner;outer-end',
      'nested calls still work afterwards');
  } catch (e) {
    failures++;
    console.log(`  FAIL ${e.message}`);
  } finally {
    await browser.close();
  }
  console.log(failures ? `FAILED (${failures})` : 'PASS');
  process.exitCode = failures ? 1 : 0;
})();
