with open(r"C:\Users\walker\.gemini\antigravity\brain\85ef3522-f44a-498e-9793-6dea70f49d57\scratch\more_classes.txt", "rb") as f:
    raw = f.read()
text = raw.decode("utf-16", errors="ignore") if raw.startswith(b"\xff\xfe") else raw.decode("utf-8", errors="ignore")
with open(r"C:\Users\walker\.gemini\antigravity\brain\85ef3522-f44a-498e-9793-6dea70f49d57\scratch\more_classes_utf8.txt", "w", encoding="utf-8") as f:
    f.write(text)
print(f"Lines: {len(text.splitlines())}")
for i, line in enumerate(text.splitlines(), 1):
    if line.startswith("==="):
        print(f"L{i}: {line}")
