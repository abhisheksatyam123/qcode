import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const webuiRoot = path.resolve(__dirname, '..');
const srcDir = path.join(webuiRoot, 'src');

test('WebUI static source files exist and are populated', () => {
  const requiredFiles = [
    'index.html',
    'app.js',
    'style.css',
    'vendor-marked.min.js',
    'vendor-jsyaml.min.js'
  ];

  for (const file of requiredFiles) {
    const filePath = path.join(srcDir, file);
    assert.ok(fs.existsSync(filePath), `Missing expected file: ${file}`);
    const stats = fs.statSync(filePath);
    assert.ok(stats.size > 0, `File is empty: ${file}`);
  }
});

test('index.html references all required scripts and styles', () => {
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');

  // Verify stylesheet links
  assert.match(indexHtml, /href="\/style\.css"/, 'Missing style.css link');

  // Verify scripts
  assert.match(indexHtml, /src="\/vendor-marked\.min\.js"/, 'Missing vendor-marked.min.js');
  assert.match(indexHtml, /src="\/vendor-jsyaml\.min\.js"/, 'Missing vendor-jsyaml.min.js');
  assert.match(indexHtml, /src="\/app\.js"/, 'Missing app.js module');

  // Verify brand and status elements
  assert.match(indexHtml, /id="brand-sub"/, 'Missing #brand-sub element for version');
  assert.match(indexHtml, /id="connection-status"/, 'Missing #connection-status element');
});

test('Critical DOM element IDs referenced in app.js exist in index.html', () => {
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');

  // Find all getElementById calls
  const matches = [...appJs.matchAll(/document\.getElementById\(['"]([^'"]+)['"]\)/g)];
  assert.ok(matches.length > 0, 'No getElementById calls found');

  const missing = [];
  for (const match of matches) {
    const id = match[1];
    // Some IDs may be created dynamically in modals, but check the rest against index.html
    const idPattern = new RegExp(`id=["']${id}["']`);
    if (!idPattern.test(indexHtml) && !appJs.includes(`id="${id}"`)) {
      missing.push(id);
    }
  }

  assert.deepEqual(missing, [], `The following IDs used in app.js were missing from index.html: ${missing.join(', ')}`);
});

test('WebUI fetch endpoints conform to documented server-api.md', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const serverApiDoc = fs.readFileSync(path.resolve(webuiRoot, '../../docs/server-api.md'), 'utf8');

  // Extract endpoints from fetch(...) calls
  const fetchPatterns = [
    /fetch\(['"`](\/[^'"`?]+)/g,
    /fetch\(\s*['"](\/[^'"]+)/g
  ];

  const staticPrefixes = [
    '/api/version',
    '/api/health',
    '/health',
    '/providers',
    '/sessions',
    '/session/last',
    '/session/cancel',
    '/rename',
    '/terminal/create'
  ];

  for (const prefix of staticPrefixes) {
    assert.ok(serverApiDoc.includes(prefix), `server-api.md should document ${prefix}`);
  }

  // Ensure resize is documented
  assert.ok(serverApiDoc.includes('/terminal/:id/resize'), 'server-api.md must document /terminal/:id/resize');
});
