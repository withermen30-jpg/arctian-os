









import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()

sym = "ca_bundle_pem"
with open(dst, "w", encoding="ascii") as f:
    f.write("/* Otomatik uretildi: tools/mk_roots.py - Mozilla CA bundle (PEM) */\n")
    f.write("/* Yeniden uretmek icin: python3 tools/mk_roots.py design/certs/cacert.pem kernel_plus/tls/roots.c */\n")
    f.write("#include \"roots.h\"\n\n")
    f.write("const unsigned char %s[] = {\n" % sym)
    for i in range(0, len(data), 16):
        f.write("  " + ",".join(str(b) for b in data[i:i + 16]) + ",\n")
    f.write("  0\n")
    f.write("};\n\n")
    f.write("const unsigned int %s_len = %d; /* sonlandirici NUL dahil */\n"
            % (sym, len(data) + 1))
print(f"mk_roots: {src} -> {dst} ({len(data)} byte PEM, {len(data) + 1} byte dizi)")
