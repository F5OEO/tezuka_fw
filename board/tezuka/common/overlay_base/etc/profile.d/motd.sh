# shellcheck shell=sh
# Print MOTD with proper terminal colors
# (sshd PrintMotd is disabled — escape sequences don't work there)
printf '\033[1;35m'
cat /etc/motd.plain 2>/dev/null
printf '\033[0m\n'
