#!/bin/bash
#
# network.sh - Network utility functions for detecting physical network adapters
#
# Usage: source /scripts/network.sh
# Then use: wait_for_network, get_physical_interfaces, etc.
#

# Wait for the network to come online by detecting a physical adapter with an IP
# Usage: wait_for_network [timeout]
# Returns: 0 if network is up, 1 on timeout/error
wait_for_network() {
    local timeout=${1:-60}
    local max_attempts=$((timeout / 5))
    local attempt=1

    echo "Waiting for physical network adapter to come online (timeout: ${timeout}s)..."

    while [ $attempt -le $max_attempts ]; do
        if check_physical_network_up; then
            echo "Network is up!"
            return 0
        fi

        echo -n "."
        sleep 5
        attempt=$((attempt + 1))
    done

    echo ""
    echo "Timeout waiting for network to come online"
    return 1
}

# Check if any physical network adapter has an IP address assigned
# Returns: 0 if up, 1 if down/no adapters
check_physical_network_up() {
    local interfaces
    local ip_addresses

    # Get list of physical interfaces (excluding Docker/Podman virtual ones)
    interfaces=$(get_physical_interfaces)

    if [ -z "$interfaces" ]; then
        return 1
    fi

    # Check each interface for IP address
    for iface in $interfaces; do
        ip_addresses=$(ip addr show "$iface" 2>/dev/null | grep -E '^\s*inet\s' | awk '{print $2}' | cut -d/ -f1)

        if [ -n "$ip_addresses" ]; then
            echo "Physical interface $iface is up with IP: $ip_addresses"
            return 0
        fi
    done

    return 1
}

# Get list of physical network interfaces (excluding Docker/Podman virtual ones)
# Returns: space-separated list of interface names
get_physical_interfaces() {
    local interfaces
    local excluded_patterns

    # Build list of patterns to exclude (Docker, Podman, and common virtual adapters)
    excluded_patterns="docker|docker0|veth|vnet|br|bridge|virbr|podman|cni|flannel|calico|weave|ovn|ipvs"

    # Get all interfaces except loopback
    interfaces=$(ip -o link show | awk 'NR>1 && $1!="lo" {print $2}' | sort -u)

    # Filter out virtual interfaces
    for iface in $interfaces; do
        if echo "$iface" | grep -qiE "$excluded_patterns"; then
            echo "Excluding virtual interface: $iface" >&2
        fi
    done

    # Return only physical interfaces
    echo "$interfaces" | grep -viE "$excluded_patterns"
}

# Check if a specific interface is a physical adapter with IP
# Usage: check_physical_interface <interface_name>
# Returns: 0 if up, 1 if down/no IP
check_physical_interface() {
    local iface=$1

    if [ -z "$iface" ]; then
        echo "Error: Interface name required" >&2
        return 1
    fi

    # Check if interface exists
    if ! ip link show "$iface" &>/dev/null; then
        echo "Interface $iface does not exist" >&2
        return 1
    fi

    # Check if it's a physical interface (not virtual)
    local iface_type
    iface_type=$(ip -o link show "$iface" | awk '{print $2}')

    case "$iface_type" in
        "loopback")
            echo "Interface $iface is loopback, not physical" >&2
            return 1
            ;;
        *)
            # Check for IP address
            local ip_addresses
            ip_addresses=$(ip addr show "$iface" | grep -E '^\s*inet\s' | awk '{print $2}' | cut -d/ -f1)

            if [ -n "$ip_addresses" ]; then
                echo "Interface $iface is up with IP: $ip_addresses"
                return 0
            else
                echo "Interface $iface has no IP address assigned" >&2
                return 1
            fi
            ;;
    esac
}

# Get IP address of a specific physical interface
# Usage: get_interface_ip <interface_name>
# Returns: IP address or empty string if not configured
get_interface_ip() {
    local iface=$1

    check_physical_interface "$iface" || return 1

    ip addr show "$iface" | grep -E '^\s*inet\s' | awk '{print $2}' | cut -d/ -f1
}

# List all physical interfaces with their IP addresses
# Usage: list_physical_interfaces
list_physical_interfaces() {
    echo "Physical Network Interfaces:"
    echo "============================"

    local interfaces
    interfaces=$(get_physical_interfaces)

    if [ -z "$interfaces" ]; then
        echo "No physical network adapters detected"
        return 1
    fi

    for iface in $interfaces; do
        local ip
        ip=$(get_interface_ip "$iface")
        printf "%-20s %s\n" "$iface" "${ip:-(no IP)}"
    done
}

# Check if network is available (any physical adapter with IP)
# Usage: is_network_available
# Returns: 0 if available, 1 if not
is_network_available() {
    check_physical_network_up
}

# Export functions for use in other scripts
if [ "${BASH_SOURCE[0]}" != "${BASH_SOURCE[1]}" ]; then
    # Being sourced - export functions
    declare -f wait_for_network
    declare -f check_physical_network_up
    declare -f get_physical_interfaces
    declare -f check_physical_interface
    declare -f get_interface_ip
    declare -f list_physical_interfaces
    declare -f is_network_available
fi
