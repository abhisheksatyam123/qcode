#!/usr/bin/env bash
#
# Trim the RAM-backed scratch directory.
#
# /tmp is a tmpfs: every file in it is RAM, and it is swapped out when memory
# runs short. Five stale copies of the 1 GB qcode session database once filled
# it (78% of /tmp) and pushed the 4 GB swap device to 100%. This removes scratch
# that is older than a day, and trims the oldest files when /tmp is over its
# budget.
#
# Usage: scripts/clean_tmp_scratch.sh [max_age_days] [max_tmp_used_mb]

set -uo pipefail

SCRATCH="${QCODE_SCRATCH_DIR:-/tmp/opencode}"
MAX_AGE_DAYS="${1:-1}"
MAX_TMP_USED_MB="${2:-1024}"

if [ ! -d "$SCRATCH" ]; then
    exit 0
fi

# 1. Drop scratch older than the age limit.
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
    echo "clean_tmp_scratch: /tmp trimmed to ${tmp_used_mb} MB (budget ${MAX_TMP_USED_MB} MB)"
fi
