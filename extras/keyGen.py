import secrets

def generate_key(length):
    return [secrets.randbits(8) for _ in range(length)]

def format_key(name, key, const_qualified=False):
    qualifier = "const " if const_qualified else ""
    lines = []
    lines.append(f"{qualifier}uint8_t {name}[{len(key)}] = {{")
    for i in range(0, len(key), 8):
        chunk = key[i:i+8]
        hex_values = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"  {hex_values},")
    lines[-1] = lines[-1].rstrip(',')  # Remove trailing comma
    lines.append(f"}}; // {name} ({len(key)*8}-bit)\n")
    return "\n".join(lines)

# Generate secure keys
devEUI = generate_key(8)
appEUI = generate_key(8)
appKey = generate_key(16)
hmacKey = generate_key(16)
gatewayEUI = generate_key(8)

# Print all keys in C-style format
print(format_key("devEUI", devEUI))
print(format_key("appEUI", appEUI))
print(format_key("appKey", appKey))
print(format_key("hmacKey", hmacKey, const_qualified=True))  # Mark as const
print(format_key("GatewayEUI", gatewayEUI))
