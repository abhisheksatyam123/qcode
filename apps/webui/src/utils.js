// qcode-webui utils — pure functions, no DOM. Imported by app.js; unit-tested directly.

export function fuzzyScore(query, text) {
  query = query.toLowerCase();
  text = text.toLowerCase();
  if (!query) return 0;            // empty query -> keep all items
  let qi = 0, score = 0, prev = -2;
  for (let ti = 0; ti < text.length && qi < query.length; ti++) {
    if (text[ti] === query[qi]) {
      score += 1;
      if (ti === prev + 1) score += 2;                       // consecutive
      const c = text[ti - 1];
      if (ti === 0 || c === ' ' || c === '/' || c === '-' || c === '_' || c === '.') score += 3; // word start
      prev = ti;
      qi++;
    }
  }
  return qi === query.length ? score : -1;  // -1 => not a subsequence
}

export function fuzzyFilter(query, items) {
  if (!query) return items.slice();
  const out = [];
  for (const it of items) {
    const hay = [it.label, it.sublabel || '', it.category || '', it.workspace || ''].join(' ');
    const s = fuzzyScore(query, hay);
    if (s >= 0) out.push({ it, s });
  }
  out.sort((a, b) => b.s - a.s);
  return out.map(x => x.it);
}

export function shortPath(p) {
  if (!p) return '';
  return p.length > 30 ? '…' + p.slice(-28) : p;
}

export function formatBytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
  return (n / (1024 * 1024)).toFixed(1) + ' MB';
}

export function formatMs(ms) {
  ms = Number(ms) || 0;
  if (ms < 1000) return ms.toFixed(0) + ' ms';
  const s = ms / 1000;
  if (s < 60) return s.toFixed(1) + ' s';
  const m = Math.floor(s / 60);
  return m + 'm ' + (s % 60).toFixed(0) + 's';
}

export function formatNumber(n) {
  const num = Number(n) || 0;
  return num.toLocaleString();
}

// USD for session costs: sub-cent precision below $100 ($0.0123).
export function formatUsd(n) {
  const v = Number(n) || 0;
  return '$' + (v >= 100 ? v.toFixed(2) : v.toFixed(4));
}

// Cost lines for the Stats tab from /session/<id>/stats `usage`. Every call
// was priced at its own model's opencode.json rates when it ran, so the
// total survives model switches; legacy sessions carry an estimate flag.
export function usageCostSummary(usage) {
  const u = usage || {};
  const c = u.session_cost || {};
  const calls = Number(u.model_calls) || 0;
  const unpriced = Number(c.unpriced_calls) || 0;
  const legacy = Number(c.legacy_calls) || 0;
  const out = {
    available: !!c.available,
    estimated: !!c.estimated,
    total: Number(c.total) || 0,
    parts: [
      { label: 'Input (uncached)', value: Number(c.input) || 0 },
      { label: 'Cache read', value: Number(c.cache_read) || 0 },
      { label: 'Cache write', value: Number(c.cache_write) || 0 },
      { label: 'Output', value: Number(c.output) || 0 },
    ],
    notes: [],
  };
  if (out.estimated) {
    out.notes.push("Estimate at the current model's price (recorded before per-call pricing).");
  }
  if (unpriced > 0) {
    out.notes.push(unpriced + ' call' + (unpriced === 1 ? '' : 's') +
      ' on models without a price ' + (unpriced === 1 ? 'is' : 'are') + ' not included.');
  }
  if (legacy > 0 && !out.estimated) {
    out.notes.push(legacy + ' earlier call' + (legacy === 1 ? '' : 's') +
      ' predate' + (legacy === 1 ? 's' : '') + ' per-call pricing and ' +
      (legacy === 1 ? 'is' : 'are') + ' not included.');
  }
  if (!out.available && calls > 0 && !unpriced && !legacy) {
    out.notes.push('No price configured: add cost.input/output/cache_read/cache_write to opencode.json.');
  }
  return out;
}

