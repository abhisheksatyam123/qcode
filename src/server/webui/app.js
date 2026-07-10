// ── QCode Web UI ──────────────────────────────────────────────────
const state = {
  provider: '',
  model: '',
  reasoning: 'off',
  messages: [],
  generating: false,
  providers: []
};

// ── DOM refs ──
const messagesEl = document.getElementById('messages');
const promptInput = document.getElementById('prompt-input');
const sendBtn = document.getElementById('send-btn');
const clearBtn = document.getElementById('clear-btn');
const providerSelect = document.getElementById('provider-select');
const modelSelect = document.getElementById('model-select');
const reasoningSelect = document.getElementById('reasoning-select');

// ── Init ──
async function init() {
  await loadProviders();
  setupEventListeners();
}
init();

async function loadProviders() {
  try {
    const res = await fetch('/providers');
    state.providers = await res.json();
    if (state.providers.length === 0) {
      showToast('No providers configured on server');
      return;
    }
    // Populate provider select
    providerSelect.innerHTML = state.providers.map((p, i) =>
      `<option value="${i}">${p.name}</option>`
    ).join('');
    onProviderChange(0);
    providerSelect.addEventListener('change', () => {
      onProviderChange(parseInt(providerSelect.value));
    });
  } catch (e) {
    showToast('Failed to load providers: ' + e.message);
  }
}

function onProviderChange(idx) {
  const p = state.providers[idx];
  if (!p) return;
  state.provider = p.id;
  modelSelect.innerHTML = p.models.map(m =>
    `<option value="${m.id}">${m.name}</option>`
  ).join('');
  if (p.models.length > 0) state.model = p.models[0].id;
  modelSelect.addEventListener('change', () => {
    state.model = modelSelect.value;
  });
}

function setupEventListeners() {
  sendBtn.addEventListener('click', sendMessage);
  promptInput.addEventListener('keydown', e => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      sendMessage();
    }
  });
  clearBtn.addEventListener('click', () => {
    state.messages = [];
    renderMessages();
  });
  reasoningSelect.addEventListener('change', () => {
    state.reasoning = reasoningSelect.value;
  });
}

// ── Send Message ──
async function sendMessage() {
  const text = promptInput.value.trim();
  if (!text || state.generating) return;

  // Add user message
  addMessage('user', text);
  state.messages.push({ role: 'user', content: text });
  promptInput.value = '';
  state.generating = true;
  setGenerating(true);

  // Add placeholder assistant message (will be updated during streaming)
  const assistantMsg = { role: 'assistant', content: '', toolEvents: [] };
  state.messages.push(assistantMsg);
  const msgIdx = state.messages.length - 1;
  renderMessages();
  scrollToBottom();

  try {
    const body = JSON.stringify({
      text,
      provider: state.provider,
      model: state.model,
      reasonong_mode: state.reasoning
    });

    const res = await fetch('/generate', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body
    });

    if (!res.ok) {
      const err = await res.text();
      throw new Error(err);
    }

    // Read NDJSON stream
    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buffer = '';

    while (true) {
      const { done, value } = await reader.read();
      if (done) break;

      buffer += decoder.decode(value, { stream: true });
      const lines = buffer.split('\n');
      buffer = lines.pop() || '';

      for (const line of lines) {
        if (!line.trim()) continue;
        try {
          const evt = JSON.parse(line);
          handleEvent(evt, assistantMsg, msgIdx);
        } catch (e) {
          console.warn('Failed to parse event:', line);
        }
      }
    }

  } catch (e) {
    showToast('Error: ' + e.message);
    assistantMsg.content = 'Error: ' + e.message;
  }

  state.generating = false;
  setGenerating(false);
  renderMessages();
  scrollToBottom();
}

function handleEvent(evt, msg, msgIdx) {
  switch (evt.type) {
    case 'session.started':
      msg.sessionId = evt.session_id;
      break;

    case 'backend.message.delta':
      msg.content = evt.text;
      renderMessages();
      scrollToBottom();
      break;

    case 'backend.reasoning.delta':
      // Store reasoning but don't display in main content
      if (!msg.reasoning) msg.reasoning = '';
      msg.reasoning = evt.text;
      break;

    case 'backend.tool.call.started':
      if (!msg.toolEvents) msg.toolEvents = [];
      msg.toolEvents.push({
        type: 'tool_call',
        tool_call_id: evt.tool_call_id,
        tool_name: evt.tool_name,
        arguments: evt.arguments,
        status: 'running'
      });
      renderMessages();
      scrollToBottom();
      break;

    case 'backend.tool.call.completed':
      if (msg.toolEvents) {
        const tc = msg.toolEvents.find(t => t.tool_call_id === evt.tool_call_id);
        if (tc) {
          tc.status = evt.is_error ? 'error' : 'success';
          tc.result = evt.result;
          tc.duration_ms = evt.duration_ms;
        }
      }
      renderMessages();
      scrollToBottom();
      break;

    case 'backend.session.status.changed':
      // Status updates (idle/generating) - could show a status indicator
      break;

    case 'backend.error.occurred':
      showToast(evt.message);
      break;

    case 'generation.complete':
      // Final event, nothing special needed
      break;
  }
}

