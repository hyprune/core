#!/usr/bin/env python3
"""Source helper prototype: publishes command, HTTP and timer sources with data.publish.

A development tool for the live-objects (machines) prototype. It runs out of process,
connects to core's IPC socket as `com.hyprune.sources` with only the `data.publish`
grant, and publishes one source per configured fetch. See README.md in this directory.
Python 3 standard library only.
"""
import argparse
import datetime
import ipaddress
import json
import logging
import math
import os
import pathlib
import queue
import random
import re
import selectors
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

log = logging.getLogger("source-helper")

DEFAULT_CLIENT_ID = "com.hyprune.sources"
DEFAULT_CLIENT_NAME = "Source helper (prototype)"
DEFAULT_PROTOCOL = "0.2"  # the first IPC version with data.publish
ID_RE = re.compile(r"[a-z][a-z0-9-]*(\.[a-z][a-z0-9-]*)+")
SLUG_RE = re.compile(r"[a-z][a-z0-9-]*")

# data.publish limits enforced by core (IPC 0.2+).
MAX_KEYS, MAX_KEY_BYTES, MAX_STRING_BYTES, MAX_VALUES_BYTES = 32, 128, 256, 16384
MAX_SOURCES, MIN_PUBLISH_GAP_S = 32, 0.1
TTL_MIN_MS, TTL_MAX_MS = 100, 60000

# Helper floors (RFC-0013 draft, "Limits").
COMMAND_MIN_INTERVAL_S, COMMAND_DEFAULT_INTERVAL_S = 5.0, 60.0
COMMAND_TIMEOUT_RANGE_S, COMMAND_DEFAULT_TIMEOUT_S = (1.0, 60.0), 10.0
HTTP_MIN_INTERVAL_S, HTTP_LOOPBACK_MIN_INTERVAL_S, HTTP_DEFAULT_INTERVAL_S = 10.0, 2.0, 60.0
HTTP_TIMEOUT_RANGE_S, HTTP_DEFAULT_TIMEOUT_S = (1.0, 30.0), 10.0
HTTP_JITTER = 0.10
DEV_MIN_INTERVAL_S = 1.0  # devFastIntervals / --dev: prototype simulators only
TIMER_MIN_INTERVAL_S, TIMER_DEFAULT_INTERVAL_S = 1.0, 1.0
STDOUT_CAP, STDERR_KEEP, HTTP_BODY_CAP = 64 * 1024, 4 * 1024, 1024 * 1024
BACKOFF_CAP_S = 600.0
CONCURRENCY = 4
RECONNECT_CAP_S = 60.0
FIELD_TYPES = {"string", "number", "int", "bool", "counter"}
OUTPUTS = {"json", "number", "text", "lines"}
TOKEN_RE = re.compile(r"[a-f0-9]{64}")


class ConfigError(Exception):
    pass


class PollError(Exception):
    """A failed fetch: keeps the last good values and backs off."""

    def __init__(self, message, meta=None, retry_after=None):
        super().__init__(message)
        self.meta = meta or {}
        self.retry_after = retry_after


class FieldError(Exception):
    pass


class RpcError(Exception):
    def __init__(self, code, message):
        super().__init__(f"{code} {message}")
        self.code, self.message = code, message


# --- values -------------------------------------------------------------------------

def is_scalar(value):
    if isinstance(value, bool) or isinstance(value, str):
        return True
    if isinstance(value, int):
        return True
    return isinstance(value, float) and math.isfinite(value)


def truncate_utf8(text, limit):
    data = text.encode("utf-8")
    if len(data) <= limit:
        return text
    return data[:limit].decode("utf-8", "ignore")


def clean_values(values, limit=MAX_KEYS, label="source"):
    """Keep scalar values only, at most `limit` keys; strings cut to 256 bytes."""
    out = {}
    for key, value in values.items():
        if not isinstance(key, str) or not key or len(key.encode()) > MAX_KEY_BYTES:
            log.warning("%s: dropped key %.40r (empty or over %d bytes)", label, key, MAX_KEY_BYTES)
            continue
        if key.startswith("_"):
            log.warning("%s: dropped key %r (keys starting with _ are reserved)", label, key)
            continue
        if not is_scalar(value):
            log.warning("%s: dropped non-scalar value for %r", label, key)
            continue
        if len(out) >= limit:
            log.warning("%s: dropped %r and later keys (at most %d values)", label, key, limit)
            break
        out[key] = truncate_utf8(value, MAX_STRING_BYTES) if isinstance(value, str) else value
    return out


def values_size(values):
    return len(json.dumps(values, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))


# --- JSON pointers and fields ------------------------------------------------------------

_MISSING = object()


def json_pointer(document, pointer):
    """Resolve an RFC 6901 JSON Pointer. Raises FieldError when it does not resolve."""
    if pointer == "":
        return document
    if not isinstance(pointer, str) or not pointer.startswith("/"):
        raise FieldError(f"invalid JSON pointer {pointer!r}")
    node = document
    for raw in pointer[1:].split("/"):
        token = raw.replace("~1", "/").replace("~0", "~")
        if isinstance(node, dict):
            if token not in node:
                raise FieldError(f"{pointer}: no member {token!r}")
            node = node[token]
        elif isinstance(node, list):
            if not re.fullmatch(r"0|[1-9][0-9]*", token):
                raise FieldError(f"{pointer}: {token!r} is not an array index")
            index = int(token)
            if index >= len(node):
                raise FieldError(f"{pointer}: index {index} out of range")
            node = node[index]
        else:
            raise FieldError(f"{pointer}: cannot descend into a scalar")
    return node


