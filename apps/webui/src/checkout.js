// Razorpay Standard Checkout for /checkout.html.
// The server creates the order and verifies the signature; this file never
// sees KEY_SECRET. Every message goes through textContent, never innerHTML.
(() => {
  'use strict';

  const MIN_PAISE = 100;
  const form = document.getElementById('checkout-form');
  const amountInput = document.getElementById('amount-input');
  const payBtn = document.getElementById('pay-btn');
  const statusEl = document.getElementById('status');

  let keyId = null;

  function setStatus(message, kind) {
    statusEl.textContent = message;
    statusEl.dataset.kind = kind || '';
  }

  function setBusy(busy) {
    payBtn.disabled = busy;
  }

  async function requestJson(method, url, payload) {
    const options = { method, credentials: 'same-origin', headers: {} };
    if (payload !== undefined) {
      options.headers['Content-Type'] = 'application/json';
      options.body = JSON.stringify(payload);
    }
    const res = await fetch(url, options);
    let data = {};
    try {
      data = await res.json();
    } catch (_) {
      // Non-JSON body: fall back to the status code below.
    }
    if (!res.ok) {
      throw new Error(data.error || `Request failed (HTTP ${res.status})`);
    }
    return data;
  }

  async function loadKeyId() {
    if (keyId) return keyId;
    const config = await requestJson('GET', '/api/payments/config');
    keyId = config.key_id;
    return keyId;
  }

  async function verifyPayment(response) {
    setStatus('Verifying payment…', 'info');
    try {
      const result = await requestJson('POST', '/api/verify-payment', {
        razorpay_order_id: response.razorpay_order_id,
        razorpay_payment_id: response.razorpay_payment_id,
        razorpay_signature: response.razorpay_signature,
      });
      setStatus(`Payment successful. Payment ID: ${result.payment_id}`, 'success');
    } catch (err) {
      setStatus(`Payment could not be verified: ${err.message}`, 'error');
    } finally {
      setBusy(false);
    }
  }

  function openModal(key, order) {
    let failureMessage = null;
    let settled = false;

    const rzp = new window.Razorpay({
      key,
      amount: order.amount,
      currency: order.currency,
      order_id: order.order_id,
      name: 'QCode',
      description: 'Payment',
      handler(response) {
        settled = true;
        verifyPayment(response);
      },
      modal: {
        ondismiss() {
          if (settled) return;
          setBusy(false);
          setStatus(failureMessage || 'Payment cancelled. You can try again.', failureMessage ? 'error' : 'warning');
        },
      },
    });

    rzp.on('payment.failed', (event) => {
      const error = (event && event.error) || {};
      failureMessage = `Payment failed: ${error.description || 'unknown error'}` +
        (error.code ? ` (code ${error.code})` : '');
      setStatus(failureMessage, 'error');
    });

    rzp.open();
  }

  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    if (payBtn.disabled) return;

    const rupees = Number(amountInput.value);
    const paise = Math.round(rupees * 100);
    if (!Number.isFinite(rupees) || paise < MIN_PAISE) {
      setStatus('The minimum amount is \u20B91.00 (100 paise).', 'error');
      return;
    }

    setBusy(true);
    setStatus('Creating order…', 'info');
    try {
      if (typeof window.Razorpay !== 'function') {
        throw new Error('Razorpay checkout did not load. Check your connection and reload.');
      }
      const key = await loadKeyId();
      const order = await requestJson('POST', '/api/create-order', {
        amount: paise,
        currency: 'INR',
        receipt: `web_${Date.now()}`,
      });
      openModal(key, order);
      setStatus('', '');
    } catch (err) {
      setStatus(`Could not start payment: ${err.message}`, 'error');
      setBusy(false);
    }
  });
})();
