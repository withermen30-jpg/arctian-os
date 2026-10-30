import os, random, hashlib, sys

# --- RSA-2048 gelistirme anahtari uretir ve C basliklarini yazar ---
# Kullanim: python tools/sig_keygen.py
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def is_probable_prime(n, k=32):
    if n < 2:
        return False
    for p in (2,3,5,7,11,13,17,19,23,29,31,37,41,43,47):
        if n % p == 0:
            return n == p
    d, s = n - 1, 0
    while d % 2 == 0:
        d //= 2
        s += 1
    for _ in range(k):
        a = random.randrange(2, n - 2)
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(s - 1):
            x = x * x % n
            if x == n - 1:
                break
        else:
            return False
    return True

def gen_prime(bits):
    while True:
        n = random.getrandbits(bits) | (1 << (bits - 1)) | 1
        if n % 65537 == 0:
            continue
        if is_probable_prime(n):
            return n

def limbs_le(n, count=64):
    out = []
    for i in range(count):
        out.append((n >> (32 * i)) & 0xFFFFFFFF)
    return out

def main():
    random.seed(0xA7C71A)   # tekrarlanabilir gelistirme anahtari
    e = 65537
    while True:
        p = gen_prime(1024)
        q = gen_prime(1024)
        if p == q:
            continue
        n = p * q
        if n.bit_length() != 2048:
            continue
        d = pow(e, -1, (p - 1) * (q - 1))
        break

    pub = os.path.join(ROOT, "include", "arctian", "sig_pub.h")
    prv = os.path.join(ROOT, "include", "arctian", "sig_priv.h")
    key = os.path.join(ROOT, "tools", "keys", "dev_rsa2048.txt")
    os.makedirs(os.path.dirname(key), exist_ok=True)

    with open(pub, "w", newline="\n") as f:
        f.write("/* Otomatik uretildi: tools/sig_keygen.py - DEGISTIRMEYIN.\n")
        f.write(" * Arctian 0.7 paket imza acik anahtari (RSA-2048, e=65537). */\n")
        f.write("#ifndef ARCTIAN_SIG_PUB_H\n#define ARCTIAN_SIG_PUB_H\n\n#include <stdint.h>\n\n")
        f.write("static const uint32_t SIG_MOD[64] = {\n")
        for i, v in enumerate(limbs_le(n)):
            f.write("    0x%08Xu%s\n" % (v, "," if i < 63 else ""))
        f.write("};\n\n#endif\n")

    with open(prv, "w", newline="\n") as f:
        f.write("/* GELISTIRME ANAHTARI - ARCTIAN_07_TEST derlemesinde test imzasi\n"
                " * uretmek icin kullanilir. Yayin imajinda bu anahtar kullanilmaz. */\n")
        f.write("#ifndef ARCTIAN_SIG_PRIV_H\n#define ARCTIAN_SIG_PRIV_H\n\n#include <stdint.h>\n\n")
        f.write("static const uint32_t SIG_D[64] = {\n")
        for i, v in enumerate(limbs_le(d)):
            f.write("    0x%08Xu%s\n" % (v, "," if i < 63 else ""))
        f.write("};\n\n#endif\n")

    with open(key, "w", newline="\n") as f:
        f.write("n=%x\ne=%x\nd=%x\n" % (n, e, d))

    print("n  = %064x" % n)
    print("yazildi: include/arctian/sig_pub.h, sig_priv.h, tools/keys/dev_rsa2048.txt")

if __name__ == "__main__":
    main()
