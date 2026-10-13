import os, re
HERE = os.path.dirname(os.path.abspath(__file__))
PAGES = [("index.html", "Home", "nav.home"), ("features.html", "Features", "nav.features"),
         ("download.html", "Download", "nav.download"), ("docs.html", "Docs", "nav.docs"),
         ("compatibility.html", "Compatibility", "nav.compat"), ("about.html", "About", "nav.about")]

def head(title, active, desc):
    nav = "".join('<a href="%s"%s data-i18n="%s">%s</a>' % (f, ' aria-current="page"' if f == active else "", k, n)
                  for f, n, k in PAGES if f != "index.html")
    return f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title}</title>
<meta name="description" content="{desc}">
<meta property="og:title" content="{title}">
<meta property="og:image" content="assets/screens/start-win11.png">
<link rel="icon" type="image/svg+xml" href="assets/logo-mark.svg">
<link rel="stylesheet" href="assets/style.css">
</head>
<body>
<header class="nav"><div class="wrap">
  <a class="brand" href="index.html"><img src="assets/logo-mark.svg" alt="">LakoOS</a>
  <nav>{nav}</nav>
  <span class="sp"></span>
  <button class="icon-btn lang" id="lang" aria-label="Language">RU</button>
  <button class="icon-btn" id="theme" aria-label="Theme">&#9790;</button>
  <a class="btn primary sm" href="download.html" data-i18n="nav.get">Get LakoOS</a>
</div></header>
<main>
'''

FOOT = '''</main>
<footer><div class="wrap">
  <div class="cols">
    <div><a class="brand" href="index.html" style="margin-bottom:10px"><img src="assets/logo-mark.svg" alt="">LakoOS</a>A free NT operating system with a familiar desktop.</div>
    <div><h4>Product</h4><a href="features.html">Features</a><a href="compatibility.html">Compatibility</a><a href="download.html">Download</a></div>
    <div><h4>Learn</h4><a href="docs.html">Documentation</a><a href="docs.html#build">Build from source</a><a href="docs.html#settings">Settings</a></div>
    <div><h4>Project</h4><a href="https://github.com/LakoMoor/LakoOS">GitHub</a><a href="https://github.com/LakoMoor/LakoOS/issues">Issues</a><a href="about.html">About</a></div>
  </div>
  <div class="legal">LakoOS is free software under the GNU GPL. Windows is a trademark of Microsoft Corporation. LakoOS is not affiliated with, endorsed by or sponsored by Microsoft. Based on LakoOS and ReactOS.</div>
</div></footer>
<script src="assets/app.js"></script>
</body>
</html>
'''

def page_hero(eyebrow, title, lead):
    return f'''<section class="phero"><span class="blob b1"></span><span class="blob b2"></span>