def _array(document, pointer):
    node = json_pointer(document, pointer)
    if not isinstance(node, list):
        raise FieldError(f"{pointer}: not an array")
    return node


def _matches(element, test):
    try:
        return json_pointer(element, test.get("pointer", "")) == test["equals"]
    except FieldError:
        return False


def select_field(document, field):
    """The raw value a field selects (pointer, or a length/countWhere/first aggregate)."""
    if "pointer" in field:
        return json_pointer(document, field["pointer"])
    if "length" in field:
        return len(_array(document, field["length"]))
    if "countWhere" in field:
        test = field["countWhere"]
        return sum(1 for e in _array(document, test["array"]) if _matches(e, test))
    if "first" in field:
        test = field["first"]
        for element in _array(document, test["array"]):
            if "equals" not in test or _matches(element, test):
                return json_pointer(element, test.get("select", ""))
        raise FieldError(f"{test['array']}: no element matched")
    raise FieldError("no selection")


def coerce(value, kind, name):
    """Apply a field type. Raises FieldError on a mismatch."""
    if not is_scalar(value):
        raise FieldError(f"{name}: value is not a scalar")
    if kind is None:
        return value
    if kind == "string":
        if isinstance(value, bool):
            return "true" if value else "false"
        return truncate_utf8(value if isinstance(value, str) else json.dumps(value), MAX_STRING_BYTES)
    if kind == "bool":
        if isinstance(value, bool):
            return value
        if isinstance(value, str) and value.lower() in ("true", "false"):
            return value.lower() == "true"
        if isinstance(value, (int, float)):
            return value != 0
        raise FieldError(f"{name}: {value!r} is not a bool")
    if isinstance(value, bool):
        raise FieldError(f"{name}: {value!r} is not a number")
    if isinstance(value, str):
        try:
            value = float(value.strip())
        except ValueError:
            raise FieldError(f"{name}: {value[:40]!r} is not a number") from None
        if not math.isfinite(value):
            raise FieldError(f"{name}: not a finite number")
    if kind == "number":
        return int(value) if isinstance(value, float) and value.is_integer() and abs(value) < 2**53 else value
    if kind in ("int", "counter"):
        if isinstance(value, float):
            if not value.is_integer():
                if kind == "counter":
                    raise FieldError(f"{name}: counter {value!r} is not an integer")
                value = round(value)
            value = int(value)
        if kind == "counter" and value < 0:
            raise FieldError(f"{name}: counter {value} is negative")
        return value
    raise FieldError(f"{name}: unknown type {kind!r}")


def extract_fields(document, fields):
    """Apply fields to a parsed document: (values, errors)."""
    values, errors = {}, []
    for field in fields:
        name = field["name"]
        try:
            try:
                raw = select_field(document, field)
            except FieldError:
                if "default" not in field:
                    raise
                raw = field["default"]
            values[name] = coerce(raw, field.get("type"), name)
        except FieldError as error:
            errors.append(str(error) if str(error).startswith(name) else f"{name}: {error}")
    return values, errors


def parse_json_output(text):
    """One JSON object, or JSON lines of objects merged in order (later keys win)."""
    text = text.strip()
    if not text:
        raise PollError("empty output")
    try:
        document = json.loads(text)
    except ValueError:
        document = None
        merged = {}
        for number, line in enumerate(text.splitlines(), 1):
            line = line.strip()
            if not line:
                continue
            try:
                item = json.loads(line)
            except ValueError as error:
                raise PollError(f"output line {number} is not JSON: {error.msg}") from None
            if not isinstance(item, dict):
                raise PollError(f"output line {number} is not a JSON object")
            merged.update(item)
        return merged
    if not isinstance(document, dict):
        raise PollError("output is not a JSON object")
    return document


def scalars_of(document, label):
    """Top-level scalars of an object (fields absent): non-scalars dropped with a warning."""
    dropped = [k for k, v in document.items() if not is_scalar(v)]
    if dropped:
        log.warning("%s: dropped non-scalar keys %s", label, ", ".join(map(repr, dropped[:5])))
    return {k: v for k, v in document.items() if is_scalar(v)}


# --- back-off and sequencing -------------------------------------------------------------

def backoff_delay(interval_s, failures, rnd=random, cap_s=BACKOFF_CAP_S):
    """interval x 2^failures, capped, with +-20% jitter (failures >= 1)."""
    if failures <= 0:
        return interval_s
    base = min(cap_s, interval_s * 2 ** min(failures, 30))
    return min(cap_s, base * rnd.uniform(0.8, 1.2))


class Sequence:
    """Strictly increasing per source; wall-clock milliseconds so a restart is never stale."""

    def __init__(self, clock=time.time):
        self.clock, self.last = clock, 0

    def next(self):
        self.last = max(self.last + 1, int(self.clock() * 1000))
        return self.last

    def floor(self, value):
        self.last = max(self.last, value)


def sanitize_error(message, secrets=()):
    """At most 256 characters, without URL query strings, fragments or secret values."""
    message = re.sub(r"(\b[a-zA-Z][a-zA-Z0-9+.-]*://[^\s?#'\"]*)[?#][^\s'\"]*", r"\1", str(message))
    for secret in secrets:
        if secret and len(secret) >= 4:
            message = message.replace(secret, "***")
    message = " ".join(message.split())
    return message[:256]


# --- configuration --------------------------------------------------------------------

