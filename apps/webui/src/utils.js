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
