import re


def parse_lua_array(lua_str: str) -> tuple[str, list[int]]:
    # Extract the base variable name (strip trailing 'L' or 'U')
    var_match = re.search(r"(\w+)[LU]\s*=", lua_str)
    if not var_match:
        raise ValueError("Could not find a valid variable name ending in L or U.")
    base_name = var_match.group(1)

    # Extract all the integer values sequentially
    values = [int(val) for val in re.findall(r"=\s*(\d+)", lua_str)]
    return base_name, values


def combine_lua_strings(str_l: str, str_u: str) -> str:
    # Parse both strings
    name_l, values_l = parse_lua_array(str_l)
    name_u, values_u = parse_lua_array(str_u)

    if name_l != name_u:
        raise ValueError(
            f"Variable names do not match: {name_l} vs {name_u}"
        )
    if len(values_l) != len(values_u):
        raise ValueError("The two tables have a different number of elements.")

    # Combine lower 32-bit and upper 32-bit values into a single 64-bit uint64_t
    combined_values = []
    for low, high in zip(values_l, values_u):
        combined = ((high & 0xFFFFFFFF) << 32) | (low & 0xFFFFFFFF)
        combined_values.append(f"{combined}ULL")

    # Format into a clean, one-liner C++ array definitions
    array_elements = ", ".join(combined_values)
    return f"uint64_t {name_l}[] = {{{array_elements}}};"


# --- Example Usage ---
if __name__ == "__main__":
    # Paste your actual Lua-formatted text blocks here
    input_l = """
    castlingRightsMaskL = {
	[1] = 14376,
	[2] = 14376,
	[3] = 14376,
	[4] = 14376,
	[5] = 14380,
	[6] = 14380,
	[7] = 14380,
	[8] = 14380,
	[9] = 14440,
	[10] = 14440,
	[11] = 14440,
	[12] = 14440,
	[13] = 14444,
	[14] = 14444,
	[15] = 14444,
	[16] = 14444,
}
    """

    input_u = """
    castlingRightsMaskU = {
	[1] = 674758656,
	[2] = 741867520,
	[3] = 1748500480,
	[4] = 1815609344,
	[5] = 674758656,
	[6] = 741867520,
	[7] = 1748500480,
	[8] = 1815609344,
	[9] = 674758656,
	[10] = 741867520,
	[11] = 1748500480,
	[12] = 1815609344,
	[13] = 674758656,
	[14] = 741867520,
	[15] = 1748500480,
	[16] = 1815609344,
}
    """

    cpp_line = combine_lua_strings(input_l, input_u)
    print(cpp_line)
