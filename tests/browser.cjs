// Chromium for the browser tests. Requests that would leave this machine (stylesheets, fonts
// and scripts from CDNs, see the demos' head option) get an empty answer, so the tests neither depend on
// the network nor wait for it. Use it like Playwright's: const { chromium } = require('./browser.cjs');
const playwright = require('playwright');

function external(url) {
  return url.hostname !== '127.0.0.1' && url.hostname !== 'localhost';
}

async function empty(route) {
  const types = { stylesheet: 'text/css', script: 'text/javascript', font: 'font/woff2' };
  await route.fulfill({ status: 200, contentType: types[route.request().resourceType()] || 'text/plain', body: '' });
}

const chromium = {
  async launch(options) {
    const browser = await playwright.chromium.launch(options);
    const newPage = browser.newPage.bind(browser);
    const newContext = browser.newContext.bind(browser);
    browser.newPage = async (...args) => {
      const page = await newPage(...args);
      await page.route(external, empty);
      return page;
    };
    browser.newContext = async (...args) => {
      const context = await newContext(...args);
      await context.route(external, empty);
      return context;
    };
    return browser;
  },
};

module.exports = { chromium };
