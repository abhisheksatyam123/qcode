// Headless browser check for the web Subagents panel's Stop button.
//
// Driven by scripts/webui_stop_smoke.py, which starts `qcode serve` with the
// stub provider and a lead turn that keeps one background subagent running.
// This script opens that session in Chromium, clicks Stop on the running row,
// and waits until the panel shows the row as killed.
//
// Environment:
//   QCODE_BASE    server base URL, e.g. http://127.0.0.1:8123
//   QCODE_SID     lead session id
//   CHROME_PATH   Chromium binary (optional; Playwright's bundled one otherwise)
//
// Exit 0 when the row turned killed, 1 otherwise. Exit 77 when Playwright is
// not installed, so the CTest wrapper can skip.

let chromium;
try {
  ({ chromium } = require('playwright'));
} catch (_) {
  console.error('playwright is not installed');
  process.exit(77);
}

const base = process.env.QCODE_BASE;
const sid = process.env.QCODE_SID;
const executablePath = process.env.CHROME_PATH || undefined;

(async () => {
  const browser = await chromium.launch({
    headless: true,
    executablePath,
    args: ['--no-sandbox'],
  });
  let page = null;
  try {
    page = await browser.newPage({ viewport: { width: 1200, height: 800 } });
    const errors = [];
    page.on('pageerror', (e) => errors.push(String(e)));

    await page.goto(base + '/#/session/' + sid, { waitUntil: 'domcontentloaded' });
    await page.click('#tab-sessions-btn');

    const stop = page.locator('.agent-card-stop').first();
    await stop.waitFor({ state: 'visible', timeout: 20000 });
    console.log('stop button visible on a running row');

    // Stop posts /tasks/<id>/kill. The kill is recorded at once, but the
    // subagent's provider call is still in flight, so the stub releases it
    // here, the way a provider answers once it is stopped.
    const killed = page.waitForResponse((r) => r.url().includes('/kill') && r.request().method() === 'POST');
    await stop.click();
    const kill = await killed;
    console.log('kill route answered ' + kill.status());
    if (process.env.QCODE_RELEASE_URL) {
      await fetch(process.env.QCODE_RELEASE_URL).catch(() => {});
    }

    // The panel reloads after the kill. The killed row shows a killed chip and
    // no Stop button.
    await page.locator('.status-chip.killed').first().waitFor({ state: 'visible', timeout: 15000 });
    const stillRunning = await page.locator('.agent-card-stop').count();
    if (stillRunning !== 0) {
      throw new Error('a Stop button is still shown after the kill');
    }

    if (errors.length) {
      throw new Error('page errors: ' + errors.join(' | '));
    }
    console.log('OK: Stop on a running subagent turned its row killed');
    process.exit(0);
  } catch (e) {
    console.error('FAIL: ' + (e && e.message ? e.message : e));
    if (page && process.env.QCODE_SHOT) {
      await page.screenshot({ path: process.env.QCODE_SHOT, fullPage: true }).catch(() => {});
      const html = await page.locator('#sessions-content').innerHTML().catch(() => '(none)');
      console.error('sessions-content: ' + html.slice(0, 1500));
    }
    process.exit(1);
  } finally {
    await browser.close();
  }
})();
