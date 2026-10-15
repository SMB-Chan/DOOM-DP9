#!/bin/bash
set -euo pipefail
cd /opt/exword/libexword
python3 - <<'PY'
from pathlib import Path

# 1. Fix obex_object_add_header name in obex.c
obex_c = Path("src/obex.c")
t = obex_c.read_text()
t = t.replace(
    "int obex_object_addheader(obex_t *self, obex_object_t *object, uint8_t hi,",
    "int obex_object_add_header(obex_t *self, obex_object_t *object, uint8_t hi,",
)
obex_c.write_text(t)

# 2. Extract crypt.c from 2.0-dev (pr5) and integrate get_xor_key + 64-bit int32_t _crypt fix into dict.c
import subprocess
crypt_src = subprocess.check_output(["git", "show", "pr5:src/crypt.c"], text=True)

dict_c = Path("src/dict.c")
t = dict_c.read_text()
if "#include <stdint.h>" not in t:
    t = "#include <stdint.h>\n" + t
if "get_xor_key(" not in t:
    # Insert crypt_src helper functions before _crypt
    t = t.replace("void _crypt(", crypt_src + "\nvoid _crypt(")
t = t.replace("*((long *)ptr) = *((long *)ptr) ^ ((long *)key)[0];", "*((int32_t *)ptr) = *((int32_t *)ptr) ^ ((int32_t *)key)[0];")
t = t.replace("*((long *)ptr) = *((long *)ptr) ^ ((long *)key)[1];", "*((int32_t *)ptr) = *((int32_t *)ptr) ^ ((int32_t *)key)[1];")
t = t.replace("*((long *)ptr) = *((long *)ptr) ^ ((long *)key)[2];", "*((int32_t *)ptr) = *((int32_t *)ptr) ^ ((int32_t *)key)[2];")
t = t.replace("*((long *)ptr) = *((long *)ptr) ^ ((long *)key)[3];", "*((int32_t *)ptr) = *((int32_t *)ptr) ^ ((int32_t *)key)[3];")

# Also update dict_install to compute key2 dynamically from returned ck.key using get_xor_key!
old_install_block = """\trsp = exword_unlock(device);
\trsp |= exword_cname(device, name, id);
\trsp |= exword_cryptkey(device, &ck);
\tfree(name);"""
new_install_block = """\trsp = exword_unlock(device);
\trsp |= exword_cname(device, name, id);
\trsp |= exword_cryptkey(device, &ck);
\t{
\t\tchar fullkey[16];
\t\tmemcpy(fullkey, ck.key, 12);
\t\tmemcpy(fullkey + 12, ck.blk2 + 8, 4);
\t\tget_xor_key(fullkey, 16, key2);
\t}
\tfree(name);"""
if old_install_block in t:
    t = t.replace(old_install_block, new_install_block)

dict_c.write_text(t)

# 3. Fix exword.h declaration
eh = Path("src/exword.h")
t = eh.read_text()
t = t.replace(
    "int dict_reset(exword_t *device, const char *user);",
    "int dict_reset(exword_t *device, char *user);",
)
eh.write_text(t)

print("patched dict.c (64-bit _crypt fix), obex.c, exword.h")
PY
make -j"$(nproc)"
make install
ldconfig
echo "==== installed ===="
which exword
ls -l /usr/local/bin/exword /usr/local/lib/libexword*
echo "==== verify _crypt disassembly (must use xorl 32-bit, NOT xorq 64-bit) ===="
objdump -d src/exword-dict.o | sed -n '/<_crypt>:/,/^$/p'