def _number(spec, key, default, label):
    value = spec.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ConfigError(f"{label}: {key} must be a number")
    return float(value)


def _is_loopback_host(host):
    if host in ("localhost", "localhost.localdomain"):
        return True
    try:
        return ipaddress.ip_address(host.strip("[]")).is_loopback
    except ValueError:
        return False


def _check_fields(fields, label):
    if not isinstance(fields, list) or len(fields) > MAX_KEYS:
        raise ConfigError(f"{label}: fields must be a list of at most {MAX_KEYS}")
    names = set()
    for field in fields:
        if not isinstance(field, dict) or not isinstance(field.get("name"), str):
            raise ConfigError(f"{label}: each field needs a name")
        name = field["name"]
        if not name or name.startswith("_") or len(name.encode()) > MAX_KEY_BYTES or name in names:
            raise ConfigError(f"{label}: invalid or duplicate field name {name!r}")
        names.add(name)
        selections = [k for k in ("pointer", "length", "countWhere", "first") if k in field]
        if len(selections) != 1:
            raise ConfigError(f"{label}: field {name!r} needs exactly one of pointer, length, countWhere, first")
        if field.get("type") is not None and field["type"] not in FIELD_TYPES:
            raise ConfigError(f"{label}: field {name!r} type must be one of {sorted(FIELD_TYPES)}")
        for key in ("countWhere", "first"):
            if key in field:
                test = field[key]
                if not isinstance(test, dict) or "array" not in test or (key == "countWhere" and "equals" not in test):
                    raise ConfigError(f"{label}: field {name!r} {key} needs array, pointer and equals")
        if "default" in field and not is_scalar(field["default"]):
            raise ConfigError(f"{label}: field {name!r} default must be a scalar")


def load_config(path, dev=False):
    path = pathlib.Path(path)
    try:
        config = json.loads(path.read_text())
    except (OSError, ValueError) as error:
        raise ConfigError(f"{path}: {error}") from None
    if not isinstance(config, dict) or not isinstance(config.get("sources"), list):
        raise ConfigError(f"{path}: expected an object with a sources list")
    client = config.get("client", {})
    client_id = client.get("id", DEFAULT_CLIENT_ID)
    if not isinstance(client_id, str) or not ID_RE.fullmatch(client_id) or len(client_id) > 128:
        raise ConfigError(f"client id {client_id!r} is not a dotted lower-case id")
    dev = dev or config.get("devFastIntervals") is True
    base = path.resolve().parent
    sources, seen = [], set()
    for spec in config["sources"]:
        if not isinstance(spec, dict):
            raise ConfigError("each source must be an object")
        slug = spec.get("id")
        label = f"source {slug!r}"
        if not isinstance(slug, str) or not SLUG_RE.fullmatch(slug):
            raise ConfigError(f"{label}: id must be a lower-case slug ([a-z][a-z0-9-]*)")
        if slug in seen:
            raise ConfigError(f"{label}: duplicate id")
        seen.add(slug)
        if spec.get("enabled", True) is False:
            continue
        sources.append(SourceSpec(spec, client_id, base, dev))
    if len(sources) > MAX_SOURCES:
        raise ConfigError(f"at most {MAX_SOURCES} sources")
    return {"client": {"id": client_id, "name": client.get("name", DEFAULT_CLIENT_NAME)},
            "protocol": str(config.get("protocol", DEFAULT_PROTOCOL)), "dev": dev, "sources": sources}


