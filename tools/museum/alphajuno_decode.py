#!/usr/bin/env python3
"""Decodes a Roland MKS-50 / Alpha Juno tone bulk dump (BLD), from the MKS-50 owner's manual, section 3.2,
'*2 TONE data format' (ManualsLib copy 695917, page 65) and its switch-bit list (page 66).

Message: F0 41 37 0n 23 20 01 00 pp, 256 nibble bytes (4 tones of 32 bytes, each byte as two nibbles, least
significant first), F7.
"""
import sys

NAME_CHARS = [chr(ord('A') + i) for i in range(26)] + [chr(ord('a') + i) for i in range(26)] + \
             [str(i) for i in range(10)] + [' ', '-']


def tones(path):
    data = open(path, 'rb').read()
    out = []
    i = 0
    while i < len(data):
        j = data.index(0xF7, i)
        msg = data[i:j + 1]
        i = j + 1
        assert msg[:7] == bytes([0xF0, 0x41, 0x37, msg[3], 0x23, 0x20, 0x01]), msg[:9].hex()
        nibbles = msg[9:-1]
        assert len(nibbles) == 256, len(nibbles)
        raw = bytes((nibbles[2 * k] & 0x0F) | ((nibbles[2 * k + 1] & 0x0F) << 4) for k in range(128))
        for t in range(4):
            out.append(raw[32 * t:32 * t + 32])
    return out


def decode(b):
    bit = lambda n: (b[4 + n] >> 7) & 1  # switch bit bNN is bit 7 of byte 4 + NN
    p = {}
    p['dco_after_depth4'] = b[0] >> 4
    p['vcf_key_follow4'] = b[0] & 15
    p['vcf_after_depth4'] = b[1] >> 4
    p['vca_after_depth4'] = b[1] & 15
    p['env_key_follow4'] = b[2] >> 4
    p['bender_range'] = b[2] & 15
    p['dco_lfo_depth'] = b[3] & 127
    names = ['dco_env_depth', 'dco_pw_pwm_depth', 'dco_pwm_rate', 'vcf_cutoff', 'vcf_resonance', 'vcf_env_depth',
             'vcf_lfo_depth', 'vca_level', 'lfo_rate', 'lfo_delay', 'env_t1', 'env_l1', 'env_t2', 'env_l2',
             'env_t3', 'env_l3', 'env_t4']
    for k, n in enumerate(names):
        p[n] = b[4 + k] & 127
    p['chorus'] = bit(0)
    p['dco_env_mode'] = bit(1) * 2 + bit(2)
    p['vcf_env_mode'] = bit(3) * 2 + bit(4)
    p['vca_env_mode'] = bit(5) * 2 + bit(6)
    p['wave_sub'] = bit(7) * 4 + bit(8) * 2 + bit(9)
    p['wave_saw'] = bit(10) * 4 + bit(11) * 2 + bit(12)
    p['wave_pulse'] = bit(13) * 2 + bit(14)
    p['hpf'] = bit(15) * 2 + bit(16)
    p['dco_range'] = bit(17) * 2 + bit(18)
    p['sub_level'] = bit(19) * 2 + bit(20)
    p['noise_level'] = bit(21) * 2 + bit(22)
    c = 0
    for k, byte in enumerate(b[27:31]):  # bit 6 = c(2k), bit 7 = c(2k+1)
        c |= ((byte >> 6) & 1) << (2 * k)
        c |= ((byte >> 7) & 1) << (2 * k + 1)
    p['chorus_rate'] = c & 127
    p['name'] = ''.join(NAME_CHARS[x & 63] for x in b[21:31])
    p['tone_data_code'] = b[31]
    return p


if __name__ == '__main__':
    all_tones = tones(sys.argv[1])
    for index, b in enumerate(all_tones):
        p = decode(b)
        label = '%d%d' % (index // 8 + 1, index % 8 + 1)
        if len(sys.argv) > 2 and sys.argv[2].lower() not in p['name'].lower():
            continue
        print(label, repr(p['name']), 'code', p['tone_data_code'])
        if len(sys.argv) > 2:
            for k, v in p.items():
                print('   ', k, '=', v)
