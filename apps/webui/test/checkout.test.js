import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const source = fs.readFileSync(path.join(__dirname, '../src/checkout.js'), 'utf8');

const reply = (status, data) => ({ ok: status >= 200 && status < 300, status, json: async () => data });
const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

// Runs checkout.js against a stub DOM, a stub fetch and a fake Razorpay class.
function setup({ responses = [], razorpayLoaded = true } = {}) {
  const elements = {
    'checkout-form': { listeners: {}, addEventListener(type, fn) { this.listeners[type] = fn; } },
    'amount-input': { value: '1.00' },
    'pay-btn': { disabled: false },
    status: { textContent: '', dataset: {} },
  };
  const modals = [];
  class FakeRazorpay {
    constructor(options) { this.options = options; this.handlers = {}; this.opened = false; modals.push(this); }
    on(event, fn) { this.handlers[event] = fn; }
    open() { this.opened = true; }
  }
  const calls = [];
  const queue = [...responses];
  const fetch = async (url, options = {}) => {
    calls.push({ url, method: options.method || 'GET', body: options.body ? JSON.parse(options.body) : undefined });
    return queue.shift();
  };
  const window = { Razorpay: razorpayLoaded ? FakeRazorpay : undefined };
  vm.runInNewContext(source, { window, document: { getElementById: (id) => elements[id] }, fetch, Date });
  const submit = () => elements['checkout-form'].listeners.submit({ preventDefault() {} });
  return { elements, calls, modals, submit, status: () => elements.status };
}

test('checkout: amount below 100 paise is rejected before any request', async () => {
  const env = setup();
  env.elements['amount-input'].value = '0.50';
  await env.submit();
  assert.equal(env.calls.length, 0);
  assert.match(env.status().textContent, /minimum/i);
  assert.equal(env.status().dataset.kind, 'error');
});

test('checkout: create order, open modal, verify on success', async () => {
  const env = setup({
    responses: [
      reply(200, { key_id: 'rzp_test_unit' }),
      reply(200, { order_id: 'order_1', amount: 100, currency: 'INR' }),
      reply(200, { status: 'verified', order_id: 'order_1', payment_id: 'pay_1' }),
    ],
  });
  await env.submit();

  assert.equal(env.calls[0].url, '/api/payments/config');
  assert.equal(env.calls[1].url, '/api/create-order');
  assert.equal(env.calls[1].body.amount, 100);
  assert.equal(env.calls[1].body.currency, 'INR');
  assert.match(env.calls[1].body.receipt, /^web_\d+$/);

  const modal = env.modals[0];
  assert.equal(modal.opened, true);
  assert.equal(modal.options.key, 'rzp_test_unit');
  assert.equal(modal.options.order_id, 'order_1');
  assert.equal(modal.options.amount, 100);

  modal.options.handler({ razorpay_order_id: 'order_1', razorpay_payment_id: 'pay_1', razorpay_signature: 'sig' });
  await flush();

  assert.equal(env.calls[2].url, '/api/verify-payment');
  assert.deepEqual(env.calls[2].body, {
    razorpay_order_id: 'order_1', razorpay_payment_id: 'pay_1', razorpay_signature: 'sig',
  });
  assert.match(env.status().textContent, /Payment successful.*pay_1/);
  assert.equal(env.status().dataset.kind, 'success');
  assert.equal(env.elements['pay-btn'].disabled, false);
});

test('checkout: a rejected signature is shown as an error, never as paid', async () => {
  const env = setup({
    responses: [
      reply(200, { key_id: 'rzp_test_unit' }),
      reply(200, { order_id: 'order_2', amount: 100, currency: 'INR' }),
      reply(400, { error: 'payment signature verification failed' }),
    ],
  });
  await env.submit();
  env.modals[0].options.handler({ razorpay_order_id: 'order_2', razorpay_payment_id: 'pay_2', razorpay_signature: 'bad' });
  await flush();
  assert.match(env.status().textContent, /could not be verified/);
  assert.doesNotMatch(env.status().textContent, /successful/);
  assert.equal(env.status().dataset.kind, 'error');
});

test('checkout: closing the modal reports cancellation and re-enables the button', async () => {
  const env = setup({
    responses: [reply(200, { key_id: 'rzp_test_unit' }), reply(200, { order_id: 'order_3', amount: 100, currency: 'INR' })],
  });
  await env.submit();
  assert.equal(env.elements['pay-btn'].disabled, true);
  env.modals[0].options.modal.ondismiss();
  assert.match(env.status().textContent, /cancelled/i);
  assert.equal(env.status().dataset.kind, 'warning');
  assert.equal(env.elements['pay-btn'].disabled, false);
});

test('checkout: payment.failed shows the reason and survives a later dismiss', async () => {
  const env = setup({
    responses: [reply(200, { key_id: 'rzp_test_unit' }), reply(200, { order_id: 'order_4', amount: 100, currency: 'INR' })],
  });
  await env.submit();
  const modal = env.modals[0];
  modal.handlers['payment.failed']({ error: { code: 'BAD_REQUEST_ERROR', description: 'Card declined' } });
  assert.match(env.status().textContent, /Card declined/);
  assert.equal(env.status().dataset.kind, 'error');
  modal.options.modal.ondismiss();
  assert.match(env.status().textContent, /Card declined/);
  assert.equal(env.status().dataset.kind, 'error');
});

test('checkout: server auth failure on create-order surfaces the message and opens no modal', async () => {
  const env = setup({
    responses: [
      reply(200, { key_id: 'rzp_test_unit' }),
      reply(401, { error: 'Razorpay authentication failed: check RAZORPAY_KEY_ID and RAZORPAY_KEY_SECRET' }),
    ],
  });
  await env.submit();
  assert.equal(env.modals.length, 0);
  assert.match(env.status().textContent, /Could not start payment: Razorpay authentication failed/);
  assert.equal(env.elements['pay-btn'].disabled, false);
});

test('checkout: a missing Razorpay script is reported before any order is created', async () => {
  const env = setup({ razorpayLoaded: false });
  await env.submit();
  assert.equal(env.calls.length, 0);
  assert.match(env.status().textContent, /did not load/);
});