// Per-model rows ("provider/model") from usage.by_model, most expensive first.
export function usageModelRows(usage) {
  const byModel = (usage && usage.by_model) || {};
  return Object.keys(byModel).map((key) => {
    const m = byModel[key] || {};
    const cost = m.cost || {};
    return {
      key,
      calls: Number(m.calls) || 0,
      inputTokens: Number(m.input_tokens) || 0,
      cacheReadTokens: Number(m.cache_read_tokens) || 0,
      outputTokens: Number(m.output_tokens) || 0,
      priced: !!cost.priced,
      cost: Number(cost.total) || 0,
      unpricedCalls: Number(m.unpriced_calls) || 0,
    };
  }).sort((a, b) => (b.cost - a.cost) || (b.calls - a.calls) || a.key.localeCompare(b.key));
}

// Per-call usage from /session/<id>/stats: cost (each call at its own
// model's opencode.json prices), billed input with the prompt-cache split,
// context in use, latency, and the model's configured limits/prices.
export function renderUsageSections(data) {
  const u = data.usage || {};
  const calls = Number(u.model_calls) || 0;
  const mi = data.model_info || null;
  const ctx = data.context || {};
  const cost = usageCostSummary(u);
  const rows = usageModelRows(u);
  const pct = (part, whole) => (whole > 0 ? Math.max(0, Math.min(100, (part / whole) * 100)) : 0);
  // One decimal below 10% so a large window does not read as an empty 0%.
  const pctLabel = (v) => (v > 0 && v < 10 ? v.toFixed(1) : v.toFixed(0)) + '%';
  const detail = (label, value) =>
    `<div class="stat-detail-item"><span class="detail-label">${esc(label)}</span><span class="detail-val">${esc(value)}</span></div>`;

  const input = Number(u.input_tokens) || 0;
  const cacheRead = Number(u.cache_read_tokens) || 0;
  const cacheWrite = Number(u.cache_write_tokens) || 0;
  const uncached = Number(u.uncached_input_tokens) || Math.max(0, input - cacheRead - cacheWrite);
  const output = Number(u.output_tokens) || 0;
  const thinking = Number(u.reasoning_tokens) || 0;
  const hit = Number(u.cache_hit_pct) || 0;
  const tokPerS = Number(u.output_tok_per_s) || 0;
  const ctxWindow = Number(ctx.window) || 0;
  const used = Number(ctx.used) || 0;

  const costValue = cost.available ? formatUsd(cost.total) : '—';
  // Delegated work is billed in child sessions; /stats rolls it up.
  const sub = data.subagents || null;
  const subSessions = sub ? Number(sub.sessions) || 0 : 0;
  const subCost = subSessions > 0 ? usageCostSummary(sub) : null;
  const subLine = subSessions > 0
    ? `<div class="stat-subtext">+ ${esc(subCost.available ? formatUsd(subCost.total) : 'unpriced')} in ${esc(String(subSessions))} subagent session${subSessions === 1 ? '' : 's'}</div>`
    : '';
  const costSub = cost.available
    ? (cost.estimated ? 'estimate · current model price' : 'each call at its model price')
    : (calls > 0 ? 'no price configured' : 'no model calls yet');
  const kpis = `
      <div class="stat-grid stats-kpi-grid">
        <div class="stat-card kpi-card cost">
          <div class="stat-card-header">
            <span class="stat-card-icon">💲</span>
            <span class="stat-label">Session Cost</span>
          </div>
          <div class="stat-value accent">${esc(costValue)}</div>
          <div class="stat-subtext">${esc(costSub)}</div>
          ${subLine}
        </div>
        <div class="stat-card kpi-card calls">
          <div class="stat-card-header">
            <span class="stat-card-icon">⚡</span>
            <span class="stat-label">Model Calls</span>
          </div>
          <div class="stat-value">${esc(formatNumber(calls))}</div>
          <div class="stat-subtext">${calls > 0 ? 'avg ' + esc(formatMs(u.avg_call_ms)) + ' · ' + esc(tokPerS.toFixed(1)) + ' tok/s' : 'none recorded'}</div>
        </div>
        <div class="stat-card kpi-card cache">
          <div class="stat-card-header">
            <span class="stat-card-icon">🗄️</span>
            <span class="stat-label">Prompt Cache Hit</span>
          </div>
          <div class="stat-value">${input > 0 ? esc(hit.toFixed(0)) + '%' : '—'}</div>
          <div class="stat-subtext">${esc(formatNumber(cacheRead))} of ${esc(formatNumber(input))} input tokens</div>
        </div>
        <div class="stat-card kpi-card context">
          <div class="stat-card-header">
            <span class="stat-card-icon">📐</span>
            <span class="stat-label">Context In Use</span>
          </div>
          <div class="stat-value">${ctxWindow > 0 ? esc(pctLabel(pct(used, ctxWindow))) : esc(formatNumber(used))}</div>
          <div class="stat-subtext">${ctxWindow > 0 ? esc(formatNumber(used)) + ' / ' + esc(formatNumber(ctxWindow)) + ' tokens' : 'window unknown: set limit.context'}</div>
        </div>
      </div>`;

  if (calls === 0) {
    return kpis + `
      <div class="stat-card section-card">
        <div class="stats-note">Per-call input, cache and cost figures start with this session's next model call.</div>
      </div>`;
  }

  const usedPct = pct(uncached, input), readPct = pct(cacheRead, input), writePct = pct(cacheWrite, input);
  const tokens = `
        <div class="stat-card section-card">
          <div class="stat-section-header">
            <div class="stat-section-title-wrap">
              <span class="stat-section-icon">🧾</span>
              <span class="stat-section-title">Billed Tokens &amp; Prompt Cache</span>
            </div>
          </div>
          <div class="stat-progress-container">
            <div class="stat-progress-bar" title="Billed input: uncached / cache read / cache write">
              <div class="stat-progress-segment uncached" style="width: ${usedPct}%"></div>
              <div class="stat-progress-segment cache-read" style="width: ${readPct}%"></div>
              <div class="stat-progress-segment cache-write" style="width: ${writePct}%"></div>
            </div>
            <div class="stat-progress-legend">
              <div class="stat-legend-item"><span class="legend-dot uncached"></span><span class="legend-label">Uncached</span></div>
              <div class="stat-legend-item"><span class="legend-dot cache-read"></span><span class="legend-label">Cache read</span></div>
              <div class="stat-legend-item"><span class="legend-dot cache-write"></span><span class="legend-label">Cache write</span></div>
            </div>
          </div>
          <div class="stat-details-list">
            ${detail('Input (billed)', formatNumber(input))}
            ${detail('Uncached', formatNumber(uncached))}
            ${detail('Cache read', formatNumber(cacheRead) + ' (' + hit.toFixed(0) + '% hit)')}
            ${detail('Cache write', formatNumber(cacheWrite))}
            ${detail('Output', formatNumber(output))}
            ${detail('Thinking (in output)', formatNumber(thinking))}
          </div>
        </div>`;

  const costParts = cost.available
    ? cost.parts.map((part) => detail(part.label, formatUsd(part.value))).join('')
    : '';
  const notes = cost.notes.map((n) => `<div class="stats-note">${esc(n)}</div>`).join('');
  const modelTable = rows.length ? `
          <table class="stats-model-table">
            <thead><tr><th>Model</th><th class="num">Calls</th><th class="num">In</th><th class="num">Out</th><th class="num">Cost</th></tr></thead>
            <tbody>
              ${rows.map((r) => `<tr>
                <td class="model">${esc(r.key)}</td>
                <td class="num">${esc(formatNumber(r.calls))}</td>
                <td class="num">${esc(formatNumber(r.inputTokens))}</td>
                <td class="num">${esc(formatNumber(r.outputTokens))}</td>
                <td class="num">${r.priced ? esc(formatUsd(r.cost)) + (r.unpricedCalls ? ' +' + esc(String(r.unpricedCalls)) + ' unpriced' : '') : 'no price'}</td>
              </tr>`).join('')}
            </tbody>
          </table>` : '';
  const costCard = `
        <div class="stat-card section-card">
          <div class="stat-section-header">
            <div class="stat-section-title-wrap">
              <span class="stat-section-icon">💲</span>
              <span class="stat-section-title">Cost (each call at its model's price)</span>
            </div>
            <span class="stat-section-badge">${esc(costValue)}</span>
          </div>
          <div class="stat-details-list">${costParts}</div>
          ${notes}
          ${modelTable}
        </div>`;

  const lastEffort = u.last_effort || 'off';
  const effortLine = u.last_variant && u.last_variant !== lastEffort
    ? u.last_variant + ' → effort ' + lastEffort : lastEffort;
  const latency = `
        <div class="stat-card section-card">
          <div class="stat-section-header">
            <div class="stat-section-title-wrap">
              <span class="stat-section-icon">⏱️</span>
              <span class="stat-section-title">Model Latency</span>
            </div>
          </div>
          <div class="stat-details-list">
            ${detail('Avg call', formatMs(u.avg_call_ms))}
            ${detail('Avg first token', u.avg_ttft_ms == null ? '—' : formatMs(u.avg_ttft_ms))}
            ${detail('Last call', formatMs(u.model_ms_last) + (Number(u.ttft_ms_last) >= 0 ? ' (first token ' + formatMs(u.ttft_ms_last) + ')' : ''))}
            ${detail('Slowest call', formatMs(u.model_ms_max))}
            ${detail('Output speed', tokPerS.toFixed(1) + ' tok/s')}
            ${detail('Total model time', formatMs(u.model_ms_total))}
            ${detail('Last effort sent', effortLine)}
          </div>
        </div>`;

  let config = '';
  if (mi) {
    const c = mi.cost || {};
    const rate = (v) => '$' + (Number(v) || 0).toFixed(Number(v) < 1 ? 3 : 2);
    const prices = (Number(c.input) > 0 || Number(c.output) > 0)
      ? rate(c.input) + ' in · ' + rate(c.output) + ' out' +
        (Number(c.cache_read) > 0 ? ' · ' + rate(c.cache_read) + ' cache read' : '') +
        (Number(c.cache_write) > 0 ? ' · ' + rate(c.cache_write) + ' cache write' : '')
      : 'not set (opencode.json cost)';
    const th = mi.thinking || {};
    const thinkingLine = th.type ? th.type + (th.display ? ' (' + th.display + ')' : '') + (th.allow_off === false ? ' · always on' : '') : 'default';
    config = `
        <div class="stat-card section-card">
          <div class="stat-section-header">
            <div class="stat-section-title-wrap">
              <span class="stat-section-icon">🧩</span>
              <span class="stat-section-title">Model Configuration (opencode.json)</span>
            </div>
          </div>
          <div class="stat-details-list">
            ${detail('Model', mi.name || mi.id || '—')}
            ${detail('Context window', Number(mi.context_window) > 0 ? formatNumber(mi.context_window) + ' tokens' : 'unknown (set limit.context)')}
            ${detail('Output limit', Number(mi.output_limit) > 0 ? formatNumber(mi.output_limit) + ' tokens' : 'not set')}
            ${detail('Default max_tokens', Number(mi.max_tokens) > 0 ? formatNumber(mi.max_tokens) : (Number(mi.output_limit) > 0 ? formatNumber(mi.output_limit) + ' (limit.output)' : 'provider default'))}
            ${detail('Price / 1M tokens', prices)}
            ${detail('Thinking', thinkingLine)}
            ${detail('Variants', (mi.variants || []).join(', ') || '—')}
          </div>
        </div>`;
  }

  return kpis + `
      <div class="stats-two-col-grid">${tokens}${costCard}</div>
      <div class="stats-two-col-grid">${latency}${config}</div>`;
}

