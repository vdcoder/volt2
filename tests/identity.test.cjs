// Browser test for tests/identity, for either build.
//   node tests/identity.test.cjs http://127.0.0.1:8080/
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
  const warnings = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => {
    if (m.type() === 'error') errors.push(m.text());
    if (m.type() === 'warning') warnings.push(m.text());
  });
  const text = (selector) => page.textContent(selector);
  const value = (selector) => page.$eval(selector, (el) => el.value);
  const focused = () => page.evaluate(() => document.activeElement && document.activeElement.id);
  const renders = async () => Number((await text('#renders')).match(/\d+/)[0]);
  // Tags a DOM node, to tell later whether it is still the same node.
  const mark = (selector, tag) => page.$eval(selector, (el, tag) => { el.__mark = tag; }, tag);
  const markOf = (selector) => page.$eval(selector, (el) => el.__mark || null);

  try {
    await page.goto(url);
    await page.waitForSelector('#root', { timeout: 15000 });
    console.log(`  (${await page.evaluate(() => volt2.mode)} build)`);

    check((await text('#mapped')) === '0:p1 1:p2 2:p3 ', `Map passes the index: "${await text('#mapped')}"`);
    check((await text('#looped')) === '0 10 20 ', `Loop: "${await text('#looped')}"`);
    check((await text('#badge')).startsWith('member after '), `a session member component: "${await text('#badge')}"`);

    await page.waitForFunction(() => document.getElementById('loaded').textContent === 'loaded', null, { timeout: 5000 });
    check(true, 'an element\'s own event (load) renders by itself');
    const settled = await renders();
    await page.waitForTimeout(500);
    check((await renders()) === settled, `and does not loop (renders ${settled} -> ${await renders()})`);

    // Typing.
    await mark('#name', 'name');
    await page.click('#name');
    await page.keyboard.type('Volt');
    check((await value('#name')) === 'Volt' && (await text('#greeting')) === 'Hello, Volt', `typing renders each key and keeps the text: "${await value('#name')}"`);
    check((await focused()) === 'name' && (await markOf('#name')) === 'name', 'the input keeps its node and the focus');
    await page.keyboard.press('Home');
    await page.keyboard.type('XY');
    check((await value('#name')) === 'XYVolt', `the caret stays where it was between renders: "${await value('#name')}"`);
    await page.click('#clear');
    check((await value('#name')) === '' && (await text('#greeting')) === 'Hello, ', 'C++ can clear the input: its value follows the state');

    // An element appearing above the focused input.
    await mark('#query', 'query');
    await page.click('#query');
    await page.keyboard.type('abcd');
    check((await text('#banner')) === 'searching for abcd', 'an element appears above the input while typing');
    check((await value('#query')) === 'abcd' && (await focused()) === 'query' && (await markOf('#query')) === 'query',
      'and the input keeps its node, its text and the focus');

    // Keyed and unkeyed reordering.
    await page.$$eval('#keyed li, #plain li', (items) => items.forEach((li) => { li.__mark = li.firstChild.nodeValue; }));
    await page.click('#reverse');
    const keyed = await page.$$eval('#keyed li', (items) => items.map((li) => [li.firstChild.nodeValue, li.__mark]));
    check(keyed.map((k) => k[0]).join('') === 'dcba' && keyed.every((k) => k[0] === k[1]),
      `keyed items move with their nodes: ${JSON.stringify(keyed)}`);
    const plain = await page.$$eval('#plain li', (items) => items.map((li) => [li.textContent, li.__mark]));
    check(plain.map((p) => p[0]).join(',') === 'p3,p2,p1' && plain.map((p) => p[1]).join(',') === 'p1,p2,p3',
      `unkeyed items keep their places and change their text: ${JSON.stringify(plain)}`);

    // The focused item stays put while the others move around it.
    await page.click('#keyed li:nth-child(1) input'); // item d
    await page.keyboard.type('typed');
    const moved = await page.evaluate(() => {
      window.__removed = [];
      window.__observer = new MutationObserver((records) => records.forEach((r) => r.removedNodes.forEach((n) => window.__removed.push(n.__mark))));
      window.__observer.observe(document.getElementById('keyed'), { childList: true });
      return true;
    });
    await page.keyboard.press('Enter');
    const after = await page.$$eval('#keyed li', (items) => items.map((li) => li.firstChild.nodeValue).join(''));
    const removed = await page.evaluate(() => { window.__observer.disconnect(); return window.__removed; });
    const active = await page.evaluate(() => [document.activeElement.parentNode.__mark, document.activeElement.value]);
    check(moved && after === 'cbad' && active[0] === 'd' && active[1] === 'typed', `the list rotates and the focused input keeps focus and text: ${after}, ${JSON.stringify(active)}`);
    check(!removed.includes('d'), `the focused item is not moved, the others move around it (moved: ${removed.join(',')})`);

    // An element with an id moves to another parent with its node.
    await mark('#player', 'player');
    await page.click('#player-input');
    await page.keyboard.type('abc');
    await page.$eval('#player-input', (el) => el.setSelectionRange(1, 2));
    await page.keyboard.press('Enter');
    const player = await page.evaluate(() => {
      const el = document.getElementById('player');
      const input = document.getElementById('player-input');
      return { parent: el.parentNode.id, mark: el.__mark, value: input.value, focused: document.activeElement === input,
               selection: [input.selectionStart, input.selectionEnd] };
    });
    check(player.parent === 'right' && player.mark === 'player' && player.value === 'abc',
      `an element with an id moves to another parent with its node: ${JSON.stringify(player)}`);
    check(player.focused && player.selection.join() === '1,2', 'a moved focused input gets its focus and selection back');

    // Components versus plain functions.
    await mark('#cards > div', 'first');
    await mark('#plain-cards > div', 'first');
    await page.fill('#cards input', 'kept?');
    await page.fill('#plain-cards input', 'kept?');
    await page.click('#flip');
    const card = await page.evaluate(() => {
      const one = (id) => { const el = document.querySelector(id + ' > div'); return { title: el.querySelector('h3').textContent, mark: el.__mark || null, input: el.querySelector('input').value }; };
      return { component: one('#cards'), plain: one('#plain-cards') };
    });
    check(card.component.title === 'b' && card.component.mark === null && card.component.input === '',
      `each call site of a Component is its own element: ${JSON.stringify(card.component)}`);
    check(card.plain.title === 'b' && card.plain.mark === 'first' && card.plain.input === 'kept?',
      `a plain function's calls are the same element: ${JSON.stringify(card.plain)}`);

    // Duplicate ids.
    await page.click('#duplicate');
    check((await page.$$('#dups span')).length === 2, 'two elements with the same id both render');
    check(warnings.some((w) => w.includes('two elements have id="dup"')), `and the console says so: ${JSON.stringify(warnings)}`);

    // Form controls.
    await page.click('#agree');
    check(await page.$eval('#agree', (el) => el.checked), 'a checkbox the user checks stays checked');
    await page.click('#disagree');
    check(!(await page.$eval('#agree', (el) => el.checked)), 'and C++ can uncheck it');
    check((await value('#color')) === 'green', `a select shows its value: "${await value('#color')}"`);
    await page.click('#blue');
    check((await value('#color')) === 'blue', 'and follows the state');
    await page.selectOption('#color', 'red');
    check((await text('#color-state')) === 'red' && (await value('#color')) === 'red', 'and the user can choose');

    // A render where only the render count changes touches only the two texts showing it.
    const records = await page.evaluate(async () => {
      const seen = [];
      const observer = new MutationObserver((list) => list.forEach((r) => seen.push(r.type + ':' + (r.target.nodeValue || r.target.id))));
      observer.observe(document.getElementById('volt2'), { subtree: true, childList: true, attributes: true, characterData: true });
      document.getElementById('noop').click();
      await new Promise((resolve) => setTimeout(resolve, 50));
      observer.disconnect();
      return seen;
    });
    const count = String(await renders());
    check(records.length === 2 && records.every((r) => r === 'characterData:' + count),
      `a render changes only what changed, here the two render counts: ${JSON.stringify(records)}`);

    // The page forgets nodes it no longer shows.
    const counts = await page.evaluate(() => {
      let count = 0;
      const walk = (node) => { count++; node.childNodes.forEach(walk); };
      walk(document.getElementById('volt2'));
      return { tracked: volt2.nodeCount(), shown: count };
    });
    check(counts.tracked === counts.shown, `every node volt2 tracks is on the page: ${JSON.stringify(counts)}`);

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
