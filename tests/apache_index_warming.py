#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

"""Exercise HTTP caching, the real MPM and persistent snapshots in /tmp."""

import argparse
import hashlib
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
import urllib.error
import urllib.parse


def fetch(base, path, headers=None, method="GET"):
    request = urllib.request.Request(base + path, headers=headers or {}, method=method)
    try:
        response = urllib.request.urlopen(request, timeout=15)
    except urllib.error.HTTPError as response:
        return response.code, response.headers, response.read()
    with response:
        return response.status, response.headers, response.read()


def check_http_cache(base):
    tags = {}
    for path in ["/packet.boo", "/packet.boo/topic/1.3",
                 "/packet.boo/object/1", "/packet.boo/download",
                 "/packet.boo/asset/book.css", "/"]:
        status, headers, body = fetch(base, path)
        assert status == 200 and body, (path, status)
        tag = headers.get("ETag")
        assert tag and tag.startswith('W/"'), (path, headers)
        assert "public" in headers["Cache-Control"]
        assert "s-maxage=" in headers["Cache-Control"]
        assert "no-cache" not in headers["Cache-Control"]
        tags[path] = tag
        status, conditional, body = fetch(base, path, {"If-None-Match": tag})
        assert status == 304 and not body, (path, status)
        assert conditional["ETag"] == tag
        assert conditional["Cache-Control"] == headers["Cache-Control"]
        assert fetch(base, path, {"If-None-Match": tag[2:]})[0] == 304
        assert fetch(base, path, {"If-None-Match": tag}, method="HEAD")[0] == 304
        assert fetch(base, path, {"If-None-Match": '"stale"'})[0] == 200
        assert fetch(base, path, {"If-None-Match": '"stale", ' + tag})[0] == 304
        assert fetch(base, path, {"If-None-Match": "*"})[0] == 304
        failure, failed_headers, _ = fetch(base, path, {"If-Match": '"stale"'})
        assert failure == 412 and "Cache-Control" not in failed_headers
        head_status, head_headers, head_body = fetch(base, path, method="HEAD")
        assert head_status == 200 and not head_body and head_headers["ETag"] == tag
        if not path.endswith("/download"):
            assert "Last-Modified" not in headers
            assert fetch(base, path, {"If-Modified-Since": "Wed, 01 Jan 2031 00:00:00 GMT"})[0] == 200
        else:
            assert fetch(base, path, {"If-Modified-Since": headers["Last-Modified"]})[0] == 304
        # A stale ETag must win over an apparently unchanged timestamp.
        assert fetch(base, path, {"If-None-Match": '"stale"',
                                 "If-Modified-Since": "Wed, 01 Jan 2031 00:00:00 GMT"})[0] == 200
    assert len(set(tags.values())) == len(tags), tags
    for path in ["/packet.boo/topic/absent", "/packet.boo/object/absent",
                 "/packet.boo/asset/absent"]:
        status, headers, _ = fetch(base, path, {"If-None-Match": "*"})
        assert status == 404 and "ETag" not in headers, (path, status, headers)
    status, _, html = fetch(base, "/packet.boo/topic/1.3")
    assets = re.findall(r'(?:href|src)="([^"]+/asset/[^\"]+)"', html.decode())
    assert assets and all("?v=" in url for url in assets), assets
    for asset in assets:
        status, _, content = fetch(base, asset)
        fingerprint = urllib.parse.parse_qs(urllib.parse.urlsplit(asset).query)["v"][0]
        assert status == 200 and hashlib.sha256(content).hexdigest() == fingerprint
    return tags


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


