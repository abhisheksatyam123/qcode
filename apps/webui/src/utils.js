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
  { name: '/agent',       desc: 'Switch between orchestrator and plan' },
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
  { id: 'agent_mode_toggle',   label: 'Toggle Agent Mode',        sublabel: '/agent',        category: 'Agent' },
  { id: 'thinking_toggle',     label: 'Toggle Thinking Trace',    sublabel: 'F2',            category: 'View' },
  { id: 'chat_open',           label: 'Chat',                    sublabel: 'Alt+1',         category: 'View' },
  { id: 'files_open',          label: 'Changed Files',            sublabel: 'Alt+2',         category: 'View' },
  { id: 'stats_open',          label: 'Session Stats',            sublabel: 'Alt+3',         category: 'View' },
  { id: 'sessions_open',       label: 'Sessions & Subagents',     sublabel: 'Alt+4',         category: 'View' },
  { id: 'theme_select',        label: 'Switch Theme',             sublabel: '/theme',        category: 'Appearance' },
  { id: 'help',                label: 'Help & Shortcuts',         sublabel: '/help',         category: 'General' },
];