<div class="wrap"><span class="eyebrow">{eyebrow}</span><h1 class="ph">{title}</h1><p class="lead" style="margin-left:0">{lead}</p></div></section>
'''

def write(name, title, desc, body):
    html = head(title, name, desc) + body + FOOT
    open(os.path.join(HERE, name), "w").write(html)

home = open(os.path.join(HERE, "_src", "home.html")).read()
home = home.replace('href="#download"', 'href="download.html"').replace('href="#build"', 'href="docs.html#build"')
write("index.html", "LakoOS — a free NT operating system with a familiar desktop",
      "LakoOS is a free, open-source NT-based operating system with a centered Start menu, a modern File Explorer and the same folders and program names you know.", home)

def sec(inner, alt=False, id=""):
    return f'<section{" id=%s" % chr(34)+id+chr(34) if id else ""}{" class=%salt%s" % (chr(34), chr(34)) if alt else ""}><div class="wrap">{inner}</div></section>\n'

def card(title, text, icon="▣"):
    return f'<div class="tile s2 reveal"><div class="ic">{icon}</div><h3>{title}</h3><p>{text}</p></div>'

features = page_hero("Features", "Built to feel familiar", "The shell, the apps and the layout follow the Windows you know. Underneath is a free NT-style system.")
features += sec('''
<div class="two"><div><span class="eyebrow">Start</span><h2>A Start menu you can shape</h2>
<p class="sub">Two layouts share one engine. Pinned apps, recommendations, instant search and a power menu.</p>
<ul class="chk"><li><b>Centered style:</b> rounded panel, 6×3 pinned grid, Recommended list.</li><li><b>List and tiles style:</b> navigation rail, alphabetical list with letters, tiles.</li><li>Keyboard first: type to search, Enter to launch, Esc to close.</li><li>Follows the light or dark theme.</li></ul></div>
<div class="shot"><img src="assets/screens/start-win11.png" alt="Start menu"></div></div>''')
features += sec('''
<div class="two"><div class="shot"><img src="assets/screens/explorer.png" alt="File Explorer"></div>
<div><span class="eyebrow">File Explorer</span><h2>Ribbon, panes and drives</h2>
<p class="sub">The new File Explorer opens for every folder. It is <code>explorer.exe</code> in the Windows folder.</p>
<ul class="chk"><li>Ribbon with File, Computer and View tabs.</li><li>Navigation pane with Quick access and This PC.</li><li>Drives with free-space bars, address bar and search.</li><li>List and tile views.</li></ul></div></div>''', alt=True)
features += sec('''
<div class="center"><span class="eyebrow">Everyday tools</span><h2>The tools keep their names</h2><p class="sub">Open them the way you always did.</p></div>
<div class="bento">'''
 + card("Task Manager", "<code>taskmgr.exe</code> with processes, performance and details.")
 + card("Control Panel", "<code>control.exe</code> with categories, search and every <code>.cpl</code> applet.")
 + card("Command Prompt", "<code>cmd.exe</code> with the usual command line tools.")
 + card("Registry Editor", "<code>regedit.exe</code> for hives and values.")
 + card("Disk Management", "<code>diskmgmt.exe</code> and <code>diskpart.exe</code>.")
 + card("Event Viewer", "<code>eventvwr.exe</code> for system logs.")
 + '</div>')
features += sec('''
<div class="center"><span class="eyebrow">Under the hood</span><h2>A modern NT core</h2><p class="sub">The system is a fork of LakoOS, which targets Windows 11 (NT 10) compatibility on 64-bit platforms.</p></div>
<div class="bento">'''
 + card("64-bit first", "amd64, arm64 and riscv64 are the focus, with i386 kept building.")
 + card("WoW64", "32-bit applications run on 64-bit systems through WoW64.")
 + card("WDDM and DWM", "A display driver model and desktop composition for blur and effects.")
 + card("New memory manager", "A clean-room memory manager and cache manager.")
 + card("UEFI boot", "UEFI with a GOP framebuffer, plus legacy BIOS boot.")
 + card("Open source", "GNU GPL. Read it, change it and share it.")
 + '</div>', alt=True)
write("features.html", "Features — LakoOS", "What is inside LakoOS: Start menu, File Explorer, familiar tools and a modern NT core.", features)

dl = page_hero("Download", "Get LakoOS", "Pick an image and start it in a virtual machine. LakoOS is a development preview.")
dl += sec('''
<div class="dl">
<div class="dcard feat"><div class="ic">⬇</div><h3>Live ISO</h3><p>Boots directly to the desktop. Nothing is written to your disk.</p><div class="meta">LakoOS-amd64-livecd.iso · 64-bit</div><a class="btn primary" href="https://github.com/LakoMoor/LakoOS/releases/latest/download/LakoOS-amd64-livecd.iso">Download ISO</a></div>
<div class="dcard"><div class="ic">⛁</div><h3>Setup ISO</h3><p>Install to a virtual disk and keep your settings.</p><div class="meta">LakoOS-amd64-setup.iso · 64-bit</div><a class="btn ghost" href="https://github.com/LakoMoor/LakoOS/releases/latest/download/LakoOS-amd64-setup.iso">Download setup ISO</a></div>
<div class="dcard"><div class="ic">⚙</div><h3>Build it yourself</h3><p>One command prepares everything from scratch.</p><div class="meta">Linux · macOS · Windows</div><a class="btn ghost" href="docs.html#build">Build instructions</a></div>
</div>
<h2 style="margin-top:48px">ARM64</h2>
<div class="dl" style="margin-top:16px">
<div class="dcard"><div class="ic">⬇</div><h3>Live ISO (ARM64)</h3><p>For ARM64 virtual machines with UEFI, for example QEMU <code>virt</code>.</p><div class="meta">LakoOS-arm64-livecd.iso · UEFI</div><a class="btn primary" href="https://github.com/LakoMoor/LakoOS/releases/latest/download/LakoOS-arm64-livecd.iso">Download ISO</a></div>
<div class="dcard"><div class="ic">⛁</div><h3>Setup ISO (ARM64)</h3><p>Install LakoOS to a virtual ARM64 disk.</p><div class="meta">LakoOS-arm64-setup.iso · UEFI</div><a class="btn ghost" href="https://github.com/LakoMoor/LakoOS/releases/latest/download/LakoOS-arm64-setup.iso">Download setup ISO</a></div>
</div>''')
dl += sec('''
<h2>Run it</h2>
<div class="runs">
<div class="card2"><h3>QEMU</h3><div class="codebox"><button class="copy">Copy</button><pre>qemu-system-x86_64 -m 2048 -smp 4 \\
  -cdrom LakoOS-amd64-livecd.iso -boot d \\
  -vga std -usb -device usb-tablet</pre></div><p class="muted">On Linux add <code>-enable-kvm</code> for speed.</p></div>
