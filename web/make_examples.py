"""Copies the examples for the web page and writes examples/index.json.
A single .cs file in examples/ becomes its own example (with the images and sounds next to it);
a folder becomes a project example."""
import json, os, shutil, sys

src, dst = sys.argv[1], sys.argv[2]
MEDIA = (".png", ".jpg", ".jpeg", ".bmp", ".wav")
root_media = sorted(f for f in os.listdir(src) if f.lower().endswith(MEDIA))
for f in root_media:
    shutil.copy(os.path.join(src, f), dst)
items = []
for name in sorted(os.listdir(src)):
    path = os.path.join(src, name)
    if name.endswith(".cs"):
        shutil.copy(path, dst)
        items.append({"name": name[:-3], "files": [name], "media": root_media})
    elif os.path.isdir(path):
        os.makedirs(os.path.join(dst, name), exist_ok=True)
        files, media = [], []
        for f in sorted(os.listdir(path)):
            if f.endswith(".cs"): files.append(name + "/" + f)
            elif f.lower().endswith(MEDIA): media.append(name + "/" + f)
            else: continue
            shutil.copy(os.path.join(path, f), os.path.join(dst, name, f))
        if files:
            items.append({"name": name + " (projekt)", "files": files, "media": media})
json.dump(items, open(os.path.join(dst, "index.json"), "w"), indent=1)
