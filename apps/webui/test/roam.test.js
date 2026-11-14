import test from 'node:test';
import assert from 'node:assert/strict';
import { roamModeFor, roamStateFor, roamDelayMs, pickRoamAct, pickRoamSpot, planRoam, actPoolFor, ROAM_MARGIN, ROAM_MODES, ROAM_POOLS, THEME_POOLS } from '../src/roam.js';
import { HERO_ACTS } from '../src/hero.js';

const running = [{ status: 'running', tool_name: 'grep' }];
const box = { width: 600, height: 400, mascotWidth: 160, mascotHeight: 150 };

test('mode: running tool wins, generation is think, otherwise idle', () => {
  assert.equal(roamModeFor({ generating: true, toolEvents: running }), 'tools');
  assert.equal(roamModeFor({ generating: true, toolEvents: [{ status: 'success' }] }), 'think');
  assert.equal(roamModeFor({ generating: false, toolEvents: running }), 'tools');
  assert.equal(roamModeFor({}), 'idle');
  assert.equal(roamModeFor({ generating: false, toolEvents: null }), 'idle');
});

test('mode: last tool outcome sets success or error after a turn; generation still wins', () => {
  assert.equal(roamModeFor({ toolEvents: [{ status: 'success' }] }), 'success');
  assert.equal(roamModeFor({ toolEvents: [{ status: 'success' }, { status: 'error' }] }), 'error');
  assert.equal(roamModeFor({ toolEvents: [{ status: 'error' }, { status: 'success' }] }), 'success');
  assert.equal(roamModeFor({ generating: true, toolEvents: [{ status: 'error' }] }), 'think');
  assert.equal(roamModeFor({ toolEvents: [null, { status: 'success' }] }), 'success');
  assert.deepEqual(ROAM_MODES, ['idle', 'think', 'tools', 'success', 'error']);
});

test('state per mode: tools use the tool pose, think is thinking, idle is chilling', () => {
  assert.equal(roamStateFor({ mode: 'tools', toolEvents: [{ status: 'running', tool_name: 'read' }] }).pose, 'computer');
  assert.equal(roamStateFor({ mode: 'think' }).pose, 'think');
  assert.equal(roamStateFor({ mode: 'idle' }).pose, 'think');
  assert.equal(roamStateFor({ mode: 'idle' }).motion, 'sway');
  assert.equal(roamStateFor({ mode: 'bogus' }).motion, 'sway', 'unknown mode falls back to idle');
});

test('caption is stable for a seed and differs across modes', () => {
  const a = roamStateFor({ mode: 'think', seed: 's1' }).caption;
  assert.equal(a, roamStateFor({ mode: 'think', seed: 's1' }).caption);
  assert.equal(typeof roamStateFor({ mode: 'tools', toolEvents: running, seed: 's1' }).caption, 'string');
});

test('delay ranges: busy chats move less often than idle ones', () => {
  assert.ok(roamDelayMs(() => 0, 'idle') >= 6000 && roamDelayMs(() => 0.999, 'idle') <= 11000);
  assert.ok(roamDelayMs(() => 0, 'think') >= 9000 && roamDelayMs(() => 0.999, 'tools') <= 16000);
});

test('acts: come from HERO_ACTS and never repeat the previous act', () => {
  for (let i = 0; i < 200; i++) {
    const prev = HERO_ACTS[i % HERO_ACTS.length];
    const next = pickRoamAct(Math.random, prev);
    assert.ok(HERO_ACTS.includes(next));
    assert.notEqual(next, prev);
  }
  assert.ok(HERO_ACTS.includes(pickRoamAct(() => 0.9999)));
});

test('spot stays inside the chat box, past the margin', () => {
  for (let i = 0; i < 300; i++) {
    const s = pickRoamSpot(Math.random, box, null);
    assert.ok(s.left >= ROAM_MARGIN && s.left <= box.width - box.mascotWidth - ROAM_MARGIN);
    assert.ok(s.top >= ROAM_MARGIN && s.top <= box.height - box.mascotHeight - ROAM_MARGIN);
  }
});

test('spot moves away from the previous one when the box allows it', () => {
  const prev = { left: 10, top: 10 };
  const s = pickRoamSpot(() => 0.01, box, prev); // would land on the same corner
  const diag = Math.hypot(box.width - 160 - 16, box.height - 150 - 16);
  assert.ok(Math.hypot(s.left - prev.left, s.top - prev.top) >= 0.3 * diag || s.left !== prev.left || s.top !== prev.top);
});

test('a tiny chat box clamps to the margin instead of going negative', () => {
  const s = pickRoamSpot(Math.random, { width: 100, height: 80, mascotWidth: 160, mascotHeight: 150 }, null);
  assert.deepEqual(s, { left: ROAM_MARGIN, top: ROAM_MARGIN });
});

test('pools: every act named in a pool exists in HERO_ACTS', () => {
  for (const pool of [...Object.values(ROAM_POOLS), ...Object.values(THEME_POOLS)]) {
    for (const a of pool) assert.ok(HERO_ACTS.includes(a), a + ' is not an act');
  }
});

test('pools: busy modes use their own pool, idle uses the theme pool, unknown theme uses all acts', () => {
  assert.deepEqual(actPoolFor('think', 'spiddy'), ROAM_POOLS.think);
  assert.deepEqual(actPoolFor('tools', 'spiddy-night'), ROAM_POOLS.tools);
  assert.deepEqual(actPoolFor('idle', 'spiddy-jade'), THEME_POOLS['spiddy-jade']);
  assert.deepEqual(actPoolFor('idle', 'no-such-theme'), HERO_ACTS);
});

test('planRoam: every mode gets an act from its pool; reduced motion stays put', () => {
  for (const mode of ROAM_MODES) {
    for (let i = 0; i < 50; i++) {
      const plan = planRoam({ rand: Math.random, mode, theme: 'spiddy-steel', box });
      assert.ok(actPoolFor(mode, 'spiddy-steel').includes(plan.act), mode + ' gave ' + plan.act);
    }
  }
  const prev = { left: 40, top: 50 };
  const still = planRoam({ rand: Math.random, mode: 'idle', theme: 'spiddy', box, prev, reduced: true });
  assert.deepEqual(still.spot, prev);
  assert.equal(still.act, null);
  assert.equal(planRoam({ rand: Math.random, mode: 'tools', box, reduced: true }).act, null);
});

test('planRoam: the act does not repeat the previous one within a pool', () => {
  const pool = THEME_POOLS['spiddy-brick'];
  for (let i = 0; i < 100; i++) {
    const prevAct = pool[i % pool.length];
    const plan = planRoam({ rand: Math.random, mode: 'idle', theme: 'spiddy-brick', box, prevAct });
    assert.notEqual(plan.act, prevAct);
  }
});

test('planRoam: an explicit pool overrides the mode/theme pools (3D heroes)', () => {
  const pool = ['think', 'search', 'celebrate', 'facepalm'];
  for (let i = 0; i < 100; i++) {
    const plan = planRoam({ rand: Math.random, mode: 'idle', theme: 'spiddy', box, pool });
    assert.ok(pool.includes(plan.act), 'act ' + plan.act + ' came from the explicit pool');
  }
  const empty = planRoam({ rand: Math.random, mode: 'ide', theme: 'spiddy', box, pool: [] });
  assert.equal(empty.act, null);
});
