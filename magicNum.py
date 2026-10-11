import random

# Number of relevant occupancy bits per square (excluding edges)
BISHOP_SHIFTS = [
    6, 5, 5, 5, 5, 5, 5, 6,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    6, 5, 5, 5, 5, 5, 5, 6
]

ROOK_SHIFTS = [
    12, 11, 11, 11, 11, 11, 11, 12,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    12, 11, 11, 11, 11, 11, 11, 12
]

def mask_bishop_attacks(sq):
    mask = 0
    tr, tc = sq // 8, sq % 8
    # Generate the 4 diagonal directions, excluding the target edges
    for r, c in [(-1,-1), (-1,1), (1,-1), (1,1)]:
        cur_r, cur_c = tr + r, tc + c
        while 0 < cur_r < 7 and 0 < cur_c < 7:
            mask |= (1 << (cur_r * 8 + cur_c))
            cur_r += r
            cur_c += c
    return mask

def mask_rook_attacks(sq):
    mask = 0
    tr, tc = sq // 8, sq % 8
    # Generate the 4 straight directions, excluding the target edges
    for cur_r in range(tr + 1, 7): mask |= (1 << (cur_r * 8 + tc))
    for cur_r in range(tr - 1, 0, -1): mask |= (1 << (cur_r * 8 + tc))
    for cur_c in range(tc + 1, 7): mask |= (1 << (tr * 8 + cur_c))
    for cur_c in range(tc - 1, 0, -1): mask |= (1 << (tr * 8 + cur_c))
    return mask

def set_occupancy(index, bits_in_mask, attack_mask):
    occupancy = 0
    for i in range(bits_in_mask):
        square = (attack_mask & -attack_mask).bit_length() - 1
        attack_mask &= attack_mask - 1  # Clear lowest set bit
        if index & (1 << i):
            occupancy |= (1 << square)
    return occupancy

def get_bishop_legal_attacks(sq, occ):
    attacks = 0
    tr, tc = sq // 8, sq % 8
    for r, c in [(-1,-1), (-1,1), (1,-1), (1,1)]:
        cur_r, cur_c = tr + r, tc + c
        while 0 <= cur_r < 8 and 0 <= cur_c < 8:
            attacks |= (1 << (cur_r * 8 + cur_c))
            if occ & (1 << (cur_r * 8 + cur_c)):
                break
            cur_r += r
            cur_c += c
    return attacks

def get_rook_legal_attacks(sq, occ):
    attacks = 0
    tr, tc = sq // 8, sq % 8
    for r, c in [(-1,0), (1,0), (0,-1), (0,1)]:
        cur_r, cur_c = tr + r, tc + c
        while 0 <= cur_r < 8 and 0 <= cur_c < 8:
            attacks |= (1 << (cur_r * 8 + cur_c))
            if occ & (1 << (cur_r * 8 + cur_c)):
                break
            cur_r += r
            cur_c += c
    return attacks

def find_magic_number(sq, relevant_bits, is_bishop):
    mask = mask_bishop_attacks(sq) if is_bishop else mask_rook_attacks(sq)
    num_configs = 1 << relevant_bits
    
    occupancies = []
    attacks = []
    for i in range(num_configs):
        occ = set_occupancy(i, relevant_bits, mask)
        occupancies.append(occ)
        attacks.append(get_bishop_legal_attacks(sq, occ) if is_bishop else get_rook_legal_attacks(sq, occ))
        
    rng = random.Random(sq + (777 if is_bishop else 888))
    shift = 64 - relevant_bits
    
    # Pre-allocate arrays to avoid dictionary overhead and garbage collection
    used_attacks = [0] * num_configs
    used_epochs = [0] * num_configs
    epoch = 0
    
    while True:
        epoch += 1
        
        magic = rng.getrandbits(64) & rng.getrandbits(64) & rng.getrandbits(64)
        
        if bin((mask * magic) & 0xFFFFFFFFFFFFFFFF).count('1') < 6:
            continue
            
        fail = False
        
        for occ, attack in zip(occupancies, attacks):
            idx = ((occ * magic) & 0xFFFFFFFFFFFFFFFF) >> shift
            
            # If we've seen this index in the current epoch, check for a collision
            if used_epochs[idx] == epoch:
                if used_attacks[idx] != attack:
                    fail = True
                    break
            else:
                # First time seeing this index in this epoch; record it
                used_epochs[idx] = epoch
                used_attacks[idx] = attack
                
        if not fail:
            return magic

def print_cpp_array(name, magics):
    print(f"constexpr uint64_t {name}[64] = {{")
    for row in range(8):
        row_str = "    "
        for col in range(8):
            sq = row * 8 + col
            row_str += f"0x{magics[sq]:016X}ULL"
            if sq < 63:
                row_str += ", "
        print(row_str)
    print("};\n")
print("// Generating magic numbers...\n")
bishop_magics = [find_magic_number(sq, BISHOP_SHIFTS[sq], True) for sq in range(64)]
print_cpp_array("BISHOP_MAGICS", bishop_magics)
rook_magics = [find_magic_number(sq, ROOK_SHIFTS[sq], False) for sq in range(64)]
print_cpp_array("ROOK_MAGICS", rook_magics)