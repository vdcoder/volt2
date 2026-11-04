// Browser test for demos/locals.cpp, for either build.
//   node tests/locals.test.cjs http://127.0.0.1:8080/
const { chromium } = require('./browser.cjs');

const url = process.argv[2];
let failures = 0;
function check(condition, message) {
  console.log(`  ${condition ? 'ok  ' : 'FAIL'} ${message}`);
  if (!condition) failures++;
}

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const errors = [];
  const warnings = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => {
    if (m.type() === 'error') errors.push(m.text());
    if (m.type() === 'warning') warnings.push(m.text());
  });
  const toggles = (selector) => page.$$eval(selector + ' .toggle', (all) => all.map((t) => t.textContent));
  const text = (selector) => page.textContent(selector);

  try {
    await page.goto(url);
    await page.waitForSelector('#root', { timeout: 15000 });
    console.log(`  (${await page.evaluate(() => volt2.mode)} build)`);

    await page.click('#first button');
    await page.click('#second button');
    await page.click('#second button');
    check((await text('#first')) === 'first 1 open' && (await text('#second')) === 'second 2',
      `each call site of a component has its own state: "${await text('#first')}", "${await text('#second')}"`);

    await page.click('#site');
    check((await text('#site')) === 'site 1', 'site() itself can keep state');

    // Keyed items: state follows the item.
    await page.click('#keyed .pair:nth-child(2) .toggle:nth-of-type(1) button'); // b-1
    await page.fill('#keyed .pair:nth-child(2) input', 'hello');
    await page.$$eval('#keyed .pair', (all) => all.forEach((p) => { p.__mark = p.querySelector('.toggle').textContent.split('-')[0]; }));
    await page.click('#reverse');
    const b = await page.$eval('#keyed .pair:nth-child(2)', (p) => ({ toggles: [...p.querySelectorAll('.toggle')].map((t) => t.textContent), note: p.querySelector('input').value, mark: p.__mark }));
    check(b.toggles[0] === 'b-1 1 open' && b.toggles[1] === 'b-2 0' && b.note === 'hello',
      `nested components keep their state, and it follows a keyed item: ${JSON.stringify(b)}`);
    check(b.mark === 'b', 'and the item keeps its element (Map gives it the key)');
    await page.click('#remove-first'); // removes c (the list is c, b, a)
    await page.click('#add-c');
    const keyed = await page.$$eval('#keyed .pair', (all) => all.map((p) => p.querySelector('.toggle').textContent + '|' + p.querySelector('input').value));
    check(keyed.join(',') === 'b-1 1 open|hello,a-1 0|note,c-1 0|note', `a removed item's state is gone; added again, it starts over: ${keyed.join(',')}`);

    // Unkeyed items: state stays at its position.
    await page.click('#plain .toggle:nth-child(1) button');
    await page.click('#reverse-plain');
    check((await toggles('#plain')).join(',') === 'y 1 open,x 0', `without a key, state stays at its position: ${(await toggles('#plain')).join(',')}`);

    await page.click('#looped .toggle:nth-child(2) button');
    check((await toggles('#looped')).join(',') === 'loop0 0,loop1 1 open', `each Loop item has its own state: ${(await toggles('#looped')).join(',')}`);

    // Not called: gone. Called but not shown: kept.
    await page.click('#toggle-first');
    await page.click('#toggle-first');
    check((await text('#first')) === 'first 0', `a component not called for a render starts over: "${await text('#first')}"`);
    await page.click('#cached button');
    await page.click('#toggle-cached');
    check((await text('#cached')) === '', 'a component called but not shown is not on the page');
    await page.click('#toggle-cached');
    check((await text('#cached')) === 'cached 1 open', `and keeps its state: "${await text('#cached')}"`);

    // Mistakes are reported.
    await page.click('#repeat-on');
    check(warnings.some((w) => /ran twice in one render.*use Map/.test(w)), `the same call twice in one render is reported: ${JSON.stringify(warnings)}`);
    await page.click('#misuse');
    await page.waitForTimeout(100);
    check(errors.some((e) => /volt2::local works while the page renders/.test(e)), 'local() outside a render is an error');
    const unexpected = errors.filter((e) => !/volt2::local works while the page renders/.test(e));
    check(unexpected.length === 0, `no other errors${unexpected.length ? ': ' + unexpected.join(' | ') : ''}`);
  } catch (e) {
    failures++;
    console.log(`  FAIL ${e.message}`);
  } finally {
    await browser.close();
  }
  console.log(failures ? `FAILED (${failures})` : 'PASS');
  process.exitCode = failures ? 1 : 0;
})();
