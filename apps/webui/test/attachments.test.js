import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const SRC = path.join(__dirname, '..', 'src');

import { classifyAttachment, attachmentSizeError, attachmentCountError, stripDataUrlPrefix, buildFileReferenceLines, appendFileReferences, IMAGE_MAX_BYTES, FILE_MAX_BYTES, MAX_ATTACHMENTS_PER_MESSAGE, parseUserContent, historyAttachmentMeta, HISTORY_PREVIEW_MAX_B64 } from '../src/utils.js';

test('classifyAttachment: png/jpeg/webp/gif are images; svg and others are files', () => {
  for (const type of ['image/png', 'image/jpeg', 'image/webp', 'image/gif', 'IMAGE/PNG']) {
    assert.equal(classifyAttachment({ name: 'a', type }), 'image', type);
  }
  for (const type of ['image/svg+xml', 'image/bmp', 'application/pdf', 'text/plain', '']) {
    assert.equal(classifyAttachment({ name: 'a', type }), 'file', type);
  }
  assert.equal(classifyAttachment({ name: 'noType' }), 'file');
  assert.equal(classifyAttachment(null), 'file');
});

test('attachmentSizeError: images cap at 10 MiB, files at 25 MiB', () => {
  assert.equal(IMAGE_MAX_BYTES, 10 * 1024 * 1024);
  assert.equal(FILE_MAX_BYTES, 25 * 1024 * 1024);
  assert.equal(attachmentSizeError('image', IMAGE_MAX_BYTES, 'a.png'), '');
  assert.equal(attachmentSizeError('image', 0, 'a.png'), '');
  assert.match(attachmentSizeError('image', IMAGE_MAX_BYTES + 1, 'big.png'), /Image "big.png" is too large \(max 10\.0 MB\)/);
  assert.equal(attachmentSizeError('file', 20 * 1024 * 1024, 'doc.pdf'), '');
  assert.match(attachmentSizeError('file', FILE_MAX_BYTES + 1, 'doc.pdf'), /File "doc.pdf" is too large \(max 25\.0 MB\)/);
  assert.match(attachmentSizeError('file', FILE_MAX_BYTES + 1, ''), /"file"/);
});

test('attachmentCountError: at most 10 attachments per message', () => {
  assert.equal(MAX_ATTACHMENTS_PER_MESSAGE, 10);
  assert.equal(attachmentCountError(9), '');
  assert.match(attachmentCountError(10), /At most 10 attachments/);
  assert.match(attachmentCountError(11), /At most 10 attachments/);
});

test('stripDataUrlPrefix: returns only the base64 payload', () => {
  assert.equal(stripDataUrlPrefix('data:image/png;base64,iVBORw0KGgo='), 'iVBORw0KGgo=');
  assert.equal(stripDataUrlPrefix('data:image/png;base64,'), '');
  assert.equal(stripDataUrlPrefix('no-comma-here'), '');
  assert.equal(stripDataUrlPrefix(undefined), '');
});

test('buildFileReferenceLines: one line per ready file with a human size', () => {
  const lines = buildFileReferenceLines([
    { path: 'uploads/report.pdf', mime_type: 'application/pdf', size: 12595 },
    { path: 'uploads/data.bin', size: 512 },
    { size: 10 },
    null,
  ]);
  assert.deepEqual(lines, [
    'Attached file: uploads/report.pdf (application/pdf, 12.3 KB)',
    'Attached file: uploads/data.bin (application/octet-stream, 512 B)',
  ]);
  assert.deepEqual(buildFileReferenceLines(undefined), []);
});

test('appendFileReferences: blank line between text and references; no files = text unchanged', () => {
  const files = [{ path: 'uploads/a.txt', mime_type: 'text/plain', size: 2048 }];
  assert.equal(appendFileReferences('look at this', files),
    'look at this\n\nAttached file: uploads/a.txt (text/plain, 2.0 KB)');
  assert.equal(appendFileReferences('', files), 'Attached file: uploads/a.txt (text/plain, 2.0 KB)');
  assert.equal(appendFileReferences('hello', []), 'hello');
  assert.equal(appendFileReferences(null, []), '');
});

