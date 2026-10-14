# System and driver updates

`lakoupdate.exe` (in `System32`) checks a feed, downloads packages, verifies them and installs
them. It works like a small Windows Update client.

    lakoupdate                       check for updates
    lakoupdate install               download and install everything pending
    lakoupdate history               show the update log
    lakoupdate /feed <url|path> ...  set the feed (saved in the registry)

Default feed: `https://github.com/LakoMoor/LakoOS/releases/latest/download/update.ini`
(registry value `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\FeedUrl`).

## How it behaves

- Packages are `file` (copied to a destination) or `driver` (`.inf` installed with `DiInstallDriver`).
- Every file needs a SHA-256 in the feed. A file with no hash, or a wrong hash, is discarded.
- The previous file is copied to `SoftwareDistribution\Backup`. A file that is in use is staged as
  `<name>.new` and replaced at the next restart.
- Installed package IDs are remembered under `...\WindowsUpdate\Installed`; the log is
  `SoftwareDistribution\history.log`.

## Making a feed

`tools/make-update-feed.py out/update.ini https://host/path spec.txt` hashes the files and writes the
manifest. Upload `update.ini` and the files to the same place.

## Not done

- The feed itself is not signed; trust relies on HTTPS to the feed host. Signed manifests are needed
  before this is used for real distribution.
- No background service, scheduling, Settings page or restart prompt yet. It is a command line tool.
- No delta packages, no rollback command (backups are kept by hand-restore only).
- No driver ranking by hardware ID; a `driver` package installs the INF it carries.