class SourceSpec:
    def __init__(self, spec, client_id, base, dev):
        self.spec = spec
        self.slug = spec["id"]
        self.source_id = f"{client_id}.{self.slug}"
        self.kind = spec.get("kind")
        label = self.label = f"source {self.slug!r}"
        self.fields = spec.get("fields")
        if self.fields is not None:
            _check_fields(self.fields, label)
        self.secrets = []
        if self.kind == "command":
            argv = spec.get("argv")
            if not isinstance(argv, list) or not argv or not all(isinstance(a, str) and a for a in argv):
                raise ConfigError(f"{label}: argv must be a non-empty list of strings")
            cwd = pathlib.Path(spec.get("cwd", "."))
            self.cwd = cwd if cwd.is_absolute() else base / cwd
            program = pathlib.Path(argv[0])
            if "/" in argv[0] and not program.is_absolute():
                program = self.cwd / program
            self.argv = [str(program)] + argv[1:]
            env = spec.get("env", {})
            if not isinstance(env, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in env.items()):
                raise ConfigError(f"{label}: env must map strings to strings")
            self.env = env
            self.output = spec.get("output", "json")
            if self.output not in OUTPUTS:
                raise ConfigError(f"{label}: output must be one of {sorted(OUTPUTS)}")
            if self.output != "json" and self.fields is not None:
                raise ConfigError(f"{label}: fields apply to json output only (use key)")
            self.key = spec.get("key", "value")
            self.interval = _number(spec, "intervalS", COMMAND_DEFAULT_INTERVAL_S, label)
            floor = DEV_MIN_INTERVAL_S if dev else COMMAND_MIN_INTERVAL_S
            self.timeout = _number(spec, "timeoutS", min(COMMAND_DEFAULT_TIMEOUT_S, self.interval), label)
            lo, hi = COMMAND_TIMEOUT_RANGE_S
        elif self.kind == "http":
            self.url = spec.get("url")
            parsed = urllib.parse.urlsplit(self.url) if isinstance(self.url, str) else None
            if not parsed or parsed.scheme not in ("http", "https") or not parsed.hostname:
                raise ConfigError(f"{label}: url must be an http(s) URL")
            self.local = spec.get("local") is True
            self.loopback = _is_loopback_host(parsed.hostname)
            if parsed.scheme == "http" and not (self.loopback or self.local):
                raise ConfigError(f"{label}: plain http needs a loopback host or \"local\": true")
            self.method = spec.get("method", "GET")
            if self.method not in ("GET", "POST"):
                raise ConfigError(f"{label}: method must be GET or POST")
            if "body" in spec and self.method != "POST":
                raise ConfigError(f"{label}: body needs method POST")
            self.body = spec.get("body")
            self.headers = spec.get("headers", {})
            if not isinstance(self.headers, dict):
                raise ConfigError(f"{label}: headers must be an object")
            for name, value in self.headers.items():
                if isinstance(value, dict):
                    if not isinstance(value.get("secret"), str):
                        raise ConfigError(f"{label}: header {name!r} needs a secret name")
                elif not isinstance(value, str):
                    raise ConfigError(f"{label}: header {name!r} must be a string or a secret reference")
                else:
                    self.secrets.append(value)
            self.interval = _number(spec, "intervalS", HTTP_DEFAULT_INTERVAL_S, label)
            floor = DEV_MIN_INTERVAL_S if dev else (HTTP_LOOPBACK_MIN_INTERVAL_S if self.loopback else HTTP_MIN_INTERVAL_S)
            self.timeout = _number(spec, "timeoutS", min(HTTP_DEFAULT_TIMEOUT_S, self.interval), label)
            lo, hi = HTTP_TIMEOUT_RANGE_S
        elif self.kind == "timer":
            self.timer = spec.get("timer")
            if self.timer == "pulse":
                self.every = _number(spec, "everyS", 60, label)
                if self.every < 1:
                    raise ConfigError(f"{label}: everyS must be at least 1")
                self.anchor = _number(spec, "fromS", 0, label)
                self.key = spec.get("key", "pulse")
            elif self.timer == "countdown":
                if "untilS" in spec or "until" in spec:
                    self.until = _parse_time(spec.get("untilS", spec.get("until")), label)
                    self.duration = None
                else:
                    self.duration = _number(spec, "durationS", 60, label)
                    self.until = None
                self.key = spec.get("key", "remaining")
            else:
                raise ConfigError(f"{label}: timer must be pulse or countdown")
            self.interval = _number(spec, "intervalS", TIMER_DEFAULT_INTERVAL_S, label)
            floor = TIMER_MIN_INTERVAL_S
            self.timeout, lo, hi = 0.0, 0.0, 0.0
        else:
            raise ConfigError(f"{label}: kind must be command, http or timer")
        if self.kind != "command" and self.kind != "timer" and "key" in spec:
            raise ConfigError(f"{label}: key applies to command and timer sources")
        if self.kind in ("command", "timer") and (not isinstance(self.key, str) or not self.key
                                                   or self.key.startswith("_")):
            raise ConfigError(f"{label}: key must be a non-empty name not starting with _")
        if self.interval < floor:
            hint = "" if dev else " (devFastIntervals allows 1 s for prototype simulators)"
            raise ConfigError(f"{label}: intervalS must be at least {floor:g}{hint}")
        if self.kind != "timer" and not lo <= self.timeout <= hi:
            raise ConfigError(f"{label}: timeoutS must be within {lo:g}-{hi:g}")
        ttl = spec.get("ttlMs", int(min(TTL_MAX_MS, max(TTL_MIN_MS, self.interval * 10000))))
        if isinstance(ttl, bool) or not isinstance(ttl, int) or not TTL_MIN_MS <= ttl <= TTL_MAX_MS:
            raise ConfigError(f"{label}: ttlMs must be an integer {TTL_MIN_MS}-{TTL_MAX_MS}")
        self.ttl_ms = ttl


def _parse_time(value, label):
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return float(value)
    if isinstance(value, str):
        try:
            moment = datetime.datetime.fromisoformat(value.replace("Z", "+00:00"))
        except ValueError:
            raise ConfigError(f"{label}: until must be Unix seconds or ISO 8601") from None
        if moment.tzinfo is None:
            moment = moment.astimezone()
        return moment.timestamp()
    raise ConfigError(f"{label}: until must be Unix seconds or ISO 8601")


# --- fetchers ----------------------------------------------------------------------------

class Processes:
    """Running command process groups, so shutdown can kill them."""

    def __init__(self):
        self.lock, self.groups = threading.Lock(), set()

    def add(self, pid):
        with self.lock:
            self.groups.add(pid)

    def discard(self, pid):
        with self.lock:
            self.groups.discard(pid)

    def kill_all(self):
        with self.lock:
            for pid in list(self.groups):
                _killpg(pid)


PROCESSES = Processes()