<div class="card2"><h3>QEMU ARM64</h3><div class="codebox"><button class="copy">Copy</button><pre>qemu-system-aarch64 -M virt -cpu cortex-a72 -smp 4 -m 3072 \\
  -drive if=pflash,format=raw,readonly=on,file=AAVMF_CODE.fd \\
  -drive if=pflash,format=raw,file=vars.fd \\
  -device ramfb -device qemu-xhci -device usb-kbd -device usb-tablet \\
  -drive file=LakoOS-arm64-livecd.iso,if=none,id=cd,media=cdrom \\
  -device usb-storage,drive=cd,bootindex=0</pre></div></div>
<div class="card2"><h3>VirtualBox</h3><ol><li>New machine, type <b>Other</b>, version <b>Other/Unknown (64-bit)</b>.</li><li>2048 MB of memory, no hard disk needed for the live image.</li><li>Attach the ISO to the optical drive and start.</li></ol></div>
<div class="card2"><h3>Hyper-V</h3><ol><li>New virtual machine, <b>Generation 1</b>.</li><li>2048 MB of memory, attach the ISO as a DVD drive.</li><li>Start and connect.</li></ol></div>
</div>''', alt=True)
dl += sec('''
<h2>System requirements</h2>
<div class="req"><div><b>CPU</b><span>64-bit x86, 2 cores</span></div><div><b>Memory</b><span>2 GB RAM</span></div><div><b>Storage</b><span>1 GB live · 8 GB install</span></div><div><b>Graphics</b><span>1024×768 VGA</span></div></div>''')
write("download.html", "Download — LakoOS", "Download the LakoOS live ISO and run it in QEMU, VirtualBox or Hyper-V.", dl)

docs = page_hero("Documentation", "Learn LakoOS", "First boot, settings, building from source and where things live.")
docs += sec('''
<div class="doc">
<aside class="toc"><a href="#start">First boot</a><a href="#install">Install</a><a href="#settings">Settings</a><a href="#layout">Folder layout</a><a href="#build">Build from source</a><a href="#help">Getting help</a></aside>
<div class="prose">
<h2 id="start">First boot</h2>
<p>Start the live ISO. The desktop appears with the taskbar in the center. Open Start to see pinned apps, type to search, or open <b>This PC</b> to browse drives.</p>
<h2 id="install">Install to a disk</h2>
<p>Boot the <b>Setup ISO</b> (<code>LakoOS-amd64-setup.iso</code>) with an empty virtual disk attached. Choose a language, pick <b>Install LakoOS</b>, select the disk and follow the wizard: account, time zone, summary. After the first restart the desktop opens and signs you in automatically when no password is set.</p>
<p>Build the Setup ISO yourself with <code>./buildenv/build.sh --target bootcd</code>.</p>
<h2 id="settings">Settings</h2>
<p>The shell reads these values from <code>HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced</code>. Changes apply the next time you open Start.</p>
<table><tr><th>Value</th><th>Data</th><th>Meaning</th></tr>
<tr><td><code>ShellStyle</code></td><td>11</td><td>Centered Start (default)</td></tr>
<tr><td><code>ShellStyle</code></td><td>10</td><td>List and tiles Start</td></tr>
<tr><td><code>ShellStyle</code></td><td>7</td><td>Classic two-column Start</td></tr>
<tr><td><code>TaskbarAl</code></td><td>0 / 1</td><td>Taskbar left / center</td></tr></table>
<div class="codebox"><button class="copy">Copy</button><pre>reg add HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced /v ShellStyle /t REG_DWORD /d 10 /f
reg add HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced /v TaskbarAl /t REG_DWORD /d 0 /f</pre></div>
<h2 id="layout">Folder layout</h2>
<p>System files live in <code>Windows</code> and <code>Windows\\System32</code>. Applications go to <code>Program Files</code> and <code>Program Files (x86)</code>. Shared data is in <code>ProgramData</code>, and user profiles are under <code>Users</code>.</p>
<h2 id="build">Build from source</h2>
<p>The <code>buildenv</code> folder installs the compilers and tools and builds the image from scratch.</p>
<div class="codebox"><button class="copy">Copy</button><pre>git clone https://github.com/LakoMoor/LakoOS.git
cd LakoOS
./buildenv/build.sh                 # Linux, macOS
.\\buildenv\\build.ps1                # Windows (WSL2 or -Backend docker)</pre></div>
<p>Options: <code>--arch amd64|i386|arm64</code>, <code>--target livecd|bootcd|reactosimg|reactosvhd</code>, <code>--release</code>, <code>--env-only</code>. The first build needs about 15 GB of free disk.</p>
<h2 id="help">Getting help</h2>
<p>Report problems in <a href="https://github.com/LakoMoor/LakoOS/issues">GitHub Issues</a> with the architecture, the build type and the steps that lead to the problem.</p>
</div></div>''')
write("docs.html", "Documentation — LakoOS", "How to boot, configure and build LakoOS.", docs)

comp = page_hero("Compatibility", "What works today", "An honest status of the development preview. Statuses change as the system improves.")
rows = [("Classic Win32 desktop apps", "Many run", "ok"), ("32-bit apps (WoW64)", "Supported on 64-bit builds", "ok"),
        ("Command line tools", "Work", "ok"), ("Windows drivers (NT style)", "Partial", "mid"),
        (".NET applications", "Not verified", "no"), ("Browsers (Chromium based)", "Not verified", "no"),
        ("UWP and WinUI apps", "Not supported yet", "no"), ("DirectX 11 / 12 games", "Experimental", "mid"),
        ("Networking and Wi-Fi", "Partial", "mid"), ("Installer to disk", "Works in a VM", "ok")]
tbl = "".join(f'<tr><td>{a}</td><td><span class="st {c}">{b}</span></td></tr>' for a, b, c in rows)
comp += sec(f'<table class="compat"><tr><th>Area</th><th>Status</th></tr>{tbl}</table><p class="muted" style="margin-top:20px">Found a program that does not work? Please <a href="https://github.com/LakoMoor/LakoOS/issues">open an issue</a> with its name and version.</p>')
write("compatibility.html", "Compatibility — LakoOS", "Which applications and features work in LakoOS today.", comp)

about = page_hero("About", "A free NT, with credit where it is due", "LakoOS is an independent fork. It builds on the work of many people.")
about += sec('''
<div class="doc"><aside class="toc"><a href="#what">What it is</a><a href="#lineage">Lineage</a><a href="#license">License</a><a href="#trademarks">Trademarks</a></aside>
<div class="prose">
<h2 id="what">What it is</h2><p>LakoOS is a free operating system on the NT architecture with a familiar desktop, maintained by <a href="https://github.com/LakoMoor">LakoMoor</a>.</p>
<h2 id="lineage">Lineage</h2><p>LakoOS is a fork of <a href="https://github.com/ahmedarif193/LakoOS">LakoOS</a>, which is a fork of ReactOS. Their authors are credited in <a href="https://github.com/LakoMoor/LakoOS/blob/main/CREDITS.md">CREDITS.md</a>, and their copyright notices stay in the source files.</p>
<h2 id="license">License</h2><p>GNU GPL 3.0. Code inherited from ReactOS keeps the license stated in its file headers, mostly GPL 2.0 or later. The full texts are in the repository.</p>
<h2 id="trademarks">Trademarks</h2><p>Windows is a trademark of Microsoft Corporation. LakoOS is not affiliated with, endorsed by or sponsored by Microsoft.</p>
</div></div>''')
write("about.html", "About — LakoOS", "About the LakoOS project, its lineage and license.", about)
print("ok")
