#!/usr/bin/env bash
# Fix routing to PlutoSDR when Tailscale exit node steals local traffic.
# Known Tailscale bug (github.com/tailscale/tailscale/issues/15366):
# "Allow Local Network Access" doesn't work on macOS for some interfaces.
#
# Finds NCM interface by MAC prefix (00:e0:22 = Pluto host_addr).
# Usage: ./pluto-route.sh [pluto_ip]  (default: 192.168.2.1)
set -euo pipefail

PLUTO_IP="${1:-192.168.2.1}"
MAC_PREFIX="00:e0:22"

# Find the Pluto NCM interface
iface=$(ifconfig -l | tr ' ' '\n' | while read -r i; do
    ifconfig "$i" 2>/dev/null | grep -qi "$MAC_PREFIX" && echo "$i" && break
done)

if [ -z "$iface" ]; then
    echo "ERROR: Pluto NCM interface not found (MAC prefix $MAC_PREFIX)" >&2
    exit 1
fi

# Get subnet (e.g. 192.168.2.0/24)
mask_hex=$(ifconfig "$iface" | awk '/inet /{print $4}')
iface_ip=$(ifconfig "$iface" | awk '/inet /{print $2}')

if [ -z "$mask_hex" ] || [ -z "$iface_ip" ]; then
    echo "ERROR: $iface has no IP (is DHCP working?)" >&2
    exit 1
fi

# Compute network address (IP & mask)
mask=$(printf '%d' "$mask_hex")
IFS=. read -r a b c d <<< "$iface_ip"
ip=$(( (a<<24) + (b<<16) + (c<<8) + d ))
net=$(( ip & mask ))
net_addr="$((net>>24&255)).$((net>>16&255)).$((net>>8&255)).$((net&255))"

# Compute prefix length from mask
bits=0; m=$mask
while [ "$m" -gt 0 ]; do bits=$((bits + (m & 1))); m=$((m >> 1)); done

subnet="${net_addr}/${bits}"
subnet_short="${net_addr%.0}"  # netstat shows "192.168.2" not "192.168.2.0"

echo "Pluto: $iface  IP: $iface_ip  Subnet: $subnet"

# 1. Delete any conflicting route for this subnet NOT on $iface
#    (Tailscale bug: injects wrong gateway, e.g. 192.168.2.0/24 → router)
changed=0
while read -r bad_gw; do
    [ -n "$bad_gw" ] || continue
    echo "  delete conflicting subnet route via $bad_gw"
    sudo route -n delete -net "$subnet" "$bad_gw" 2>/dev/null || \
    sudo route -n delete -net "$subnet_short" "$bad_gw" 2>/dev/null || true
    changed=1
done < <(netstat -rn -f inet 2>/dev/null | \
    awk -v s="$net_addr" -v ss="$subnet_short" -v iface="$iface" \
        '($1==s || $1==ss) && $4!=iface && $4!="" && $2!="link#" {print $2}')

# 2. Delete any host route (Tailscale injects /32 for individual IPs)
while read -r bad_if; do
    [ -n "$bad_if" ] && [ "$bad_if" != "$iface" ] || continue
    echo "  delete host route via $bad_if"
    sudo route -n delete -host "$PLUTO_IP" 2>/dev/null || true
    changed=1
done < <(netstat -rn -f inet 2>/dev/null | \
    awk -v ip="$PLUTO_IP" -v iface="$iface" \
        '$1==ip && $4!=iface && $4!="" {print $4}')

# 3. Delete poisoned host route + ARP (route -interface creates permanent
#    ARP entry mapping Pluto IP → interface's own MAC — wrong).
#    Normalize MACs: arp may drop leading zeros (0:e0:22 vs 00:e0:22).
_normalize() { awk -F: '{for(i=1;i<=NF;i++) printf "%02x:", "0x"$i; print ""}' | sed 's/:$//'; }
arp_mac=$(arp -n "$PLUTO_IP" 2>/dev/null | awk '/\(/{print $4}' | _normalize)
iface_mac=$(ifconfig "$iface" | awk '/ether/{print $2}' | _normalize)
if [ -n "$arp_mac" ] && [ "$arp_mac" = "$iface_mac" ]; then
    echo "  ARP poisoned ($PLUTO_IP → self MAC), flushing"
    sudo arp -d "$PLUTO_IP" 2>/dev/null || true
    sudo route -n delete -host "$PLUTO_IP" 2>/dev/null || true
    changed=1
fi

# 4. If no host route exists via Pluto iface AND Tailscale exit node is
#    active (steals default route), add one. Otherwise the directly-connected
#    subnet route ($subnet on $iface) handles traffic.
#    NOTE: macOS route -host -interface creates poisoned ARP (self MAC).
#    We accept that tradeoff only when needed to fight Tailscale.
using_exit=$(tailscale status 2>/dev/null | grep -c 'active; exit node' | tr -d '[:space:]')
if [ "${using_exit:-0}" -gt 0 ] 2>/dev/null && ! route -n get "$PLUTO_IP" 2>/dev/null | grep -q "interface: $iface"; then
    echo "  Tailscale exit node active; adding host route via $iface"
    sudo route -n add -host "$PLUTO_IP" -interface "$iface" 2>/dev/null || true
    changed=1
fi

if [ "$changed" -eq 0 ]; then
    echo "Routes already correct."
else
    echo "Routes fixed."
fi

# Quick test
if ping -c 1 -t 2 "$PLUTO_IP" >/dev/null 2>&1; then
    echo "ping $PLUTO_IP OK"
else
    echo "ping $PLUTO_IP FAILED — Tailscale may have re-added its route."
    echo "If so, run this script again, or: tailscale set --exit-node="
fi