def _killpg(pid):
    try:
        os.killpg(pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass


def run_command(argv, cwd, env, timeout, stdout_cap=STDOUT_CAP, stderr_keep=STDERR_KEEP, processes=PROCESSES):
    """Run without a shell in a new process group. Returns (code, stdout, stderr_tail).

    Raises PollError on a timeout (the group is killed) or stdout over the cap."""
    environment = dict(os.environ)
    environment.update(env)
    try:
        process = subprocess.Popen(argv, cwd=cwd, env=environment, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    except OSError as error:
        raise PollError(f"cannot run {pathlib.Path(argv[0]).name}: {error.strerror or error}") from None
    processes.add(process.pid)
    out, err = bytearray(), bytearray()
    deadline = time.monotonic() + timeout
    problem = None
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            selector.register(process.stderr, selectors.EVENT_READ)
            while selector.get_map() and problem is None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    problem = f"timed out after {timeout:g} s"
                    break
                for key, _ in selector.select(remaining):
                    data = os.read(key.fd, 65536)
                    if not data:
                        selector.unregister(key.fileobj)
                    elif key.fileobj is process.stdout:
                        out += data
                        if len(out) > stdout_cap:
                            problem = f"stdout over {stdout_cap // 1024} KiB"
                            break
                    else:
                        err += data
                        del err[:-stderr_keep]
        if problem is None:
            try:
                process.wait(max(0.0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                problem = f"timed out after {timeout:g} s"
        if problem is not None:
            _killpg(process.pid)
            process.wait()
            raise PollError(problem, {"_exitCode": process.returncode})
    finally:
        processes.discard(process.pid)
        process.stdout.close()
        process.stderr.close()
        if process.poll() is None:
            _killpg(process.pid)
            process.wait()
    return process.returncode, bytes(out), bytes(err).decode("utf-8", "replace")


def command_values(spec):
    code, out, err = run_command(spec.argv, spec.cwd, spec.env, spec.timeout)
    if err.strip():
        log.debug("%s stderr: %s", spec.label, err.strip()[-500:])
    meta = {"_exitCode": code}
    if code != 0:
        tail = err.strip().splitlines()[-1] if err.strip() else ""
        raise PollError(f"exit code {code}" + (f": {tail}" if tail else ""), meta)
    text = out.decode("utf-8", "replace")
    if spec.output == "json":
        document = parse_json_output(text)
        if spec.fields is not None:
            values, errors = extract_fields(document, spec.fields)
            return values, errors, meta
        return scalars_of(document, spec.label), [], meta
    lines = [line for line in text.splitlines() if line.strip()]
    if spec.output == "lines":
        return {spec.key: len(lines)}, [], meta
    if not lines:
        raise PollError("empty output", meta)
    if spec.output == "text":
        return {spec.key: truncate_utf8(lines[0].strip(), MAX_STRING_BYTES)}, [], meta
    try:
        return {spec.key: coerce(lines[0].strip(), "number", spec.key)}, [], meta
    except FieldError:
        raise PollError("output is not a number", meta) from None


class SameOrigin(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        old, new = urllib.parse.urlsplit(req.full_url), urllib.parse.urlsplit(newurl)
        if (old.scheme, old.hostname, old.port) != (new.scheme, new.hostname, new.port):
            raise PollError(f"redirect to another origin refused (HTTP {code})", {"_httpStatus": code})
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def _retry_after(headers):
    value = headers.get("Retry-After") if headers else None
    if not value:
        return None
    value = value.strip()
    if value.isdigit():
        return float(value)
    try:
        moment = datetime.datetime.strptime(value, "%a, %d %b %Y %H:%M:%S GMT").replace(tzinfo=datetime.timezone.utc)
    except ValueError:
        return None
    return max(0.0, moment.timestamp() - time.time())


def _secret(name):
    return os.environ.get("HYPRUNE_SOURCES_SECRET_" + re.sub(r"[^A-Za-z0-9]", "_", name).upper())


def _check_resolution(spec, host):
    """No DNS rebinding: a public name must not resolve to a loopback or private address."""
    if spec.local or spec.loopback:
        return
    try:
        ipaddress.ip_address(host)
        return  # a literal public address
    except ValueError:
        pass
    try:
        infos = socket.getaddrinfo(host, None)
    except OSError as error:
        raise PollError(f"cannot resolve host: {error.strerror or error}") from None
    for info in infos:
        address = ipaddress.ip_address(info[4][0].split("%")[0])
        if address.is_private or address.is_loopback or address.is_link_local:
            raise PollError("host resolves to a local address; mark the source local")


def http_values(spec):
    headers, secrets = {"Accept": "application/json", "User-Agent": "hyprune-source-helper/0"}, list(spec.secrets)
    for name, value in spec.headers.items():
        if isinstance(value, dict):
            secret = _secret(value["secret"])
            if secret is None:
                raise PollError(f"secret {value['secret']!r} is not set")
            secrets.append(secret)
            value = value.get("prefix", "") + secret
        headers[name] = value
    data = None
    if spec.method == "POST":
        data = json.dumps(spec.body if spec.body is not None else {}).encode()
        headers["Content-Type"] = "application/json"
    _check_resolution(spec, urllib.parse.urlsplit(spec.url).hostname)
    request = urllib.request.Request(spec.url, data=data, headers=headers, method=spec.method)
    opener = urllib.request.build_opener(SameOrigin)
    try:
        with opener.open(request, timeout=spec.timeout) as response:
            status = response.status
            body = response.read(HTTP_BODY_CAP + 1)
    except urllib.error.HTTPError as error:
        retry = _retry_after(error.headers) if error.code in (429, 503) else None
        error.close()
        raise PollError(f"HTTP {error.code} {error.reason}", {"_httpStatus": error.code}, retry) from None
    except PollError:
        raise
    except urllib.error.URLError as error:
        raise PollError(sanitize_error(f"request failed: {error.reason}", secrets)) from None
    except (OSError, ValueError) as error:
        raise PollError(sanitize_error(f"request failed: {error}", secrets)) from None
    meta = {"_httpStatus": status}
    if len(body) > HTTP_BODY_CAP:
        raise PollError("response body over 1 MiB", meta)
    try:
        document = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        raise PollError("response is not JSON", meta) from None
    if spec.fields is None:
        if not isinstance(document, dict):
            raise PollError("response is not a JSON object (configure fields)", meta)
        return scalars_of(document, spec.label), [], meta
    values, errors = extract_fields(document, spec.fields)
    return values, errors, meta


def timer_values(spec, now=None):
    now = time.time() if now is None else now
    if spec.timer == "pulse":
        return {spec.key: max(0, int((now - spec.anchor) // spec.every))}, [], {}
    if spec.until is None:
        if not hasattr(spec, "started"):
            spec.started = now
        until = spec.started + spec.duration
    else:
        until = spec.until
    return {spec.key: max(0, math.ceil(until - now))}, [], {}


def poll(spec):
    """One fetch: (values, field errors, metadata). Raises PollError."""
    if spec.kind == "command":
        return command_values(spec)
    if spec.kind == "http":
        return http_values(spec)
    return timer_values(spec)


# --- source state ------------------------------------------------------------------------

class Source:
    def __init__(self, spec, clock=time.monotonic):
        self.spec, self.clock = spec, clock
        self.values, self.meta = {}, {}
        self.has_data = False
        self.failures = 0
        self.running = False
        self.next_due = clock()
        self.sequence = Sequence()
        self.published = None        # last values published on the current connection
        self.published_at = None     # monotonic time of that publication
        self.retry_at = 0.0          # earliest publish attempt after a refusal

    def interval(self, rnd=random):
        jitter = HTTP_JITTER if self.spec.kind == "http" else 0.0
        return self.spec.interval * (1 + rnd.uniform(-jitter, jitter)) if jitter else self.spec.interval

    def succeeded(self, values, errors, meta, rnd=random):
        label = self.spec.label
        limit = MAX_KEYS - 3
        clean = clean_values(values, limit, label)
        for name, value in clean.items():
            if any(f.get("name") == name and f.get("type") == "counter" for f in self.spec.fields or []):
                old = self.values.get(name)
                if isinstance(old, int) and value < old:
                    log.warning("%s: counter %r went from %d to %d (core treats it as a reset)",
                                label, name, old, value)
        if errors:
            # A field that fails keeps its last good value; the source reports the error.
            for error in errors:
                log.warning("%s: %s", label, error)
            kept = {f["name"]: self.values[f["name"]] for f in self.spec.fields or []
                    if f["name"] not in clean and f["name"] in self.values}
            clean = {**kept, **clean}
            meta = {"_status": "error", "_error": sanitize_error("; ".join(errors), self.spec.secrets), **meta}
        else:
            meta = {"_status": "ok", **meta}
        self.values, self.meta, self.has_data = clean, meta, True
        if self.failures:
            log.info("%s: recovered after %d failure(s)", label, self.failures)
        self.failures = 0
        self.next_due = self.clock() + self.interval(rnd)

    def failed(self, error, rnd=random):
        self.failures += 1
        message = sanitize_error(str(error), self.spec.secrets)
        self.meta = {"_status": "error", "_error": message, **getattr(error, "meta", {})}
        self.has_data = True  # an error is published too: last good values plus metadata
        delay = backoff_delay(self.spec.interval, self.failures, rnd)
        if getattr(error, "retry_after", None):
            delay = max(delay, min(error.retry_after, 3600.0))
        self.next_due = self.clock() + delay
        log.warning("%s: %s (attempt %d, next in %.1f s)", self.spec.label, message, self.failures, delay)

    def publication(self):
        return {**self.values, **self.meta}

    def due_publication(self, now):
        """The values to publish now, or None: on change, or as a keep-alive at ttl/2."""
        if not self.has_data or now < self.retry_at:
            return None
        current = self.publication()
        if self.published_at is not None and now - self.published_at < MIN_PUBLISH_GAP_S:
            return None
        if current != self.published:
            return current
        if now - self.published_at >= self.spec.ttl_ms / 2000:
            return current
        return None

    def next_publish_time(self):
        """When due_publication may next return values (for the main loop's wait)."""
        if not self.has_data:
            return math.inf
        if self.published_at is None:
            return self.retry_at
        if self.publication() != self.published:
            return max(self.retry_at, self.published_at + MIN_PUBLISH_GAP_S)
        return max(self.retry_at, self.published_at + self.spec.ttl_ms / 2000)

    def mark_published(self, values, now):
        self.published, self.published_at = values, now

    def reset_connection(self):
        self.published = self.published_at = None
        self.retry_at = 0.0


# --- IPC -------------------------------------------------------------------------------

class Connection:
    """Core's IPC framing: 4-byte big-endian length + one JSON-RPC 2.0 object."""

    def __init__(self, path, timeout=5.0):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(timeout)
        try:
            self.socket.connect(path)
        except OSError:
            self.socket.close()
            raise
        self.count = 0

    def _exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.socket.recv(n - len(data))
            if not chunk:
                raise EOFError("IPC connection closed")
            data += chunk
        return data

    def call(self, method, params):
        self.count += 1
        ident = str(self.count)
        payload = json.dumps({"jsonrpc": "2.0", "id": ident, "method": method, "params": params},
                             separators=(",", ":")).encode()
        self.socket.sendall(struct.pack(">I", len(payload)) + payload)
        while True:
            size = struct.unpack(">I", self._exact(4))[0]
            message = json.loads(self._exact(size))
            if message.get("id") == ident:
                if "error" in message:
                    raise RpcError(message["error"].get("code"), message["error"].get("message", ""))
                return message.get("result")
            if message.get("id") is None and "error" in message:
                raise EOFError(f"core closed the session: {message['error'].get('message')}")
            # Notifications are not expected without state.read; ignore them.

    def close(self):
        try:
            self.socket.close()
        except OSError:
            pass


class TokenSource:
    """Launch tokens are one-use: a reconnect needs a fresh one (a rewritten token file)."""

    def __init__(self, file=None, value=None):
        self.file, self.value, self.used = file, value, set()

    def get(self):
        token = self.value
        if self.file:
            try:
                token = pathlib.Path(self.file).read_text().strip()
            except OSError as error:
                log.warning("cannot read token file: %s", error.strerror)
                token = None
        if token and not TOKEN_RE.fullmatch(token):
            log.warning("ignoring a token that is not 64 lower-case hex digits")
            token = None
        if token and token in self.used:
            log.warning("the launch token was already used (tokens are one-use); "
                        "rewrite the token file with a fresh grant to reconnect")
        return token


class Publisher:
    def __init__(self, socket_path, tokens, client, protocol):
        self.path, self.tokens, self.client, self.protocol = socket_path, tokens, client, protocol
        self.connection = None
        self.reconnect_at = 0.0
        self.reconnects = 0

    def connected(self):
        return self.connection is not None

    def connect(self, now):
        if self.connection or now < self.reconnect_at:
            return False
        token = self.tokens.get() if self.tokens else None
        params = {"protocols": [self.protocol], "client": dict(self.client), "capabilities": ["data.publish"]}
        if token:
            params["token"] = token
        connection = None
        try:
            connection = Connection(self.path)
            result = connection.call("session.hello", params)
            if token:
                self.tokens.used.add(token)
            if "data.publish" not in (result or {}).get("capabilities", []):
                raise RpcError(-32002, "no data.publish grant (missing, expired or already used launch token)")
        except (OSError, EOFError, ValueError, RpcError) as error:
            if connection:
                connection.close()
            self.reconnects += 1
            delay = backoff_delay(1.0, self.reconnects, cap_s=RECONNECT_CAP_S)
            self.reconnect_at = now + delay
            log.warning("cannot connect to core: %s (retry in %.1f s)", error, delay)
            return False
        log.info("connected to core (protocol %s) as %s", result.get("protocol"), self.client["id"])
        self.connection, self.reconnects = connection, 0
        return True

    def drop(self, now, reason):
        if self.connection:
            log.warning("connection lost: %s", reason)
            self.connection.close()
        self.connection = None
        self.reconnects += 1
        delay = backoff_delay(1.0, self.reconnects, cap_s=RECONNECT_CAP_S)
        self.reconnect_at = now + delay

    def publish(self, source, values, now):
        """True when published. Refusals are handled here; a lost connection is dropped."""
        params = {"sourceId": source.spec.source_id, "sequence": source.sequence.next(),
                  "values": values, "ttlMs": source.spec.ttl_ms}
        try:
            self.connection.call("data.publish", params)
        except RpcError as error:
            return self._refused(source, values, error, now)
        except (OSError, EOFError, ValueError) as error:
            self.drop(now, error)
            return False
        log.debug("publish %s #%d %s", source.spec.source_id, params["sequence"],
                  json.dumps(values, separators=(",", ":")))
        source.mark_published(values, now)
        return True

    def _refused(self, source, values, error, now):
        label = source.spec.label
        if error.code == -32008 and "rate" in error.message:
            source.retry_at = now + 0.2
            log.debug("%s: rate limited, retrying", label)
        elif error.code == -32008:
            source.retry_at = now + 5.0
            log.warning("%s: refused (%s), retrying in 5 s", label, error.message)
        elif error.code == -32004 and "sequence" in error.message:
            source.sequence.floor(int(time.time() * 1000) + 1000)
            source.retry_at = now + 0.2
            log.warning("%s: stale sequence, moving forward", label)
        elif error.code == -32601:
            self.drop(now, "data.publish not advertised (no grant)")
        else:
            # Invalid values or a namespace/ownership refusal: wait for new values.
            source.mark_published(values, now)
            source.retry_at = now + 5.0
            log.error("%s: data.publish refused: %s", label, error)
        return False

    def remove_all(self, sources):
        if not self.connection:
            return
        for source in sources:
            if source.published is None:
                continue
            try:
                self.connection.call("data.remove", {"sourceId": source.spec.source_id})
                log.info("removed %s", source.spec.source_id)
            except (RpcError, OSError, EOFError, ValueError) as error:
                log.warning("data.remove %s: %s", source.spec.source_id, error)
                if not isinstance(error, RpcError):
                    break
        self.connection.close()
        self.connection = None


class DryRunPublisher:
    """Prints each publication instead of sending it."""

    def __init__(self, stream=sys.stdout):
        self.stream = stream

    def connected(self):
        return True

    def connect(self, now):
        return False

    def publish(self, source, values, now):
        sequence = source.sequence.next()
        self.stream.write(json.dumps({"sourceId": source.spec.source_id, "sequence": sequence,
                                      "ttlMs": source.spec.ttl_ms, "values": values},
                                     separators=(",", ":")) + "\n")
        self.stream.flush()
        source.mark_published(values, now)
        return True

    def remove_all(self, sources):
        for source in sources:
            if source.published is not None:
                self.stream.write(json.dumps({"remove": source.spec.source_id}) + "\n")
        self.stream.flush()


# --- scheduling ------------------------------------------------------------------------

class Runner:
    def __init__(self, sources, publisher):
        self.sources = sources
        self.publisher = publisher
        self.results = queue.Queue()
        self.jobs = queue.Queue()
        self.stopping = threading.Event()
        self.was_connected = False
        for _ in range(min(CONCURRENCY, max(1, len(sources)))):
            threading.Thread(target=self._worker, daemon=True).start()

    def _worker(self):
        while True:
            source = self.jobs.get()
            if source is None:
                return
            try:
                result = ("ok", poll(source.spec))
            except PollError as error:
                result = ("error", error)
            except Exception as error:  # a bug in a fetcher must not stop the helper
                log.exception("%s: unexpected failure", source.spec.label)
                result = ("error", PollError(f"internal error: {type(error).__name__}"))
            self.results.put((source, result))

    def stop(self):
        # Safe from a signal handler: only an Event (the loop wakes at least every 0.25 s).
        self.stopping.set()

    def _handle(self, item):
        source, (status, payload) = item
        source.running = False
        if status == "ok":
            source.succeeded(*payload)
        else:
            source.failed(payload)

    def step(self, now):
        """Dispatch due fetches and publications; returns the next wake time."""
        for source in self.sources:
            if not source.running and now >= source.next_due:
                if source.spec.kind == "timer":  # no I/O: computed inline
                    source.succeeded(*poll(source.spec))
                else:
                    source.running = True
                    self.jobs.put(source)
        if not self.publisher.connected():
            self.publisher.connect(now)
        connected = self.publisher.connected()
        if connected and not self.was_connected:
            for source in self.sources:
                source.reset_connection()
        self.was_connected = connected
        if connected:
            for source in self.sources:
                values = source.due_publication(now)
                if values is not None:
                    self.publisher.publish(source, values, now)
                    if not self.publisher.connected():
                        self.was_connected = False
                        break
        wake = min([s.next_due for s in self.sources if not s.running] or [now + 1])
        if self.publisher.connected():
            wake = min([wake] + [s.next_publish_time() for s in self.sources])
        else:
            wake = min(wake, getattr(self.publisher, "reconnect_at", wake))
        return wake

    def run(self):
        while not self.stopping.is_set():
            now = time.monotonic()
            wake = self.step(now)
            timeout = max(0.01, min(0.25, wake - time.monotonic()))
            try:
                item = self.results.get(timeout=timeout)
            except queue.Empty:
                continue
            while True:
                self._handle(item)
                try:
                    item = self.results.get_nowait()
                except queue.Empty:
                    break
        PROCESSES.kill_all()
        self.publisher.remove_all(self.sources)


def run_once(sources, stream=sys.stdout):
    failed = False
    for spec in sources:
        source = Source(spec)
        try:
            source.succeeded(*poll(spec))
        except PollError as error:
            source.failed(error)
            failed = True
        stream.write(json.dumps({"sourceId": spec.source_id, "values": source.publication()},
                                separators=(",", ":")) + "\n")
    stream.flush()
    return 1 if failed else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Publish command, HTTP and timer sources to core with data.publish.")
    parser.add_argument("--config", required=True, help="sources JSON file")
    parser.add_argument("--socket", default=os.environ.get("HYPRUNE_SOCKET"),
                        help="core IPC socket (default $HYPRUNE_SOCKET); required unless --once/--dry-run")
    tokens = parser.add_mutually_exclusive_group()
    tokens.add_argument("--token-file", help="file holding the launch token (re-read on each connect)")
    tokens.add_argument("--token-fd", type=int, help="inherited descriptor to read the launch token from")
    parser.add_argument("--protocol", help=f"IPC protocol to offer (default: config, else {DEFAULT_PROTOCOL})")
    parser.add_argument("--dev", action="store_true",
                        help="prototype only: allow 1 s intervals (same as devFastIntervals)")
    parser.add_argument("--once", action="store_true", help="fetch every source once, print, exit")
    parser.add_argument("--dry-run", action="store_true", help="run the loop but print publications")
    parser.add_argument("-v", "--verbose", action="store_true", help="log one line per publication")
    args = parser.parse_args(argv)
    logging.basicConfig(stream=sys.stderr, level=logging.DEBUG if args.verbose else logging.INFO,
                        format="source-helper: %(levelname)s %(message)s")
    try:
        config = load_config(args.config, args.dev)
    except ConfigError as error:
        log.error("%s", error)
        return 2
    if config["dev"]:
        log.info("devFastIntervals: 1 s intervals allowed (prototype simulators only)")
    if args.once:
        return run_once(config["sources"])
    token = None
    if args.token_fd is not None:
        with os.fdopen(args.token_fd) as stream:
            token = stream.read().strip()
    elif not args.token_file:
        token = os.environ.get("HYPRUNE_SOURCES_TOKEN") or os.environ.get("HYPRUNE_MACHINES_TOKEN")
    if args.dry_run:
        publisher = DryRunPublisher()
    else:
        if not args.socket:
            log.error("no socket: pass --socket or set HYPRUNE_SOCKET (there is deliberately no default)")
            return 2
        if not (token or args.token_file):
            log.warning("no launch token: core will not grant data.publish")
        publisher = Publisher(args.socket, TokenSource(args.token_file, token), config["client"],
                              args.protocol or config["protocol"])
    runner = Runner([Source(spec) for spec in config["sources"]], publisher)
    for number in (signal.SIGTERM, signal.SIGINT):
        signal.signal(number, lambda *_: runner.stop())
    try:
        runner.run()
    except BrokenPipeError:  # --dry-run piped into a reader that went away
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
