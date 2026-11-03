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
    check(/^constructed 1, destroyed 0, renders \d+$/.test(await text('#lifecycle')), `session constructed once: "${await text('#lifecycle')}"`);

    await page.waitForFunction(() => document.getElementById('image')?.textContent === 'image loaded', null, { timeout: 5000 });
    const settled = await renders();
    await page.waitForTimeout(800);
    check((await renders()) === settled, `an image's load event does not loop (renders ${settled} -> ${await renders()})`);

    await page.evaluate(() => window.cppBump());
    check((await text('#bumps')) === 'bumps 1', 'invalidate() renders from C++ called outside any event');

    await page.click('#ct-inner');
    check((await text('#targets')) === 'ct-inner;ct-outer;', `currentTarget is each handler's own element: "${await text('#targets')}"`);

    const template = await page.$eval('#tpl', (el) => ({ content: el.content.childNodes.length, direct: el.childNodes.length }));
    check(template.content === 1 && template.direct === 0, `a template's children are its content: ${JSON.stringify(template)}`);

    const big = await page.$eval('#big', (el) => ({ text: el.textContent, data: el.dataset.n }));
    check(big.text === '9007199254740993 -9007199254740993' && big.data === '9007199254740993', `integers stay exact: ${JSON.stringify(big)}`);

    const flags = await page.$eval('#flags', (el) => ({
      draggable: el.getAttribute('draggable'), spellcheck: el.getAttribute('spellcheck'), hidden: el.hasAttribute('hidden'),
      expanded: el.getAttribute('aria-expanded'), on: el.dataset.on,
    }));
    check(flags.draggable === 'true' && flags.spellcheck === 'false' && !flags.hidden && flags.expanded === 'true' && flags.on === 'false',
      `true/false follows each attribute: ${JSON.stringify(flags)}`);

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

    const lockState = () => page.evaluate(() => {
      const input = document.getElementById('maybe-ro');
      return { readonly: input.hasAttribute('readonly'), title: input.getAttribute('title'), data: input.hasAttribute('data-x'),
               placeholder: input.hasAttribute('placeholder'), note: !!document.getElementById('locked-note') };
    });
    let state = await lockState();
    check(!state.readonly && state.title === null && !state.data && !state.placeholder && !state.note,
      `None, empty optionals and null pointers leave things out: ${JSON.stringify(state)}`);
    await page.click('#maybe-click');
    check((await log()).endsWith(',unlocked-click'), 'an event chosen by a conditional is wired up');
    await page.click('#lock');
    state = await lockState();
    check(state.readonly && state.title === 'locked' && !state.data && state.note,
      `the same conditionals, the other way: ${JSON.stringify(state)}`);
    const clicks = (await log()).split(',').length;
    await page.click('#maybe-click');
    check((await log()).split(',').length === clicks, 'an event replaced by None is not wired up');
    check((await text('#nones')) === 'text', `None and null text as children render nothing: "${await text('#nones')}"`);

    const before = await renders();
    await page.click('#relay');
    check((await log()).endsWith(',relay,inner,outer'), `a handler can make another event fire: "${await log()}"`);
    check((await renders()) === before + 1, `and the page renders once (${before} -> ${await renders()})`);

    await page.reload();
    await page.waitForSelector('#root');
    const expected = mode === 'development' ? /^constructed 2, destroyed 1, renders \d+$/ : /^constructed 1, destroyed 0, renders \d+$/;
    check(expected.test(await text('#lifecycle')), `a reload gets a fresh session, the old one destroyed: "${await text('#lifecycle')}"`);
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