export function detectFsLanguage(path) {
  const name = (path || '').split('/').pop() || '';
  const lower = name.toLowerCase();
  if (lower === 'cmakelists.txt' || lower.endsWith('.cmake')) return 'cmake';
  if (lower === 'dockerfile' || lower.startsWith('dockerfile.')) return 'dockerfile';
  if (lower === 'makefile' || lower === 'gnumakefile') return 'makefile';
  const dot = lower.lastIndexOf('.');
  const ext = dot >= 0 ? lower.slice(dot + 1) : '';
  const map = {
    js: 'javascript', mjs: 'javascript', cjs: 'javascript', jsx: 'javascript',
    ts: 'typescript', tsx: 'typescript', mts: 'typescript', cts: 'typescript',
    py: 'python', pyw: 'python',
    c: 'c', h: 'c',
    cc: 'cpp', cpp: 'cpp', cxx: 'cpp', hpp: 'cpp', hh: 'cpp', hxx: 'cpp',
    rs: 'rust', go: 'go', java: 'java', kt: 'kotlin', kts: 'kotlin',
    rb: 'ruby', php: 'php', swift: 'swift',
    cs: 'csharp', fs: 'fsharp',
    sh: 'bash', bash: 'bash', zsh: 'bash', fish: 'bash',
    json: 'json', jsonc: 'json',
    yml: 'yaml', yaml: 'yaml',
    toml: 'ini', ini: 'ini', conf: 'ini', cfg: 'ini',
    md: 'markdown', markdown: 'markdown',
    html: 'xml', htm: 'xml', xhtml: 'xml', svg: 'xml', xml: 'xml',
    css: 'css', scss: 'scss', less: 'less',
    sql: 'sql', graphql: 'graphql', gql: 'graphql',
    diff: 'diff', patch: 'diff',
    r: 'r', lua: 'lua', pl: 'perl', pm: 'perl',
    vim: 'vim', proto: 'protobuf',
    txt: 'plaintext', log: 'plaintext',
  };
  return map[ext] || '';
}

