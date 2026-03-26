# shellcheck shell=sh
# Show Tailscale status on login
if command -v tailscale >/dev/null 2>&1; then
    ts_status=$(tailscale status --json 2>/dev/null)
    if [ -n "$ts_status" ]; then
        ts_state=$(echo "$ts_status" | grep -o '"BackendState":"[^"]*"' | cut -d'"' -f4)
        if [ "$ts_state" = "Running" ]; then
            ts_ip=$(tailscale ip -4 2>/dev/null)
            printf "\033[32mTailscale: connected (%s)\033[0m\n" "$ts_ip"
        elif [ "$ts_state" = "NeedsLogin" ]; then
            printf "\033[33mTailscale: not authenticated. Run:\033[0m\n"
            printf "  tailscale up --qr\n\n"
        else
            printf "\033[33mTailscale: %s\033[0m\n" "$ts_state"
        fi
    fi
fi
