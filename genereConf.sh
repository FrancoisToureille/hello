#!/bin/bash

# Usage : ./generate_router_conf.sh R1 > router.conf

ROUTER_ID="$1"
if [[ -z "$ROUTER_ID" ]]; then
    echo "Usage: $0 <router_id>" >&2
    exit 1
fi
echo "router_id = $ROUTER_ID"
echo -n "interfaces = "

# Liste des interfaces actives avec une adresse IPv4
# Liste des interfaces actives avec une adresse IPv4, exclut lo
IFACES=()
while read -r iface; do
    IFACES+=("$iface")
done < <(ip -o -4 addr show scope global | awk '$2 != "lo" {print $2}' | sort -u)
# Écrire la liste des interfaces
echo "${IFACES[*]}" | sed 's/ /, /g'

# Pour chaque interface : récupérer l'adresse de broadcast
for iface in "${IFACES[@]}"; do
    # Récupérer l'IP et le broadcast
    IP_INFO=$(ip -o -4 addr show dev "$iface" | awk '{print $4 " " $6}')
    [[ -z "$IP_INFO" ]] && continue

    IP=$(echo "$IP_INFO" | cut -d' ' -f1 | cut -d'/' -f1)
    BRD=$(echo "$IP_INFO" | cut -d' ' -f2)

    if [[ "$BRD" != "-" && -n "$BRD" ]]; then
        echo "broadcast.$iface = $BRD"
    fi
done