export function parseToolValue(value) {
  if (typeof value !== 'string') return value || {};
  try {
    return JSON.parse(value);
  } catch (error) {
    return { raw: value };
  }
}

export function sessionIdFromTaskResult(result) {
  if (result == null) return '';
  let obj = result;
  if (typeof result === 'string') {
    try { obj = JSON.parse(result); } catch (e) {
      const m = result.match(/task_id:\s*(\S+)/);
      return (m && m[1].indexOf('bg_') !== 0) ? m[1] : '';
    }
  }
  if (typeof obj !== 'object') return '';
  if (obj.result && typeof obj.result === 'object') {
    const nested = sessionIdFromTaskResult(obj.result);
    if (nested) return nested;
  }
  const meta = obj.metadata || {};
  let sid = meta.sessionId || meta.task_id || obj.sessionId || obj.task_id || '';
  if (sid && String(sid).indexOf('bg_') !== 0) return String(sid);
  if (typeof obj.output === 'string') {
    const m = obj.output.match(/task_id:\s*(\S+)/);
    if (m && m[1].indexOf('bg_') !== 0) return m[1];
  }
  return '';
}

export function extractChildSessionId(tc) {
  if (!tc) return '';
  const toolName = (tc.tool_name || '').toLowerCase();
  if (toolName !== 'task' && toolName !== 'dispatch_agent') return '';
  let sid = sessionIdFromTaskResult(tc.result);
  if (sid) return sid;
  if (tc.arguments) {
    const args = parseToolValue(tc.arguments);
    if (args && typeof args === 'object') {
      const candidate = args.sessionId || args.task_id || '';
      if (candidate && String(candidate).startsWith('ses_')) {
        return String(candidate);
      }
    }
  }
  return '';
}

