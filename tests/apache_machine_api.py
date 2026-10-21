#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

"""Exercise the opt-in, BooIndex-scoped REST/OpenAPI and MCP endpoints."""

import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


MCP_VERSION = "2026-07-28"
LEGACY_MCP_VERSION = "2025-11-25"


def fetch(base, path, headers=None, method="GET", body=None):
    request = urllib.request.Request(
        base + path, data=body, headers=headers or {}, method=method)
    try:
        response = urllib.request.urlopen(request, timeout=15)
    except urllib.error.HTTPError as response:
        return response.code, response.headers, response.read()
    with response:
        return response.status, response.headers, response.read()


def json_post(base, path, value, headers=None):
    request_headers = {
        "Accept": "application/json, text/event-stream",
        "Content-Type": "application/json",
    }
    request_headers.update(headers or {})
    return fetch(base, path, request_headers, "POST",
                 json.dumps(value, separators=(",", ":")).encode())


def decode_json(response):
    status, headers, body = response
    assert headers.get_content_type() == "application/json", headers
    try:
        return status, headers, json.loads(body)
    except json.JSONDecodeError as error:
        raise AssertionError((status, headers, body)) from error


def wait_for_http(base, process, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"Apache exited with status {process.returncode}")
        try:
            fetch(base, "/")
            return
        except urllib.error.URLError:
            time.sleep(0.05)
    raise AssertionError("Apache did not start accepting HTTP requests")


def assert_not_found(base, prefix):
    for suffix in ["/.geist/api/v1", "/.geist/openapi.json",
                   "/.geist/api/v1/books", "/.geist/mcp"]:
        status, _, _ = fetch(base, prefix + suffix)
        assert status == 404, (prefix + suffix, status)


def check_rest(base):
    status, _, _ = fetch(base, "/enabled/.geist")
    assert status == 404, status

    status, _, service = decode_json(fetch(base, "/enabled/.geist/api/v1"))
    assert status == 200 and isinstance(service, dict), service

    status, _, schema = decode_json(fetch(base, "/enabled/.geist/openapi.json"))
    assert status == 200
    assert schema.get("openapi") == "3.1.2", schema
    paths = schema.get("paths", {})
    assert "/.geist/api/v1" in paths, paths
    assert "/.geist/api/v1/books" in paths, paths

    status, _, books = decode_json(fetch(base, "/enabled/.geist/api/v1/books"))
    assert status == 200
    entries = books.get("books") if isinstance(books, dict) else books
    assert isinstance(entries, list), books
    assert any(entry.get("id") == "packet.boo" for entry in entries), entries


def rpc_request(method, request_id, params=None):
    value = {"jsonrpc": "2.0", "id": request_id, "method": method}
    if params is not None:
        value["params"] = params
    return value


