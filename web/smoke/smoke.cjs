// Smoke test for the browser build in headless Chromium (SwiftShader WebGL 2).
//
//   node web/smoke/smoke.cjs http://127.0.0.1:8080/ OUT_DIR [--phone]
//
// Loads the page, waits for the solar system, and checks that frames keep
// coming (it prints the frame rate), that the frame is drawn (bodies, orbits,
// asteroids and labels, and lit pixels on screen), that a drag orbits the
// camera, that zooming works (the wheel on the desktop, a two-finger pinch on
// the phone) and that the toolbar changes the focus. Screenshots go to OUT_DIR.
// --phone uses Playwright's iPhone 13 profile (touch, 390x664, DPR 3).
const fs = require('fs');
const path = require('path');
const { chromium, devices } = require('playwright');

const [url, out, ...flags] = process.argv.slice(2);
if (!url || !out) {
  console.error('usage: node web/smoke/smoke.cjs URL OUT_DIR [--phone]');
  process.exit(2);
}
const phone = flags.includes('--phone');
fs.mkdirSync(out, { recursive: true });

function check(ok, what) {
  if (!ok) throw new Error('FAIL: ' + what);
  console.log('ok   ' + what);
}

// Fraction of pixels in a PNG brighter than the background, worked out in the
// page (a 2D canvas decodes the PNG; no image library needed here).
async function litFraction(page, png) {
  return page.evaluate(async (b64) => {
    const img = new Image();
    img.src = 'data:image/png;base64,' + b64;
    await img.decode();
    const c = document.createElement('canvas');
    c.width = img.width; c.height = img.height;
    const g = c.getContext('2d');
    g.drawImage(img, 0, 0);
    const d = g.getImageData(0, 0, c.width, c.height).data;
    let lit = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i] + d[i + 1] + d[i + 2] > 60) lit++;
    return lit / (d.length / 4);
  }, png.toString('base64'));
}

async function shot(page, name) {
  const file = path.join(out, name + '.png');
  // A clipped page screenshot: an element screenshot waits for the element to
  // be "stable" across animation frames, which a busy WebGL page may never be.
  const box = await page.locator('canvas#canvas').boundingBox();
  await page.evaluate(() => window.orrery.hold(1));
  await page.waitForTimeout(300);
  let png;
  for (let attempt = 1; ; attempt++) {
    try {
      png = await page.screenshot({ path: file, clip: box, timeout: 30000 });
      break;
    } catch (e) {
      if (attempt >= 3) throw e;
      console.log('(screenshot retry ' + attempt + ': ' + String(e).split('\n')[0] + ')');
    }
  }
  await page.evaluate(() => window.orrery.hold(0));
  return png;
}

