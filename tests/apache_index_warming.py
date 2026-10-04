#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

"""Exercise the real MPM, background walk and persistent snapshots in /tmp."""

import argparse
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import time
import urllib.request


def wait_for(predicate, message, process, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"Apache exited ({process.returncode}): {message}")
        result = predicate()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError(message)


def snapshots(cache):
    result = {}
    for path in cache.glob("*.cache"):
        data = path.read_bytes()
        if data.startswith(b"GSTIDX01"):
            length = struct.unpack_from("<I", data, 12)[0]
            directory = data[16:16 + length].decode()
            result[directory] = path
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--httpd", required=True)
    parser.add_argument("--modules", required=True)
    parser.add_argument("--module", required=True)
    parser.add_argument("--fixture", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="geist-apache-test-") as name:
        root = Path(name)
        # Tests normally run as a developer, but remain usable in root CI.
        root.chmod(0o755)
        www, cache = root / "www", root / "cache"
        cache.mkdir(mode=0o750)
        if os.geteuid() == 0:
            import pwd
            user = pwd.getpwnam("www-data" if Path("/etc/debian_version").exists() else "apache")
            uid, gid = user.pw_uid, user.pw_gid
            os.chown(cache, uid, gid)
        else:
            uid, gid = os.getuid(), os.getgid()
        directories = [www, www / "sub", www / "off", www / "off" / "reon",
                       www / "off" / "ht", www / "off" / "vhost",
                       www / "regex-off", www / "conditional",
                       root / "external", root / "wild" / "books"]
        for directory in directories:
            directory.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.fixture, directory / "packet.boo")
        (www / "off" / "ht" / ".htaccess").write_text("BooIndex On\n")
        (www / "loop").symlink_to(www, target_is_directory=True)
        (www / "sub" / "loop").symlink_to(www, target_is_directory=True)
        runtime = root / "run"
        runtime.mkdir()
        if os.geteuid() == 0:
            os.chown(runtime, uid, gid)
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        log = root / "error.log"
        config = root / "httpd.conf"
        modules = Path(args.modules)
        config.write_text(f'''
ServerRoot "{root}"
DefaultRuntimeDir "{runtime}"
PidFile "{root / 'pid'}"
ErrorLog "{log}"
LogLevel info
ServerName localhost
Listen 127.0.0.1:{port}
LoadModule mpm_event_module "{modules / 'mod_mpm_event.so'}"
LoadModule authz_core_module "{modules / 'mod_authz_core.so'}"
LoadModule dir_module "{modules / 'mod_dir.so'}"
LoadModule geist_module "{Path(args.module).resolve()}"
User #{uid}
Group #{gid}
StartServers 2
ServerLimit 2
ThreadsPerChild 4
ThreadLimit 4
MinSpareThreads 4
MaxSpareThreads 8
MaxRequestWorkers 8
DocumentRoot "{www}"
GeistCacheDir "{cache}"
<Directory "{root}">
    Require all granted
    AllowOverride FileInfo
    Options FollowSymLinks
</Directory>
<Directory "{www}">
    BooIndex On
</Directory>
<Directory "{www / 'off'}">
    BooIndex Off
</Directory>
<Directory "{www / 'off' / 'reon'}">
    BooIndex On
</Directory>
<Directory "{root / 'external'}">
    BooIndex On
</Directory>
<Directory "{root / 'wild'}/*">
    BooIndex On
</Directory>
<DirectoryMatch "^{www / 'regex-off'}/?$">
    BooIndex Off
</DirectoryMatch>
<Directory "{www / 'conditional'}">
    <If "%{{REQUEST_METHOD}} == 'GET'">
        BooIndex Off
    </If>
</Directory>
<VirtualHost 127.0.0.1:{port}>
    ServerName localhost
    DocumentRoot "{www}"
    <Directory "{www / 'off' / 'vhost'}">
        BooIndex On
    </Directory>
</VirtualHost>
''')
        excluded = {www / "off", www / "regex-off", www / "conditional"}
        expected = {str(path) for path in directories if path not in excluded}
        book_count = len(expected)
        console = root / "console.log"
        process = None

        def start():
            return subprocess.Popen([args.httpd, "-f", str(config), "-DFOREGROUND"],
                                    stdout=output, stderr=output, start_new_session=True)

        def stop():
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                    raise AssertionError("Apache did not stop its background thread")

        def text():
            return log.read_text(errors="replace") if log.exists() else ""

        with console.open("wb") as output:
            try:
                process = start()
                wait_for(lambda: set(snapshots(cache)) == expected,
                         "automatic walk did not discover enabled shelves before any request", process)
                wait_for(lambda: text().count("mod_geist: warmed") == 3,
                         "walks were duplicated or not completed", process)
                first_text = text()
                assert sum(map(int, re.findall(r"(\d+) probe\(s\)", first_text))) == book_count, first_text
                before = {directory: path.stat().st_mtime_ns for directory, path in snapshots(cache).items()}
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=10) as response:
                    assert response.status == 200
                    html = response.read().decode()
                    assert "packet.boo" in html and "Book Index" in html
                    etag = response.headers["ETag"]
                stop()
                previous_log = len(text())
                process = start()
                wait_for(lambda: text()[previous_log:].count("mod_geist: warmed") == 3,
                         "restart did not finish walking", process)
                restart = text()[previous_log:]
                assert sum(map(int, re.findall(r"(\d+) disk hit\(s\)", restart))) == book_count, restart
                assert sum(map(int, re.findall(r"(\d+) probe\(s\)", restart))) == 0, restart
                assert before == {directory: path.stat().st_mtime_ns for directory, path in snapshots(cache).items()}
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=10) as response:
                    assert response.headers["ETag"] == etag
                previous_log = len(text())
                process.send_signal(signal.SIGUSR1)
                wait_for(lambda: text()[previous_log:].count("mod_geist: warmed") == 3,
                         "graceful reload did not warm the new generation", process)
                reload_text = text()[previous_log:]
                assert sum(map(int, re.findall(r"(\d+) disk hit\(s\)", reload_text))) == book_count, reload_text
                assert sum(map(int, re.findall(r"(\d+) probe\(s\)", reload_text))) == 0, reload_text
                # Replacing a source with preserved size/mtime must still
                # invalidate its identity and the rendered shelf's ETag.
                original = www / "packet.boo"
                stamp = original.stat()
                replacement = www / "new.tmp"
                shutil.copyfile(original, replacement)
                os.utime(replacement, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
                replacement.replace(original)
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=10) as response:
                    assert response.headers["ETag"] != etag
                stop()
                # A corrupt disk snapshot causes one probe, with the other
                # shelves still using their cached metadata.
                path = snapshots(cache)[str(www)]
                path.write_bytes(path.read_bytes()[:20])
                previous_log = len(text())
                process = start()
                wait_for(lambda: text()[previous_log:].count("mod_geist: warmed") == 3,
                         "corrupt cache recovery did not finish", process)
                recovery = text()[previous_log:]
                assert sum(map(int, re.findall(r"(\d+) probe\(s\)", recovery))) == 1, recovery
                assert sum(map(int, re.findall(r"(\d+) disk hit\(s\)", recovery))) == book_count - 1, recovery
                stop()
                config.write_text(config.read_text().replace(
                    f'GeistCacheDir "{cache}"', f'GeistCacheDir "{root / "missing-cache"}"'))
                previous_log = len(text())
                process = start()
                wait_for(lambda: "background warming skipped" in text()[previous_log:],
                         "unavailable cache did not degrade gracefully", process)
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=10) as response:
                    assert response.status == 200 and b"packet.boo" in response.read()
                print("automatic walk, exclusions, .htaccess, election, restart/reload reuse, corruption recovery and cache fallback passed")
            except Exception:
                print(text())
                print(console.read_text(errors="replace"))
                raise
            finally:
                if process is not None:
                    stop()


if __name__ == "__main__":
    main()