def check_mcp(base):
    path = "/enabled/.geist/mcp"
    status, headers, _ = fetch(base, path)
    assert status == 405, status
    assert headers.get("Allow") == "POST", headers

    # The 2025-11-25 compatibility lifecycle negotiates with initialize and
    # then identifies subsequent requests with MCP-Protocol-Version.
    initialize = rpc_request("initialize", 1, {
        "protocolVersion": LEGACY_MCP_VERSION,
        "capabilities": {},
        "clientInfo": {"name": "mod_geist-test", "version": "1"},
    })
    status, _, response = decode_json(json_post(base, path, initialize))
    assert status == 200, response
    assert response.get("jsonrpc") == "2.0" and response.get("id") == 1, response
    result = response.get("result", {})
    assert result.get("protocolVersion") == LEGACY_MCP_VERSION, result
    assert isinstance(result.get("serverInfo"), dict), result
    assert isinstance(result.get("capabilities", {}).get("tools"), dict), result

    initialized = {"jsonrpc": "2.0", "method": "notifications/initialized"}
    status, _, body = json_post(base, path, initialized, {
        "MCP-Protocol-Version": LEGACY_MCP_VERSION,
    })
    assert status == 202 and not body, (status, body)

    legacy_tools = rpc_request("tools/list", 2, {})
    status, _, response = decode_json(json_post(
        base, path, legacy_tools,
        {"MCP-Protocol-Version": LEGACY_MCP_VERSION}))
    assert status == 200, response
    assert response.get("jsonrpc") == "2.0" and response.get("id") == 2, response
    names = {tool.get("name") for tool in response.get("result", {}).get("tools", [])}
    assert names == {"list_books", "get_book", "list_topics", "get_topic",
                     "list_resources", "get_resource"}, names

    # In 2026-07-28, each request is self-describing. The HTTP method header
    # mirrors the JSON-RPC method and _meta carries client/protocol metadata.
    modern_params = {
        "_meta": {
            "io.modelcontextprotocol/protocolVersion": MCP_VERSION,
            "io.modelcontextprotocol/clientInfo": {
                "name": "mod_geist-test", "version": "1"},
            "io.modelcontextprotocol/clientCapabilities": {},
        },
    }
    tools = rpc_request("tools/list", 3, modern_params)
    modern_headers = {
        "MCP-Protocol-Version": MCP_VERSION,
        "Mcp-Method": "tools/list",
    }
    status, _, response = decode_json(json_post(base, path, tools, modern_headers))
    assert status == 200, response
    assert response.get("jsonrpc") == "2.0" and response.get("id") == 3, response

    status, _, response = decode_json(json_post(base, path, tools, {
        **modern_headers,
        "Origin": base,
    }))
    assert status == 200 and response.get("id") == 3, response

    # Origin is an HTTP security boundary, independent of JSON-RPC validity.
    status, _, _ = json_post(base, path, tools, {
        **modern_headers,
        "Origin": "https://attacker.invalid",
    })
    assert status == 403, status

    for headers in [
            {},
            {"MCP-Protocol-Version": MCP_VERSION},
            {"MCP-Protocol-Version": MCP_VERSION,
             "Mcp-Method": "resources/list"},
            {"MCP-Protocol-Version": "2026-01-01",
             "Mcp-Method": "tools/list"}]:
        status, _, error = decode_json(json_post(base, path, tools, headers))
        assert status == 400, (headers, status, error)
        assert error.get("error", {}).get("code") == -32020, error

    status, _, body = fetch(base, path, {
        "Accept": "application/json",
        "Content-Type": "application/json",
        **modern_headers,
    }, "POST", json.dumps(tools).encode())
    assert status == 406 and not body, (status, body)

    incomplete = rpc_request("tools/list", 5, {
        "_meta": {
            "io.modelcontextprotocol/protocolVersion": MCP_VERSION,
        },
    })
    status, _, error = decode_json(json_post(base, path, incomplete, modern_headers))
    assert status == 400, (status, error)
    assert error.get("error", {}).get("code") == -32020, error

    unknown = rpc_request("geist/absent", 4, modern_params)
    status, _, error = decode_json(json_post(base, path, unknown, {
        "MCP-Protocol-Version": MCP_VERSION,
        "Mcp-Method": "geist/absent",
    }))
    assert status == 404, (status, error)
    assert error.get("error", {}).get("code") == -32601, error

    encoded = json.dumps(tools, separators=(",", ":")).encode()
    status, _, _ = fetch(base, path, {
        "Accept": "application/json, text/event-stream",
        **modern_headers,
        "Content-Type": "text/plain",
    }, "POST", encoded)
    assert status == 415, status


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--httpd", required=True)
    parser.add_argument("--modules", required=True)
    parser.add_argument("--module", required=True)
    parser.add_argument("--fixture", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="geist-machine-api-test-") as name:
        root = Path(name)
        root.chmod(0o755)
        www = root / "www"
        directories = [
            www / "default",
            www / "api-only",
            www / "mcp-only",
            www / "enabled",
            www / "enabled" / "disabled-child",
        ]
        for directory in directories:
            directory.mkdir(parents=True)
            shutil.copyfile(args.fixture, directory / "packet.boo")

        if os.geteuid() == 0:
            import pwd
            user = pwd.getpwnam(
                "www-data" if Path("/etc/debian_version").exists() else "apache")
            uid, gid = user.pw_uid, user.pw_gid
        else:
            uid, gid = os.getuid(), os.getgid()
        runtime = root / "run"
        runtime.mkdir()
        if os.geteuid() == 0:
            os.chown(runtime, uid, gid)

        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        base = f"http://127.0.0.1:{port}"
        log = root / "error.log"
        console = root / "console.log"
        modules = Path(args.modules)
        config = root / "httpd.conf"
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
StartServers 1
ServerLimit 1
ThreadsPerChild 4
ThreadLimit 4
MinSpareThreads 4
MaxSpareThreads 4
MaxRequestWorkers 4
DocumentRoot "{www}"
<Directory "{root}">
    Require all granted
</Directory>
<Directory "{www / 'default'}">
    BooIndex On
</Directory>
<Directory "{www / 'api-only'}">
    GeistAPI On
</Directory>
<Directory "{www / 'mcp-only'}">
    GeistMCP On
</Directory>
<Directory "{www / 'enabled'}">
    BooIndex On
    GeistAPI On
    GeistMCP On
</Directory>
<Directory "{www / 'enabled' / 'disabled-child'}">
    BooIndex Off
</Directory>
''')

        process = None
        with console.open("wb") as output:
            try:
                process = subprocess.Popen(
                    [args.httpd, "-f", str(config), "-DFOREGROUND"],
                    stdout=output, stderr=output, start_new_session=True)
                wait_for_http(base, process)

                assert_not_found(base, "/default")
                assert_not_found(base, "/api-only")
                assert_not_found(base, "/mcp-only")
                assert_not_found(base, "/enabled/disabled-child")
                check_rest(base)
                check_mcp(base)
                print("BooIndex-gated REST/OpenAPI and MCP endpoint checks passed")
            except Exception:
                if log.exists():
                    print(log.read_text(errors="replace"))
                print(console.read_text(errors="replace"))
                raise
            finally:
                if process is not None and process.poll() is None:
                    process.send_signal(signal.SIGTERM)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
                        raise AssertionError("Apache did not stop")


if __name__ == "__main__":
    main()
