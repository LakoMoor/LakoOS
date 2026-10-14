#!/usr/bin/env python3
"""Build update.ini for lakoupdate from a small description.

Usage: make-update-feed.py out/update.ini BASEURL spec.txt
spec.txt, one package per block:
  id=LAKO-KB0001
  title=Shell fixes
  kind=file|driver
  reboot=0|1
  file=<local path>;<destination path or empty for drivers>
Blocks are separated by a blank line. Files are hashed with SHA-256 and the
URL is BASEURL/<file name>.
"""
import hashlib, os, sys

out, base, spec = sys.argv[1:4]
blocks = [b for b in open(spec).read().split("\n\n") if b.strip()]
lines = ["[Update]", "Version=" + os.environ.get("LAKOOS_VERSION", "0.0.0"),
         "Packages=%d" % len(blocks), ""]
for n, b in enumerate(blocks, 1):
    kv, files = {}, []
    for l in b.strip().splitlines():
        k, _, v = l.partition("=")
        if k == "file":
            files.append(v.split(";"))
        else:
            kv[k] = v
    lines += ["[Package%d]" % n, "Id=" + kv["id"], "Title=" + kv.get("title", kv["id"]),
              "Kind=" + kv.get("kind", "file"), "Reboot=" + kv.get("reboot", "0"),
              "Files=%d" % len(files)]
    for i, (path, dest) in enumerate(files, 1):
        name = os.path.basename(path)
        lines += ["File%dName=%s" % (i, name), "File%dUrl=%s/%s" % (i, base.rstrip("/"), name),
                  "File%dSha256=%s" % (i, hashlib.sha256(open(path, "rb").read()).hexdigest())]
        if dest:
            lines.append("File%dDest=%s" % (i, dest))
    lines.append("")
open(out, "w").write("\n".join(lines))