test('app.js wires attachments into generate, upload and the composer', () => {
  const app = fs.readFileSync(path.join(SRC, 'app.js'), 'utf8');
  const html = fs.readFileSync(path.join(SRC, 'index.html'), 'utf8');
  // Upload uses the raw-body route; images never go to it.
  assert.match(app, /\/upload\?name=' \+ encodeURIComponent\(att\.name\)/);
  assert.match(app, /\.\.\.\(images && images\.length \? \{ attachments: images \} : \{\}\)/);
  assert.match(app, /Wait for the current reply before sending images/);
  assert.match(app, /Wait for attachments to finish uploading/);
  assert.match(app, /revokeObjectURL/);
  for (const id of ['attach-btn', 'attach-input', 'attachment-tray']) {
    assert.match(html, new RegExp('id="' + id + '"'), id);
    assert.match(app, new RegExp("getElementById\\('" + id + "'\\)"), id);
  }
  // The attach input is a multi-select file picker.
  assert.match(html, /<input type="file" id="attach-input" multiple/);
});

test('parseUserContent: plain text and brace-heavy text pass through unchanged', () => {
  assert.deepEqual(parseUserContent('hello world'), { text: 'hello world', images: [] });
  assert.deepEqual(parseUserContent(''), { text: '', images: [] });
  const jsonish = '{"a": 1, "images": "not an envelope"}';
  assert.deepEqual(parseUserContent(jsonish), { text: jsonish, images: [] });
  const malformed = '{"text": "x", "images": [';
  assert.deepEqual(parseUserContent(malformed), { text: malformed, images: [] });
  const notEnvelope = '{"images": [{"mime_type":"image/png","data":"AA=="}]}';
  assert.deepEqual(parseUserContent(notEnvelope), { text: notEnvelope, images: [] });
});

test('parseUserContent: valid envelope yields text and image entries', () => {
  const raw = JSON.stringify({ text: 'look', images: [
    { mime_type: 'image/png', data: 'iVBORw0=', description: 'shot.png' },
    { mime_type: 'IMAGE/JPEG', data: '/9j/', description: 'photo.jpg' },
  ] });
  assert.deepEqual(parseUserContent(raw), {
    text: 'look',
    images: [
      { mime_type: 'image/png', data: 'iVBORw0=', description: 'shot.png' },
      { mime_type: 'IMAGE/JPEG', data: '/9j/', description: 'photo.jpg' },
    ],
  });
});

test('parseUserContent: empty text envelope keeps images; missing description becomes empty', () => {
  const raw = JSON.stringify({ text: '', images: [{ mime_type: 'image/gif', data: 'R0lG' }] });
  assert.deepEqual(parseUserContent(raw), {
    text: '',
    images: [{ mime_type: 'image/gif', data: 'R0lG', description: '' }],
  });
});

test('parseUserContent: non-image mime types and malformed entries are dropped', () => {
  const raw = JSON.stringify({ text: 't', images: [
    { mime_type: 'application/pdf', data: 'JVBER' },
    { mime_type: 'text/plain', data: 'aGk=' },
    { mime_type: 'image/png' },
    { mime_type: 5, data: 'AA==' },
    null,
    'image/png',
    42,
    { mime_type: 'image/webp', data: 'UklG', description: 'ok.webp' },
  ] });
  assert.deepEqual(parseUserContent(raw), {
    text: 't',
    images: [{ mime_type: 'image/webp', data: 'UklG', description: 'ok.webp' }],
  });
});

test('parseUserContent: non-string input is empty and never throws', () => {
  for (const v of [null, undefined, 0, 7, {}, [], true]) {
    assert.deepEqual(parseUserContent(v), { text: '', images: [] });
  }
});

test('parseMessages: user rows go through parseUserContent and historyAttachmentMeta', () => {
  const app = fs.readFileSync(path.join(SRC, 'app.js'), 'utf8');
  const utils = fs.readFileSync(path.join(SRC, 'utils.js'), 'utf8');
  assert.match(app, /parseUserContent\(m\.content\)/);
  assert.match(app, /historyAttachmentMeta\(parsed\.images\)/);
  assert.match(utils, /previewUrl = 'data:' \+ im\.mime_type \+ ';base64,' \+ im\.data/);
});

test('historyAttachmentMeta: small images become previews, large ones a chip', () => {
  const small = historyAttachmentMeta([{ mime_type: 'image/png', data: 'QUJD', description: 'a.png' }]);
  assert.equal(small.length, 1);
  assert.equal(small[0].previewUrl, 'data:image/png;base64,QUJD');
  assert.equal(small[0].name, 'a.png');
  const big = historyAttachmentMeta([{ mime_type: 'image/png', data: 'x'.repeat(HISTORY_PREVIEW_MAX_B64 + 1) }]);
  assert.equal(big.length, 1);
  assert.equal(big[0].previewUrl, undefined);
  assert.equal(big[0].name, 'image');
  assert.deepEqual(historyAttachmentMeta([null, {}, { mime_type: 'image/png' }]), []);
});