def check_navigation(base, book):
    """Every sidebar topic participates in source-order toolbar navigation."""
    status, _, body = fetch(base, book)
    assert status == 200
    sidebar = body.decode().split('<ol id="geist-toc-list">', 1)[1].split('</ol>', 1)[0]
    topics = re.findall(r'<a href="([^"]+)"', sidebar)
    assert topics and len(topics) == len(set(topics)), topics
    for index, topic in enumerate(topics):
        status, _, body = fetch(base, topic)
        assert status == 200, (topic, status)
        toolbar = body.decode().split('<header class="geist-bar">', 1)[1].split('</header>', 1)[0]
        if topic.lower() != topic:
            alias_status, _, alias_body = fetch(base, topic.lower())
            alias_toolbar = alias_body.decode().split('<header class="geist-bar">', 1)[1].split('</header>', 1)[0]
            assert alias_status == 200 and alias_toolbar == toolbar, topic
        for label, neighbor in [('Previous topic', index - 1), ('Next topic', index + 1)]:
            link = re.search(r'<a class="geist-btn" href="([^"]+)" title="' + label + '"', toolbar)
            if 0 <= neighbor < len(topics):
                assert link and link[1] == topics[neighbor], (topic, label, topics[neighbor], toolbar)
            else:
                assert not link and 'aria-disabled="true" title="' + label + '"' in toolbar, (topic, label)
    status, _, body = fetch(base, book)
    toolbar = body.decode().split('<header class="geist-bar">', 1)[1].split('</header>', 1)[0]
    assert status == 200
    for label in ['Previous topic', 'Next topic']:
        assert 'aria-disabled="true" title="' + label + '"' in toolbar


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
    parser.add_argument("--navigation-book", help="Optional local book with topics omitted from its printed contents")
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
        # Keep a book whose first GDF object has an invalid extent outside the
        # shelves. HEAD and a matching conditional request must answer from
        # metadata without trying to decode that malformed object.
        malformed = www / "off" / "packet.boo"
        malformed_bytes = bytearray(malformed.read_bytes())
        gdf_offset = 224714
        assert malformed_bytes[gdf_offset:gdf_offset + 4] == bytes.fromhex("01120004")
        malformed_bytes[gdf_offset + 4:gdf_offset + 20] = bytes(16)
        malformed.write_bytes(malformed_bytes)
        if args.navigation_book:
            shutil.copyfile(args.navigation_book, www / "navigation.boo")
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
        book_count = len(expected) + bool(args.navigation_book)
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
                base = f"http://127.0.0.1:{port}"
                tags = check_http_cache(base)
                malformed_object = "/off/packet.boo/object/1"
                status, headers, body = fetch(base, malformed_object, method="HEAD")
                assert status == 200 and not body
                malformed_tag = headers["ETag"]
                status, _, body = fetch(
                    base, malformed_object, {"If-None-Match": malformed_tag})
                assert status == 304 and not body
                assert fetch(base, malformed_object)[0] == 404
                check_navigation(base, "/packet.boo")
                if args.navigation_book:
                    check_navigation(base, "/navigation.boo")
                assert "private" in fetch(base, "/packet.boo/topic/1.3",
                    {"Authorization": "Bearer test"})[1]["Cache-Control"]
                title = www / ".title"
                title.write_text("Shelf A\n")
                title_stamp = title.stat()
                first_title_tag = fetch(base, "/")[1]["ETag"]
                title.write_text("Shelf B\n")
                os.utime(title, ns=(title_stamp.st_atime_ns, title_stamp.st_mtime_ns))
                status, title_headers, title_body = fetch(base, "/")
                assert status == 200 and b"Shelf B" in title_body
                assert title_headers["ETag"] != first_title_tag
                title.unlink()
                overrides = www / ".htaccess"
                overrides.write_text("GeistTheme dark\n")
                for path in ["/packet.boo/topic/1.3", "/"]:
                    status, changed_headers, changed_body = fetch(base, path)
                    assert status == 200 and b'data-theme="dark"' in changed_body
                    assert changed_headers["ETag"] != tags[path]
                overrides.unlink()
                for path in ["/packet.boo/topic/1.3", "/"]:
                    assert fetch(base, path)[1]["ETag"] == tags[path]
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
                assert fetch(base, "/packet.boo/topic/1.3")[1]["ETag"] == tags["/packet.boo/topic/1.3"]
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
                assert fetch(base, "/packet.boo/topic/1.3")[1]["ETag"] != tags["/packet.boo/topic/1.3"]
                stop()
                # A corrupt disk snapshot reprobes that shelf's books, with
                # the other shelves still using their cached metadata.
                path = snapshots(cache)[str(www)]
                path.write_bytes(path.read_bytes()[:20])
                previous_log = len(text())
                process = start()
                wait_for(lambda: text()[previous_log:].count("mod_geist: warmed") == 3,
                         "corrupt cache recovery did not finish", process)
                recovery = text()[previous_log:]
                recovered_books = 1 + bool(args.navigation_book)
                assert sum(map(int, re.findall(r"(\d+) probe\(s\)", recovery))) == recovered_books, recovery
                assert sum(map(int, re.findall(r"(\d+) disk hit\(s\)", recovery))) == book_count - recovered_books, recovery
                tags = {path: fetch(base, path)[1]["ETag"]
                        for path in ["/packet.boo/topic/1.3", "/packet.boo", "/"]}
                stop()
                config.write_text(config.read_text().replace(
                    f'GeistCacheDir "{cache}"', f'GeistCacheDir "{root / "missing-cache"}"'))
                config.write_text(config.read_text() + '\nGeistTheme dark\n')
                previous_log = len(text())
                process = start()
                wait_for(lambda: "background warming skipped" in text()[previous_log:],
                         "unavailable cache did not degrade gracefully", process)
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=10) as response:
                    assert response.status == 200 and b"packet.boo" in response.read()
                for path in ["/packet.boo/topic/1.3", "/packet.boo", "/"]:
                    status, headers, body = fetch(base, path)
                    assert status == 200 and b'data-theme="dark"' in body
                    assert headers["ETag"] != tags[path]
                print("Topic navigation, HTTP validators, asset fingerprints, config/file invalidation, automatic walk, exclusions, election, restart/reload reuse, corruption recovery and cache fallback passed")
            except Exception:
                print(text())
                print(console.read_text(errors="replace"))
                raise
            finally:
                if process is not None:
                    stop()


if __name__ == "__main__":
    main()
