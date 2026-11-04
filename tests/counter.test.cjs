// Browser test for examples/counter (the markup starter), for either build.
//   node tests/counter.test.cjs http://127.0.0.1:8080/
const { chromium } = require('playwright');

const url = process.argv[2];
let failures = 0;
function check(condition, message) {
  console.log(`  ${condition ? 'ok  ' : 'FAIL'} ${message}`);
  if (!condition) failures++;
}

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const problems = [];
  page.on('pageerror', (e) => problems.push(`pageerror: ${e.message}`));
  page.on('console', (m) => {
    if (m.type() === 'error' || m.type() === 'warning') problems.push(`${m.type()}: ${m.text()}`);
  });
  const text = (selector) => page.textContent(selector);
  const fruits = () => page.$$eval('#fruits li', (items) => items.map((li) => li.firstChild.textContent));

  try {
    await page.goto(url);
    await page.waitForSelector('#count', { timeout: 15000 });
    check((await text('#count')) === 'Clicked 0 times', 'first render shows the count');
    check((await fruits()).join(',') === 'Apple,Banana,Cherry', 'and the list from the session');
    check((await page.$('#panel')) !== null && (await page.$('#warning')) === null, 'panel open, no warning yet');

    for (let i = 0; i < 3; i++) await page.click('#increment');
    await page.click('#decrement');
    check((await text('#count')) === 'Clicked 2 times', 'clicks change session state and the page renders again');

    for (let i = 0; i < 9; i++) await page.click('#increment');
    check((await text('#count')) === 'Clicked 11 times' && (await page.$('#warning')) !== null, 'a conditional element appears past 10');

    await page.click('#name');
    await page.keyboard.type('Ada');
    check((await text('#greeting')) === 'Hello, Ada!', `typing renders the greeting: "${await text('#greeting')}"`);
    check(await page.evaluate(() => document.activeElement.id === 'name' && document.activeElement.value === 'Ada'),
      'and the input keeps the focus and the text');

    await page.click('#toggle');
    check((await page.$('#panel')) === null && (await text('#toggle')) === 'Show the panel', 'the panel closes');
    await page.click('#toggle');
    check((await page.$('#panel')) !== null && (await text('#toggle')) === 'Hide the panel', 'and opens again');

    await page.$$eval('#fruits li', (items) => items.forEach((li) => { li.__fruit = li.firstChild.textContent; }));
    await page.click('#fruits li:nth-child(2) button');
    check((await fruits()).join(',') === 'Apple,Cherry', 'a list item removes itself');
    check(await page.$$eval('#fruits li', (items) => items.every((li) => li.__fruit === li.firstChild.textContent)),
      'and the others keep their elements');
    await page.click('#add');
    await page.click('#add');
    check((await fruits()).join(',') === 'Apple,Cherry,Fruit 1,Fruit 2', 'items are added');

    const before = await page.evaluate(() => volt2.handleCount());
    for (let i = 0; i < 20; i++) await page.click('#increment');
    const after = await page.evaluate(() => volt2.handleCount());
    check(after === before, `20 more renders hold no extra page objects (${before} -> ${after})`);

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