export function esc(s) {
  const str = String(s == null ? '' : s);
  return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

export function capitalize(s) { return s.charAt(0).toUpperCase() + s.slice(1); }

export function relTime(ts) {
  const d = Date.now() - ts;
  if (d < 60e3) return 'just now';
  if (d < 3600e3) return Math.floor(d/60e3) + 'm ago';
  if (d < 86400e3) return Math.floor(d/3600e3) + 'h ago';
  return new Date(ts).toLocaleDateString();
}

export function extractFrontmatter(src) {
  if (typeof src !== 'string') return { frontmatter: null, body: src || '' };
  const trimmed = src.replace(/^[\uFEFF\r\n\s]+/, '');
  if (!trimmed.startsWith('---')) {
    return { frontmatter: null, body: src };
  }

  // Opening fence: require the `---` line to end OR be followed by a
  // YAML-ish `key:`/`key :` line — avoids treating a horizontal rule or an
  // arbitrary separator as frontmatter.
  const fenceEnd = trimmed.indexOf('\n');
  const openLine = fenceEnd >= 0 ? trimmed.slice(0, fenceEnd) : trimmed;
  if (!/^---[ \t]*$/.test(openLine)) {
    return { frontmatter: null, body: src };
  }
  const tail = fenceEnd >= 0 ? trimmed.slice(fenceEnd + 1) : '';
  const keyLine = /^([ \t]*[A-Za-z0-9_][\w.-]*[ \t]*:[ \t]*(?:[^\n]*|$))/.exec(tail);
  const closeOnly = /^([ \t]*---[ \t]*|\r?\n[ \t]*\.\.\.[ \t]*)/.exec(tail);
  // Plain closing fence with no keys is still valid YAML (empty map).
  if (!keyLine && !closeOnly) {
    return { frontmatter: null, body: src };
  }

  const contentStart = fenceEnd + 1;
  const closeRegex = /\r?\n(---|...)[ \t]*(?=\r?\n|$)/g;
  closeRegex.lastIndex = contentStart;
  const matchClose = closeRegex.exec(trimmed);
  if (!matchClose) return { frontmatter: null, body: src };

  const frontmatter = trimmed.slice(contentStart, matchClose.index);
  const bodyStart = matchClose.index + matchClose[0].length;
  let body = trimmed.slice(bodyStart);
  if (body.startsWith('\n')) body = body.slice(1);
  else if (body.startsWith('\r\n')) body = body.slice(2);

  return { frontmatter, body };
}

export function stripFrontmatter(text) {
  return extractFrontmatter(text);
}

export function transformWikilinks(src) {
  if (typeof src !== 'string') return src;
  const lines = src.split('\n');
  let inFence = false;
  const out = [];

  const pattern = /(!?)\[\[\s*([^\]|#]*?)\s*(?:#([^\]|]+))?\s*(?:\|([^\]]+))?\s*\]\]/g;

  for (const line of lines) {
    if (/^\s*```/.test(line)) { inFence = !inFence; out.push(line); continue; }
    if (inFence) { out.push(line); continue; }

    const parts = line.split(/(`[^`]+`)/);
    for (let i = 0; i < parts.length; i++) {
      if (!parts[i].startsWith('`')) {
        parts[i] = parts[i].replace(pattern, (_, isEmbedStr, targetRaw, headingRaw, aliasRaw) => {
          const isEmbed = Boolean(isEmbedStr);
          const target = (targetRaw || '').trim();
          const heading = (headingRaw || '').trim();
          const alias = (aliasRaw || '').trim();
          const hasExt = /\.[a-zA-Z0-9]+$/.test(target);

          let headingSlug = '';
          if (heading) {
            headingSlug = '#' + heading.toLowerCase().trim().replace(/[^\w\s-]/g, '').replace(/\s+/g, '-');
          }

          if (isEmbed) {
            if (hasExt && /\.(png|jpg|jpeg|gif|svg|webp)$/i.test(target)) {
              const alt = alias || target;
              return '![' + alt + '](<' + target + '>)';
            } else {
              const display = alias || (target + (heading ? ' > ' + heading : ''));
              const noteTarget = target ? (target + (target && !hasExt ? '.md' : '') + headingSlug) : headingSlug;
              return '<div class="md-transclusion-card" data-href="' + esc(noteTarget) + '">📄 <strong>' + esc(display) + '</strong></div>';
            }
          }

          if (!target && heading) {
            const disp = alias || ('#' + heading);
            return '[' + disp + '](<' + headingSlug + '>)';
          }

          const noteTarget = target + (target && !hasExt ? '.md' : '') + headingSlug;
          const disp = alias || (target + (heading ? ' > ' + heading : ''));
          return '[' + disp + '](<' + noteTarget + '>)';
        });
      }
    }
    out.push(parts.join(''));
  }
  return out.join('\n');
}

// Command catalog data (pure). Handlers stay in app.js.

export const SLASH_COMMANDS = [
  { name: '/help',        desc: 'Show help and keyboard shortcuts' },
  { name: '/model',       desc: 'Select a model from any provider' },
  { name: '/variant',     desc: 'Select thinking / reasoning effort' },
  { name: '/theme',       desc: 'Select a UI theme color' },
  { name: '/new',         desc: 'Start a new session' },
  { name: '/rename',      desc: 'Rename current session' },
  { name: '/session',     desc: 'Manage and load saved sessions' },
  { name: '/compact',     desc: 'Summarize conversation to save context' },
  { name: '/queue',       desc: 'List or drop queued prompts (/queue rm <n>)' },
  { name: '/clear-queue', desc: 'Clear all queued prompts' },
  { name: '/retry',       desc: 'Resend the last user prompt' },
  { name: '/tools',       desc: 'Toggle tool use: on|off' },
];

export const PALETTE_COMMANDS = [
  { id: 'session_new',         label: 'New Session',              sublabel: 'Ctrl+N',        category: 'Session' },
  { id: 'session_list',        label: 'Switch Session',           sublabel: '/session',      category: 'Session' },
  { id: 'session_rename',      label: 'Rename Session',           sublabel: '/rename',       category: 'Session' },
  { id: 'session_compact',     label: 'Compact Context',          sublabel: '/compact',      category: 'Session' },
  { id: 'session_clear_queue', label: 'Clear Prompt Queue',       sublabel: '/clear-queue',  category: 'Session' },
  { id: 'session_retry',       label: 'Retry Last Prompt',        sublabel: '/retry',        category: 'Session' },
  { id: 'model_select',        label: 'Select Model',             sublabel: '/model',        category: 'Model' },
  { id: 'model_variant',       label: 'Select Reasoning Variant', sublabel: '/variant',      category: 'Model' },
  { id: 'thinking_toggle',     label: 'Toggle Thinking Trace',    sublabel: 'F2',            category: 'View' },
  { id: 'chat_open',           label: 'Chat',                    sublabel: 'Alt+1',         category: 'View' },
  { id: 'files_open',          label: 'Changed Files',            sublabel: 'Alt+2',         category: 'View' },
  { id: 'stats_open',          label: 'Session Stats',            sublabel: 'Alt+3',         category: 'View' },
  { id: 'sessions_open',       label: 'Sessions & Subagents',     sublabel: 'Alt+4',         category: 'View' },
  { id: 'theme_select',        label: 'Switch Theme',             sublabel: '/theme',        category: 'Appearance' },
  { id: 'help',                label: 'Help & Shortcuts',         sublabel: '/help',         category: 'General' },
];
