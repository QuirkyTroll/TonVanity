import re
import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
text = (root / "src" / "native" / "wordlist_en.h").read_text(encoding="utf-8")
words = re.findall(r'"([a-z]+)"', text)
assert len(words) == 2048, len(words)
blob = b"".join(w.encode("ascii").ljust(9, b"\0") for w in words)
out = root.parent / ".work" / "wordlist.bin"
out.write_bytes(blob)
print(out, len(blob))
