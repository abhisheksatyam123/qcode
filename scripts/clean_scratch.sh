#!/usr/bin/env bash
#
# Trim qcode's scratch space.
#
# - /tmp is a tmpfs: every file in it is RAM, and it is swapped out when memory
#   runs short. Five stale copies of the 1 GB qcode session database once filled
#   it (78% of /tmp) and pushed the 4 GB swap device to 100%. Scratch in
#   /tmp/opencode older than a day is removed, and the oldest files are deleted
#   while /tmp is over its budget.
# - ~/.cache/qcode/tool-output holds one file per bash tool call that spilled
#   its output. Nothing prunes it, so it grows without bound (9757 files / 351 MB
#   after two months); entries older than a week are removed.
#
# Usage: scripts/clean_scratch.sh [max_age_days] [max_tmp_used_mb]

set -uo pipefail

SCRATCH="${QCODE_SCRATCH_DIR:-/tmp/opencode}"
MAX_AGE_DAYS="${1:-1}"
MAX_TMP_USED_MB="${2:-1024}"
TOOL_OUTPUT="${HOME}/.cache/qcode/tool-output"
TOOL_OUTPUT_MAX_AGE_DAYS="${QCODE_TOOL_OUTPUT_MAX_AGE_DAYS:-7}"

# 1. Drop /tmp scratch older than the age limit.
if [ -d "$SCRATCH" ]; then
    find "$SCRATCH" -mindepth 1 -maxdepth 1 -mtime "+$MAX_AGE_DAYS" -exec rm -rf -- {} + 2>/dev/null

    # 2. Trim the oldest files while /tmp is over budget.
    tmp_used_mb="$(df -Pm /tmp 2>/dev/null | awk 'NR==2 {print $3}')"
    if [ -n "${tmp_used_mb:-}" ] && [ "$tmp_used_mb" -gt "$MAX_TMP_USED_MB" ]; then
        while [ "$tmp_used_mb" -gt "$MAX_TMP_USED_MB" ]; do
            oldest="$(find "$SCRATCH" -mindepth 1 -maxdepth 1 -type f -printf '%T@ %p\n' 2>/dev/null \
                | sort -n | head -1 | cut -d' ' -f2-)"
            if [ -z "$oldest" ]; then
                break
            fi
            rm -f -- "$oldest" 2>/dev/null
            tmp_used_mb="$(df -Pm /tmp 2>/dev/null | awk 'NR==2 {print $3}')"
        done
        echo "clean_scratch: /tmp trimmed to ${tmp_used_mb} MB (budget ${MAX_TMP_USED_MB} MB)"
    fi
fi

# 3. Drop bash tool output that no session has read in a week.
if [ -d "$TOOL_OUTPUT" ]; then
    find "$TOOL_OUTPUT" -mindepth 1 -maxdepth 1 -mtime "+$TOOL_OUTPUT_MAX_AGE_DAYS" \
        -exec rm -f -- {} + 2>/dev/null
fi
