// Browser test for tests/markup, for either build.
//   node tests/markup.test.cjs http://127.0.0.1:8080/
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
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  const text = (selector) => page.textContent(selector);
  const log = () => text('#log');
  const renders = async () => Number((await text('#lifecycle')).match(/renders (\d+)/)[1]);

  try {
    await page.goto(url);
    await page.waitForSelector('#root', { timeout: 15000 });
    const mode = await page.evaluate(() => volt2.mode);
    console.log(`  (${mode} build)`);
    check((await text('#lifecycle')) === 'constructed 1, destroyed 0, renders 1', `session constructed once: "${await text('#lifecycle')}"`);

    const box = await page.$eval('#box', (el) => ({
      type: el.getAttribute('type'), checked: el.hasAttribute('checked') && el.checked, disabled: el.hasAttribute('disabled'),
      tabindex: el.getAttribute('tabindex'), role: el.dataset.role, label: el.getAttribute('aria-label'),
    }));
    check(box.type === 'checkbox' && box.checked && !box.disabled && box.tabindex === '3' && box.role === 'demo' && box.label === 'A box',
      `attributes: strings, true, false (left out), numbers, data-, aria-: ${JSON.stringify(box)}`);

    const literal = '<b>not bold</b> & "quoted"';
    check((await text('#text')) === literal && (await page.$$('#text b')).length === 0, 'text stays text, never HTML');
    check((await text('#numbers')) === '42 2.5 -0.125 1e+21', `numbers: "${await text('#numbers')}"`);

    await page.click('#inner');
    check((await log()) === 'inner,outer', `bubbling: target first, then ancestors: "${await log()}"`);
    await page.click('#stop');
    check((await log()) === 'inner,outer,stop', `stopPropagation stops it: "${await log()}"`);

    const address = page.url();
    await page.click('#send');
    await page.waitForTimeout(300);
    check(page.url() === address && (await log()).endsWith(',submit'), 'preventDefault keeps the form from navigating');

    await page.fill('#name', 'Volt');
    check((await text('#typed')) === 'typed: Volt', `event values reach C++: "${await text('#typed')}"`);

    await page.focus('#focusme');
    check((await log()).endsWith(',focus') && !(await log()).includes('wrap-focus'), `a non-bubbling event reaches only its target: "${await log()}"`);

    check((await page.$$eval('#list li', (items) => items.map((li) => li.textContent))).join(',') === 'a,b,c',
      'children: a range, empty and full optionals, nothing');

    await page.click('#throw');
    await page.waitForTimeout(100);
    check(errors.some((e) => /handler failed/.test(e)), 'a throwing handler reports its error to the page');
    check((await log()).endsWith(',throw'), 'and the page still renders the new state');

    const before = await renders();
    await page.click('#relay');
    check((await log()).endsWith(',relay,inner,outer'), `a handler can make another event fire: "${await log()}"`);
    check((await renders()) === before + 1, `and the page renders once (${before} -> ${await renders()})`);

    await page.reload();
    await page.waitForSelector('#root');
    const expected = mode === 'development' ? 'constructed 2, destroyed 1, renders 1' : 'constructed 1, destroyed 0, renders 1';
    check((await text('#lifecycle')) === expected, `a reload gets a fresh session, the old one destroyed: "${await text('#lifecycle')}"`);
    check((await page.title()) !== 'set by a destroyed session', `a destroyed session's JavaScript calls do not reach the new page (title "${await page.title()}")`);

    const unexpected = errors.filter((e) => !/handler failed/.test(e));
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