// ── Rendering ──
function renderMessages() {
  messagesEl.innerHTML = '';
  for (const msg of state.messages) {
    const el = renderMessage(msg);
    messagesEl.appendChild(el);
  }
}

function renderMessage(msg) {
  const div = document.createElement('div');
  div.className = `message ${msg.role}`;

  const header = document.createElement('div');
  header.className = 'message-header';
  const icons = { user: '👤', assistant: '🤖', system: 'ℹ️', tool: '🔧' };
  header.innerHTML = `<span class="role-icon">${icons[msg.role] || ''}</span> ${capitalize(msg.role)}`;
  div.appendChild(header);

  const content = document.createElement('div');
  content.className = 'message-content';

  // Tool events
  if (msg.toolEvents && msg.toolEvents.length > 0) {
    const toolContainer = document.createElement('div');
    toolContainer.className = 'tool-events';
    for (const tc of msg.toolEvents) {
      toolContainer.appendChild(renderToolBlock(tc));
    }
    content.appendChild(toolContainer);
  }

  // Text content
  if (msg.content) {
    const textEl = document.createElement('div');
    textEl.textContent = msg.content;
    if (state.generating && msg === state.messages[state.messages.length - 1]) {
      textEl.className = 'streaming-cursor';
    }
    content.appendChild(textEl);
  }

  div.appendChild(content);
  return div;
}

function renderToolBlock(tc) {
  const icons = { bash: '⚡', task: '🤖', read_file: '📄', write_file: '📝',
                  view_file: '📄', edit_file: '✏️', search: '🔍', grep: '🔍', ripgrep: '🔍' };
  const icon = icons[tc.tool_name] || '🔧';
  const statusIcons = { running: '⏳', success: '✅', error: '❌' };

  const block = document.createElement('div');
  block.className = 'tool-block';

  const header = document.createElement('div');
  header.className = 'tool-header';

  const chevron = document.createElement('span');
  chevron.className = 'tool-chevron';
  chevron.textContent = '▼';

  header.innerHTML = `
    <span class="tool-icon">${icon}</span>
    <span class="tool-name">${capitalize(tc.tool_name)}</span>
    ${tc.duration_ms ? `<span class="tool-duration">${Math.round(tc.duration_ms)}ms</span>` : ''}
    <span class="tool-status ${tc.status}">${statusIcons[tc.status] || ''} ${tc.status}</span>
  `;
  header.appendChild(chevron);

  const body = document.createElement('div');
  body.className = 'tool-body collapsed';

  // Arguments
  if (tc.arguments) {
    const argsStr = typeof tc.arguments === 'string' ? tc.arguments : JSON.stringify(tc.arguments, null, 2);
    body.innerHTML = `<div class="input-label">Input:</div>${escapeHtml(argsStr)}`;
  }

  // Result
  if (tc.result) {
    const resultStr = typeof tc.result === 'string' ? tc.result : JSON.stringify(tc.result, null, 2);
    body.innerHTML += `\n\n${escapeHtml(resultStr)}`;
  }

  let expanded = false;
  header.addEventListener('click', () => {
    expanded = !expanded;
    body.classList.toggle('collapsed', !expanded);
    chevron.textContent = expanded ? '▲' : '▼';
  });

  block.appendChild(header);
  block.appendChild(body);
  return block;
}

function addMessage(role, content) {
  const div = renderMessage({ role, content });
  messagesEl.appendChild(div);
  scrollToBottom();
}

// ── Helpers ──
function setGenerating(on) {
  sendBtn.disabled = on;
  sendBtn.textContent = on ? 'Generating...' : 'Send';
  promptInput.disabled = on;
}

function scrollToBottom() {
  const container = document.getElementById('chat-container');
  container.scrollTop = container.scrollHeight;
}

function capitalize(s) {
  return s.charAt(0).toUpperCase() + s.slice(1);
}

function escapeHtml(s) {
  const d = document.createElement('div');
  d.textContent = s;
  return d.innerHTML;
}

function showToast(msg) {
  const toast = document.createElement('div');
  toast.className = 'toast';
  toast.textContent = msg;
  document.body.appendChild(toast);
  setTimeout(() => toast.remove(), 5000);
}
