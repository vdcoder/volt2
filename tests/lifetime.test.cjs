// Browser test for tests/lifetime, for either build.
//   node tests/lifetime.test.cjs http://127.0.0.1:8080/
// Start the development build with VOLT2_WARN_AT=200 so the leak warning can be checked.
const { chromium } = require('playwright');

const url = process.argv[2];
let failures = 0;
function check(condition, message) {
  console.log(`  ${condition ? 'ok  ' : 'FAIL'} ${message}`);
  if (!condition) failures++;
}

(async () => {
  const browser = await chromium.launch({ args: ['--js-flags=--expose-gc'] });
  const page = await browser.newPage();
  const errors = [];
  const warnings = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => {
    if (m.type() === 'error') errors.push(m.text());
    if (m.type() === 'warning') warnings.push(m.text());
  });
  const call = (expression) => page.evaluate(expression);
  const counts = () => call(() => ({ ...window.cppStats(), handles: volt2.handleCount() }));
  const same = (a, b) => a.values === b.values && a.functions === b.functions && a.handles === b.handles;
  const show = (c) => `values ${c.values}, functions ${c.functions}, page handles ${c.handles}`;

  try {
    await page.goto(url);
    await page.waitForFunction(() => typeof window.cppStats === 'function', null, { timeout: 15000 });
    const mode = await call(() => volt2.mode);
    console.log(`  (${mode} build)`);
    const start = await counts();

    for (let round = 1; round <= 3; round++) {
      await call(() => window.churn(100));
      const now = await counts();
      check(same(now, start), `churn round ${round}: 100 listeners created and released, back to start (${show(now)})`);
    }

    const twice = await call(() => window.holdTwice());
    check(twice.same && twice.equal && twice.added === 1, `same object held twice is one handle, equal locally: ${JSON.stringify(twice)}`);
    const held = await counts();
    check(held.handles === start.handles + 1, `page side agrees: ${start.handles} -> ${held.handles} handles`);

    check((await call(() => window.keyed())) === true, 'Value works as an unordered_map key (objects by identity, NaN == NaN)');

    check((await call(() => window.releasedFunction())) === 'still here', 'a held js::Function can be called');
    const released = await call(() => { try { window.temporary(); return 'no error'; } catch (e) { return e.message; } });
    check(/was released/.test(released), `after release, calling it throws: "${released}"`);
    const beforeStale = await call(() => volt2.handleCount());
    await call(() => { for (let i = 0; i < 20; i++) { try { window.temporary(document.createElement('i')); } catch (e) {} } });
    const afterStale = await call(() => volt2.handleCount());
    check(afterStale === beforeStale, `calling a released function with objects leaks nothing: ${beforeStale} -> ${afterStale} handles`);

    const tag = await call(() => { try { return window.rvalueArguments(); } catch (e) { return e.message; } });
    check(tag === 'SPAN', `temporaries passed straight into call and set work: "${tag}"`);

    await call(() => window.holdMany(250));
    const many = await counts();
    check(many.values >= held.values + 250 && many.functions >= held.functions + 250, `holding 250 more: ${show(many)}`);
    if (mode === 'development') {
      check(warnings.some((w) => /C\+\+ now holds \d+ JavaScript values/.test(w)), 'development warns about growing values');
      check(warnings.some((w) => /C\+\+ now holds \d+ C\+\+ functions/.test(w)), 'development warns about growing functions');
    } else {
      check(warnings.length === 0, 'WebAssembly build stays quiet');
    }
    await call(() => window.dropAll());
    const dropped = await counts();
    check(same(dropped, held), `dropping them returns to before (${show(dropped)})`);

    const fresh = await call(() => { try { return window.freshAfterGrowth(900).tagName; } catch (e) { return e.message; } });
    check(fresh === 'B', `a fresh object returned while a leak warning fires still arrives: "${fresh}"`);
    await call(() => window.dropAll());

    const beforeThread = await counts();
    const thread = await call(() => window.fromThread());
    if (mode === 'development') {
      check(/^[^|]*only be used on the thread that called volt2::run/.test(thread), `another thread is refused: "${thread.split(' | ')[0]}"`);
      check(/\|.*only be used on the thread that called volt2::run/.test(thread), `stats() on another thread is refused: "${thread.split(' | ')[1]}"`);
      const afterThread = await counts();
      check(same(afterThread, beforeThread), `a Value destroyed on another thread is still released (${show(beforeThread)} -> ${show(afterThread)})`);
      check((await call(() => window.dropFunctionOnThread())) === true, 'a js::Function was destroyed on another thread');
      const result = await call(() => { try { return window.droppedOnThread(); } catch (e) { return e.message; } });
      check(/was released/.test(String(result)), `...and JavaScript is told it was released: "${result}"`);
    } else {
      check(thread === 'no threads in this build', 'WebAssembly build has no threads');
    }

    await page.reload();
    await page.waitForFunction(() => typeof window.cppStats === 'function');
    const reloaded = await counts();
    check(same(reloaded, start), `reload starts clean (${show(reloaded)})`);

    const unexpected = warnings.filter((w) => !/C\+\+ now holds/.test(w));
    check(errors.length === 0 && unexpected.length === 0, `no page errors or unexpected warnings${errors.concat(unexpected).length ? ': ' + errors.concat(unexpected).join(' | ') : ''}`);
  } catch (e) {
    failures++;
    console.log(`  FAIL ${e.message}`);
  } finally {
    await browser.close();
  }
  console.log(failures ? `FAILED (${failures})` : 'PASS');
  process.exitCode = failures ? 1 : 0;
})();
