#!/usr/bin/env bash
set -euo pipefail

dur=$(grep -R stream-success hosts/client/tgen.*.stdout | awk -F'usecs-to-last-byte-recv=' '{print $2}' | awk -F',' '{print $1}' | tail -n1 || true)
echo "duration_us=${dur:-}"
if [[ -z "${dur:-}" ]]; then
  echo "No stream-success line found"
  exit 1
fi
# Require at least 8 seconds for the throttled transfer
if [[ "$dur" -lt 8000000 ]]; then
  echo "Edge limiting: transfer finished too fast"
  exit 1
fi

# Also verify log-level signals are present
enabled_logs=$(grep -R "Experimental edge bandwidth limiting enabled for" . || true)
if [[ -z "$enabled_logs" ]]; then
  echo "Edge limiting: enablement log not found"
  exit 1
fi

delay_logs=$(grep -R "Edge bandwidth limiting: delaying packet hop" hosts 2>/dev/null || true)
if [[ -z "$delay_logs" ]]; then
  echo "Note: Edge limiting delay logs not found in per-host files; continuing"
fi

exit 0
