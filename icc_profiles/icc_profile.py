def convert_icc_to_byte_array(variable_name):
    with open(f'{variable_name}.icc', 'rb') as f:
        content = f.read()

    byte_array_str = ', '.join(f'0x{byte:02X}' for byte in content)
    return f'const uint8_t {variable_name}[] = {{ {byte_array_str} }};\n'

profiles = [
    'srgb_profile',
    'rec709_profile',
    'rec2020_profile',
    'rec601_pal_profile',
    'rec601_ntsc_profile',
]

for profile_name in profiles:
    byte_array = convert_icc_to_byte_array(profile_name)
    with open(f'{profile_name}.h', 'w') as f:
        f.write(byte_array)