(async () => {
  const browser = await chromium.launch({
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
  const context = await browser.newContext(phone ? { ...devices['iPhone 13'] } : { viewport: { width: 1280, height: 800 } });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => {
    if (m.type() === 'error') errors.push(m.text());
    if (process.env.ORRERY_SMOKE_VERBOSE) console.log('page: ' + m.text());
  });
  try {
    // 'commit': the page's main loop keeps the load event from ever mattering.
    await page.goto(url, { waitUntil: 'commit' });
    await page.waitForFunction(() => window.orreryReady === true, null, { timeout: 120000 });
    const status = () => page.evaluate(() => window.orrery.status());
    await page.waitForFunction(() => window.orrery.status().frames > 5, null, { timeout: 60000 });

    // Frame rate: frames counted by the C++ loop over three seconds.
    const a = await status();
    const t0 = Date.now();
    await page.waitForTimeout(3000);
    const b = await status();
    const fps = ((b.frames - a.frames) * 1000) / (Date.now() - t0);
    console.log(`frame rate: ${fps.toFixed(1)} fps (${b.width}x${b.height} device pixels, ${phone ? 'phone' : 'desktop'})`);
    fs.writeFileSync(path.join(out, 'fps.txt'), fps.toFixed(1) + '\n');
    check(fps > 1, 'frames keep coming (' + fps.toFixed(1) + ' fps)');
    check(b.width > 100 && b.height > 100, `the canvas is drawn at its page size (${b.width}x${b.height})`);
    check(b.spheres >= 38 && b.points > 1000 && b.lineVertices > 1000 && b.labelCount >= 2,
      `the frame holds the solar system (${b.spheres} spheres, ${b.points} points, ${b.lineVertices} line vertices, ${b.labelCount} labels)`);
    check(b.firstDate <= '1980-01-01' && b.lastDate >= '2069-12-31', `date range ${b.firstDate} .. ${b.lastDate}`);

    const first = await shot(page, 'overview');
    // The time display moves on every second, so the input checks below run
    // paused: then only the input itself can change the frame.
    await page.evaluate(() => window.orrery.key(44, 0));  // Space
    await page.waitForFunction(() => window.orrery.status().paused === true);
    await page.waitForTimeout(500);
    const still = await shot(page, 'paused');
    check((await shot(page, 'paused-again')).equals(still), 'paused, the frame holds still');
    const lit = await litFraction(page, first);
    check(lit > 0.002, 'the canvas is not blank (lit ' + lit.toFixed(4) + ')');

    // Orbit: drag across the canvas (mouse, or one finger on the phone).
    const box = await page.locator('canvas#canvas').boundingBox();
    const cx = box.x + box.width / 2, cy = box.y + box.height / 2;
    let cdp = null;
    const touch = async (type, points) => {
      await cdp.send('Input.dispatchTouchEvent', { type, touchPoints: points.map(([x, y], id) => ({ x, y, id })) });
    };
    if (phone) {
      cdp = await context.newCDPSession(page);
      await touch('touchStart', [[cx - 80, cy]]);
      for (let i = 1; i <= 10; i++) await touch('touchMove', [[cx - 80 + i * 16, cy + i * 4]]);
      await touch('touchEnd', []);
    } else {
      await page.mouse.move(cx - 150, cy);
      await page.mouse.down();
      for (let i = 1; i <= 10; i++) await page.mouse.move(cx - 150 + i * 30, cy + i * 6);
      await page.mouse.up();
    }
    await page.waitForTimeout(300);
    const turned = await shot(page, 'orbited');
    check(!turned.equals(still), 'a drag orbits the camera (the frame changed)');

    // Zoom: the wheel, or a two-finger pinch outwards.
    const before = await shot(page, 'before-zoom');
    if (phone) {
      await touch('touchStart', [[cx - 20, cy], [cx + 20, cy]]);
      for (let i = 1; i <= 10; i++) await touch('touchMove', [[cx - 20 - i * 10, cy], [cx + 20 + i * 10, cy]]);
      await touch('touchEnd', []);
    } else {
      await page.mouse.move(cx, cy);
      for (let i = 0; i < 5; i++) await page.mouse.wheel(0, -100);
    }
    await page.waitForTimeout(300);
    const zoomed = await shot(page, 'zoomed');
    check(!zoomed.equals(before), (phone ? 'a pinch' : 'the wheel') + ' zooms (the frame changed)');

    // The toolbar: next body, then pick Saturn from the list.
    const focus0 = (await status()).focus;
    await page.locator('#next').click();
    check((await status()).focus !== focus0, 'the toolbar\'s next button changes the focus');
    const saturn = await page.evaluate(() => {
      for (let i = 0; i < window.orrery.count(); i++) if (window.orrery.name(i) === 'Saturn') return i;
      return -1;
    });
    await page.locator('#body').selectOption(String(saturn));
    await page.waitForTimeout(500);
    const s = await status();
    check(s.focusName === 'Saturn', 'the body list focuses Saturn');
    await shot(page, 'saturn');
    await page.evaluate(() => window.orrery.hold(1));
    await page.waitForTimeout(200);
    await page.screenshot({ path: path.join(out, 'page.png') });
    await page.evaluate(() => window.orrery.hold(0));

    check(errors.length === 0, 'no page errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
    console.log('smoke test passed (' + (phone ? 'phone' : 'desktop') + ')');
  } catch (e) {
    console.error(String(e && e.stack || e));
    if (errors.length) console.error('page errors: ' + errors.join(' | '));
    await page.screenshot({ path: path.join(out, 'failure.png') }).catch(() => {});
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();
