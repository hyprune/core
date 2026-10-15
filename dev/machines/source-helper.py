#!/usr/bin/env python3
"""Source helper prototype: publishes command, HTTP and timer sources with data.publish.

A development tool for the live-objects (machines) prototype. It runs out of process,
connects to core's IPC socket as `com.hyprune.sources` with only the `data.publish`
grant, and publishes one source per configured fetch. A local control socket (same user
only) reports status and tests, restarts and reloads sources. See README.md in this directory.
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
import select
import selectors
import shutil
import signal
import socket
import stat
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
HTTP_METHODS = ("GET", "POST", "PUT", "PATCH")
HTTP_REQUEST_BODY_CAP = 64 * 1024
DEV_MIN_INTERVAL_S = 1.0  # devFastIntervals / --dev: prototype simulators only
TIMER_MIN_INTERVAL_S, TIMER_DEFAULT_INTERVAL_S = 1.0, 1.0
MAX_INTERVAL_S = 86400.0
STDOUT_CAP, STDERR_KEEP, HTTP_BODY_CAP = 64 * 1024, 4 * 1024, 1024 * 1024
BACKOFF_CAP_S = 600.0
CONCURRENCY = 4
RECONNECT_CAP_S = 60.0
FIELD_TYPES = {"string", "number", "int", "bool", "counter"}
OUTPUTS = {"json", "number", "text", "lines"}
TOKEN_RE = re.compile(r"[a-f0-9]{64}")
TOKEN_READ_TIMEOUT_S = 10.0
EXIT_NO_GRANT = 3  # the token descriptor closed and the connection is gone: nothing can reconnect
CONFIG_CHECK_S = 1.0  # how often the config file's mtime is checked
META_KEYS = 4  # _status, _error, _exitCode or _httpStatus, _reset
CONTROL_REQUEST_CAP = 64 * 1024
CONTROL_OUTPUT_CAP = 4 * 1024
CONTROL_CONNECTIONS, CONTROL_TESTS = 16, 2
CONTROL_REPLY_TIMEOUT_S = 30.0


class ConfigError(Exception):
    pass


class ConfigMissing(ConfigError):
    """The config file does not exist: the helper idles until it appears."""


class PollError(Exception):
    """A failed fetch: keeps the last good values and backs off."""

    def __init__(self, message, meta=None, retry_after=None):
        super().__init__(message)
        self.meta = meta or {}
        self.retry_after = retry_after
        self.stdout, self.stderr = b"", ""  # what a command printed before it failed


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
    """The raw value a field selects (pointer, a JSONPath-like path with an optional reduce, or a
    length/countWhere/first aggregate)."""
    if "pointer" in field:
        return json_pointer(document, field["pointer"])
    if "path" in field:
        return select_path(document, field["path"], field.get("reduce"))
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


# --- JSONPath-like selectors and the gauge mapping (data sources phase 2) ---------------

PATH_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_-]*")
PATH_FILTER = re.compile(r"""\?\(@((?:\.[A-Za-z_][A-Za-z0-9_-]*|\[\d+\])*)\s*(?:(==|!=|<=|>=|<|>)\s*(.+?))?\s*\)""")
REDUCERS = {"count", "sum", "avg", "min", "max", "first", "last"}
MAX_PATH = 256


def _literal(text):
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        return text[1:-1]
    if text in ("true", "false"):
        return text == "true"
    if text == "null":
        return None
    try:
        return float(text) if any(c in text for c in ".eE") else int(text)
    except ValueError:
        raise FieldError(f"filter value {text!r} is not a string, number, true, false or null") from None


def _compare(left, op, right):
    if op is None:
        return left is not _MISSING and left is not None and left is not False
    if left is _MISSING:
        return False
    if op in ("==", "!="):
        if isinstance(left, bool) or isinstance(right, bool):  # true is not 1
            same = isinstance(left, bool) and isinstance(right, bool) and left == right
        else:
            same = left == right
        return same if op == "==" else not same
    numbers = all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in (left, right))
    strings = isinstance(left, str) and isinstance(right, str)
    if not (numbers or strings):
        return False
    return {"<": left < right, "<=": left <= right, ">": left > right, ">=": left >= right}[op]


def _member(node, steps):
    for kind, key in steps:
        if kind == "name" and isinstance(node, dict) and key in node:
            node = node[key]
        elif kind == "index" and isinstance(node, list) and -len(node) <= key < len(node):
            node = node[key]
        else:
            return _MISSING
    return node


def parse_path(expr):
    """A JSONPath-like selector as a list of steps. Supported: $ (root), .name and ['name'],
    [n] (negative from the end), [a:b] slices, * and [*] (every member), ..name (recursive),
    and filters [?(@.a.b == 'x')] (==, !=, <, <=, >, >=; strings, numbers, true, false, null;
    [?(@.a)] tests that a member is present and true-ish). Raises FieldError."""
    if not isinstance(expr, str) or not expr.startswith("$") or len(expr) > MAX_PATH:
        raise FieldError(f"invalid path {expr!r} (it starts with $)")
    steps, i = [], 1
    while i < len(expr):
        c = expr[i]
        if expr.startswith("..", i):
            m = PATH_IDENT.match(expr, i + 2)
            if not m:
                raise FieldError(f"{expr}: .. needs a member name")
            steps.append(("descend", m.group(0))); i = m.end()
        elif c == ".":
            if expr.startswith(".*", i):
                steps.append(("all", None)); i += 2; continue
            m = PATH_IDENT.match(expr, i + 1)
            if not m:
                raise FieldError(f"{expr}: a member name after . at {i}")
            steps.append(("name", m.group(0))); i = m.end()
        elif c == "[":
            end = expr.find("]", i)
            if expr.startswith("[?(", i):
                end = expr.find(")]", i) + 1
            if end <= i:
                raise FieldError(f"{expr}: unclosed [")
            inner = expr[i + 1:end].strip()
            if inner == "*":
                steps.append(("all", None))
            elif inner[:1] in "'\"" and inner[-1:] == inner[:1] and len(inner) >= 2:
                steps.append(("name", inner[1:-1]))
            elif re.fullmatch(r"-?\d+", inner):
                steps.append(("index", int(inner)))
            elif re.fullmatch(r"-?\d*:-?\d*", inner):
                a, b = inner.split(":")
                steps.append(("slice", (int(a) if a else None, int(b) if b else None)))
            elif inner.startswith("?("):
                m = PATH_FILTER.fullmatch(inner)
                if not m:
                    raise FieldError(f"{expr}: filter {inner!r} is not [?(@.member op value)]")
                sub = [("name", n) if not n.startswith("[") else ("index", int(n[1:-1]))
                       for n in re.findall(r"\.[A-Za-z_][A-Za-z0-9_-]*|\[\d+\]", m.group(1))]
                sub = [(k, v[1:] if k == "name" else v) for k, v in sub]
                steps.append(("filter", (sub, m.group(2), _literal(m.group(3)) if m.group(2) else None)))
            else:
                raise FieldError(f"{expr}: unsupported selector [{inner}]")
            i = end + 1
        else:
            raise FieldError(f"{expr}: unexpected {c!r} at {i}")
    return steps


def json_path(document, expr):
    """Every value a JSONPath-like selector matches, in document order (parse_path)."""
    nodes = [document]
    for kind, arg in parse_path(expr):
        out = []
        for node in nodes:
            if kind == "name":
                if isinstance(node, dict) and arg in node:
                    out.append(node[arg])
            elif kind == "index":
                if isinstance(node, list) and -len(node) <= arg < len(node):
                    out.append(node[arg])
            elif kind == "slice":
                if isinstance(node, list):
                    out.extend(node[arg[0]:arg[1]])
            elif kind == "all":
                out.extend(node.values() if isinstance(node, dict) else node if isinstance(node, list) else [])
            elif kind == "descend":
                stack = [node]
                while stack:
                    cur = stack.pop()
                    if isinstance(cur, dict):
                        if arg in cur:
                            out.append(cur[arg])
                        stack.extend(reversed(list(cur.values())))
                    elif isinstance(cur, list):
                        stack.extend(reversed(cur))
            elif kind == "filter":
                sub, op, value = arg
                items = node.values() if isinstance(node, dict) else node if isinstance(node, list) else []
                out.extend(e for e in items if _compare(_member(e, sub), op, value))
        nodes = out
    return nodes


def select_path(document, expr, reduce=None):
    """A selector's value: its first match (a scalar), or a reduction of all its matches:
    count, sum, avg, min, max (numbers), first, last. Raises FieldError."""
    matches = json_path(document, expr)
    if reduce == "count":
        return len(matches)
    if reduce in ("sum", "avg", "min", "max"):
        numbers = []
        for m in matches:
            try:
                numbers.append(coerce(m, "number", expr))
            except FieldError:
                raise FieldError(f"{expr}: {reduce} needs numbers, found {json.dumps(m)[:40]}") from None
        if not numbers:
            if reduce == "sum":
                return 0
            raise FieldError(f"{expr}: no match to {reduce}")
        return {"sum": sum(numbers), "avg": sum(numbers) / len(numbers), "min": min(numbers), "max": max(numbers)}[reduce]
    if not matches:
        raise FieldError(f"{expr}: no match")
    value = matches[-1] if reduce == "last" else matches[0]
    if not is_scalar(value) and value is not None:
        raise FieldError(f"{expr}: matches a {'list' if isinstance(value, list) else 'object'}, not a value (add a reduce)")
    return value


LEVELS = ("ok", "warn", "fail")  # the gauges' level input
TEMPLATE_RE = re.compile(r"\{([^{}:]+)(?::([^{}]*))?\}")
FORMAT_SPEC_RE = re.compile(r"[<>^]?[+-]?\d{0,3}[,_]?(\.\d{1,6})?[fdeg%]?")
MAP_KEYS = {"value", "level", "display", "label"}


def _check_select(spec, label, allow_const=False):
    """A mapping selection: {"path"[, "reduce"]}, {"pointer"}, {"field"} (a name from fields)."""
    if allow_const and isinstance(spec, (str, int, float)) and not isinstance(spec, bool):
        return
    if not isinstance(spec, dict):
        raise ConfigError(f"{label} must be an object")
    picks = [k for k in ("path", "pointer", "field") if k in spec]
    if len(picks) != 1:
        raise ConfigError(f"{label} needs exactly one of path, pointer, field")
    if "path" in spec:
        try:
            parse_path(spec["path"])
        except FieldError as error:
            raise ConfigError(f"{label}: {error}") from None
    if spec.get("reduce") is not None and (spec["reduce"] not in REDUCERS or "path" not in spec):
        raise ConfigError(f"{label}: reduce is one of {sorted(REDUCERS)} with a path")


def check_map(mapping, label):
    """The gauge mapping: value (0-100, scaled from min/max), level (ok, warn or fail: thresholds on
    the value or a text mapping), display (a template) and label (text or a selection). Raises
    ConfigError."""
    if not isinstance(mapping, dict) or not mapping or set(mapping) - MAP_KEYS:
        raise ConfigError(f"{label}: map takes value, level, display and label")
    if "value" in mapping:
        v = mapping["value"]
        _check_select(v, f"{label}: map.value")
        for key in ("min", "max"):
            if key in v and (isinstance(v[key], bool) or not isinstance(v[key], (int, float)) or not math.isfinite(v[key])):
                raise ConfigError(f"{label}: map.value.{key} must be a number")
        if v.get("min", 0) == v.get("max", 100):
            raise ConfigError(f"{label}: map.value min and max must differ")
    if "level" in mapping:
        lv = mapping["level"]
        if not isinstance(lv, dict):
            raise ConfigError(f"{label}: map.level must be an object")
        if "warn" in lv or "fail" in lv:
            for key in ("warn", "fail"):
                if key in lv and (isinstance(lv[key], bool) or not isinstance(lv[key], (int, float))):
                    raise ConfigError(f"{label}: map.level.{key} must be a number")
            if lv.get("of", "value") not in ("value", "raw") or not isinstance(lv.get("below", False), bool):
                raise ConfigError(f"{label}: map.level of is value or raw; below is true or false")
            if lv.get("of", "value") == "value" and "value" not in mapping:
                raise ConfigError(f"{label}: map.level thresholds need map.value")
        elif "match" in lv:
            _check_select({k: x for k, x in lv.items() if k not in ("match", "otherwise")}, f"{label}: map.level")
            if not isinstance(lv["match"], dict) or not all(x in LEVELS for x in lv["match"].values()) or lv.get("otherwise", "ok") not in LEVELS:
                raise ConfigError(f"{label}: map.level.match maps texts to ok, warn or fail")
        else:
            raise ConfigError(f"{label}: map.level needs warn/fail thresholds or a match")
    if "display" in mapping:
        if not isinstance(mapping["display"], str) or len(mapping["display"]) > 256:
            raise ConfigError(f"{label}: map.display must be a template of at most 256 characters")
        for name, spec in TEMPLATE_RE.findall(mapping["display"]):
            if spec and not FORMAT_SPEC_RE.fullmatch(spec):
                raise ConfigError(f"{label}: map.display format {{{name}:{spec}}} is not a number or text format")
            if name.startswith("$"):
                try:
                    parse_path(name)
                except FieldError as error:
                    raise ConfigError(f"{label}: map.display: {error}") from None
    if "label" in mapping:
        lab = mapping["label"]
        if isinstance(lab, str):
            if len(lab) > 64:
                raise ConfigError(f"{label}: map.label at most 64 characters")
        else:
            _check_select(lab, f"{label}: map.label")


def _pick(document, values, spec):
    if not isinstance(spec, dict):
        return spec
    if "field" in spec:
        if spec["field"] not in values:
            raise FieldError(f"field {spec['field']!r} has no value")
        return values[spec["field"]]
    if "pointer" in spec:
        return json_pointer(document, spec["pointer"])
    return select_path(document, spec["path"], spec.get("reduce"))


def _format(value, spec):
    if not spec:
        if isinstance(value, float):
            return f"{value:g}" if abs(value) < 1e15 else str(value)
        return "true" if value is True else "false" if value is False else str(value)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        if spec[-1:] in "fdeg%":
            raise FieldError(f"display: {value!r} is not a number for :{spec}")
        return format(str(value), spec)
    if spec.endswith("d"):
        value = int(round(value))
    return format(value, spec)


def apply_map(document, values, mapping):
    """The mapped keys (value, level, display, label) of a document and the fields already
    picked: (values, errors). A failing key keeps out; the others still map."""
    out, errors, raw = {}, [], None
    if "value" in mapping:
        v = mapping["value"]
        try:
            raw = coerce(_pick(document, values, v), "number", "value")
            lo, hi = v.get("min", 0), v.get("max", 100)
            scaled = (raw - lo) / (hi - lo) * 100
            out["value"] = round(min(100.0, max(0.0, scaled)), 2)
            if out["value"].is_integer():
                out["value"] = int(out["value"])
        except FieldError as error:
            errors.append(f"value: {error}")
    if "label" in mapping:
        try:
            out["label"] = truncate_utf8(str(_pick(document, values, mapping["label"])), 64)
        except FieldError as error:
            errors.append(f"label: {error}")
    if "level" in mapping:
        lv = mapping["level"]
        try:
            if "match" in lv:
                text = _pick(document, values, {k: x for k, x in lv.items() if k not in ("match", "otherwise")})
                out["level"] = lv["match"].get(str(text).lower() if isinstance(text, str) else json.dumps(text), lv.get("otherwise", "ok"))
            else:
                x = out.get("value") if lv.get("of", "value") == "value" else raw
                if x is None:
                    raise FieldError("no value to compare")
                worse = (lambda a, b: a <= b) if lv.get("below") else (lambda a, b: a >= b)
                out["level"] = "fail" if "fail" in lv and worse(x, lv["fail"]) else "warn" if "warn" in lv and worse(x, lv["warn"]) else "ok"
        except FieldError as error:
            errors.append(f"level: {error}")
    if "display" in mapping:
        names = {**values, **out, "raw": raw}
        def fill(match):
            name, spec = match.group(1).strip(), match.group(2)
            if name.startswith("$"):
                value = select_path(document, name)
            elif name in names and names[name] is not None:
                value = names[name]
            else:
                raise FieldError(f"display: {{{name}}} has no value")
            return _format(value, spec)
        try:
            out["display"] = truncate_utf8(TEMPLATE_RE.sub(fill, mapping["display"]), MAX_STRING_BYTES)
        except (FieldError, ValueError) as error:
            errors.append(str(error) if str(error).startswith("display") else f"display: {error}")
    return out, errors


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
        selections = [k for k in ("pointer", "path", "length", "countWhere", "first") if k in field]
        if len(selections) != 1:
            raise ConfigError(f"{label}: field {name!r} needs exactly one of pointer, path, length, countWhere, first")
        if "path" in field:
            try:
                parse_path(field["path"])
            except FieldError as error:
                raise ConfigError(f"{label}: field {name!r}: {error}") from None
        if field.get("reduce") is not None and (field["reduce"] not in REDUCERS or "path" not in field):
            raise ConfigError(f"{label}: field {name!r} reduce is one of {sorted(REDUCERS)} with a path")
        if field.get("type") is not None and field["type"] not in FIELD_TYPES:
            raise ConfigError(f"{label}: field {name!r} type must be one of {sorted(FIELD_TYPES)}")
        for key in ("countWhere", "first"):
            if key in field:
                test = field[key]
                if not isinstance(test, dict) or "array" not in test or (key == "countWhere" and "equals" not in test):
                    raise ConfigError(f"{label}: field {name!r} {key} needs array, pointer and equals")
        if "default" in field and not is_scalar(field["default"]):
            raise ConfigError(f"{label}: field {name!r} default must be a scalar")


def empty_config(dev=False):
    """No sources: what the helper runs with until a config file appears."""
    return {"client": {"id": DEFAULT_CLIENT_ID, "name": DEFAULT_CLIENT_NAME}, "protocol": DEFAULT_PROTOCOL,
            "dev": dev, "sources": [], "disabled": []}


def load_config(path, dev=False):
    """The parsed config. An empty file, or one without a sources list, has no sources.
    Raises ConfigMissing when the file does not exist, ConfigError when it is invalid."""
    path = pathlib.Path(path)
    try:
        text = path.read_text()
    except FileNotFoundError:
        raise ConfigMissing(f"{path}: no such file") from None
    except (OSError, ValueError) as error:
        raise ConfigError(f"{path}: {error}") from None
    try:
        config = json.loads(text) if text.strip() else {}
    except ValueError as error:
        raise ConfigError(f"{path}: {error}") from None
    if isinstance(config, dict) and "sources" not in config:
        config = {**config, "sources": []}
    if not isinstance(config, dict) or not isinstance(config.get("sources"), list):
        raise ConfigError(f"{path}: expected an object with a sources list")
    client = config.get("client", {})
    client_id = client.get("id", DEFAULT_CLIENT_ID)
    if not isinstance(client_id, str) or not ID_RE.fullmatch(client_id) or len(client_id) > 128:
        raise ConfigError(f"client id {client_id!r} is not a dotted lower-case id")
    dev = dev or config.get("devFastIntervals") is True
    base = path.resolve().parent
    sources, disabled, seen = [], [], set()
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
            disabled.append(spec)
            continue
        sources.append(SourceSpec(spec, client_id, base, dev))
    if len(sources) > MAX_SOURCES:
        raise ConfigError(f"at most {MAX_SOURCES} sources")
    return {"client": {"id": client_id, "name": client.get("name", DEFAULT_CLIENT_NAME)},
            "protocol": str(config.get("protocol", DEFAULT_PROTOCOL)), "dev": dev, "sources": sources,
            "disabled": disabled}


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
        self.map = spec.get("map")
        if self.map is not None:
            check_map(self.map, label)
            if self.kind == "timer":
                raise ConfigError(f"{label}: map applies to command and http sources")
            for key in ("field",):
                for sel in (self.map.get("value"), self.map.get("label"), self.map.get("level")):
                    if isinstance(sel, dict) and key in sel and sel[key] not in {f["name"] for f in self.fields or []} | {spec.get("key", "value")}:
                        raise ConfigError(f"{label}: map refers to field {sel[key]!r}, which isn't one")
        self.secrets = []
        self.secret_names = sorted(set(secret_names({k: spec.get(k) for k in ("url", "headers", "body")})))
        written = json.dumps({k: spec.get(k) for k in ("url", "headers", "body")})
        for loose in re.findall(r"\{secret:[^}]*\}", written):
            if not SECRET_PLACEHOLDER_RE.fullmatch(loose):
                raise ConfigError(f"{label}: {loose[:40]} is not a secret placeholder ({{secret:name}}, name letters, digits, ._-)")
        for name in self.secret_names:
            if not SECRET_NAME_RE.fullmatch(name):
                raise ConfigError(f"{label}: secret name {name!r} must be letters, digits, ._- (at most 64)")
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
            if self.method not in HTTP_METHODS:
                raise ConfigError(f"{label}: method must be one of {', '.join(HTTP_METHODS)}")
            if "body" in spec and self.method == "GET":
                raise ConfigError(f"{label}: a body needs method POST, PUT or PATCH")
            self.body = spec.get("body")
            if self.body is not None and len(json.dumps(self.body)) > HTTP_REQUEST_BODY_CAP:
                raise ConfigError(f"{label}: body over {HTTP_REQUEST_BODY_CAP // 1024} KiB")
            self.output = spec.get("output", "json")
            if self.output not in OUTPUTS:
                raise ConfigError(f"{label}: output must be one of {sorted(OUTPUTS)}")
            if self.output != "json" and self.fields is not None:
                raise ConfigError(f"{label}: fields apply to json output only (use key)")
            self.key = spec.get("key", "value")
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
        if self.kind == "http" and "key" in spec and self.output == "json":
            raise ConfigError(f"{label}: key applies to non-json output")
        if self.kind in ("command", "timer", "http") and (not isinstance(self.key, str) or not self.key
                                                   or self.key.startswith("_")):
            raise ConfigError(f"{label}: key must be a non-empty name not starting with _")
        if self.interval < floor:
            hint = "" if dev else " (devFastIntervals allows 1 s for prototype simulators)"
            raise ConfigError(f"{label}: intervalS must be at least {floor:g}{hint}")
        if self.interval > MAX_INTERVAL_S:
            raise ConfigError(f"{label}: intervalS must be at most {MAX_INTERVAL_S:g}")
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


class ProcessSlot:
    """The process group a source's command is running in, so a restart can kill it."""

    def __init__(self):
        self.lock, self.process = threading.Lock(), None

    def started(self, process):
        with self.lock:
            self.process = process

    def finished(self):
        with self.lock:
            self.process = None

    def kill(self):
        """Kill the running group. True when there was one."""
        with self.lock:
            if self.process is None or self.process.returncode is not None:
                return False
            _killpg(self.process.pid)
            return True


def run_command(argv, cwd, env, timeout, stdout_cap=STDOUT_CAP, stderr_keep=STDERR_KEEP, processes=PROCESSES,
                slot=None):
    """Run without a shell in a new process group. Returns (code, stdout, stderr_tail).

    Raises PollError on a timeout (the group is killed) or stdout over the cap; the error
    carries what was read (`stdout`, `stderr`)."""
    environment = dict(os.environ)
    environment.update(env)
    try:
        process = subprocess.Popen(argv, cwd=cwd, env=environment, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    except OSError as error:
        raise PollError(f"cannot run {pathlib.Path(argv[0]).name}: {error.strerror or error}") from None
    processes.add(process.pid)
    if slot is not None:
        slot.started(process)
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
            error = PollError(problem, {"_exitCode": process.returncode})
            error.stdout, error.stderr = bytes(out[:stdout_cap]), bytes(err).decode("utf-8", "replace")
            raise error
    finally:
        if slot is not None:
            slot.finished()
        processes.discard(process.pid)
        process.stdout.close()
        process.stderr.close()
        if process.poll() is None:
            _killpg(process.pid)
            process.wait()
    return process.returncode, bytes(out), bytes(err).decode("utf-8", "replace")


def command_values(spec, slot=None):
    code, out, err = run_command(spec.argv, spec.cwd, spec.env, spec.timeout, slot=slot)
    return interpret_command(spec, code, out, err)


def interpret_command(spec, code, out, err):
    """A finished command's (values, field errors, metadata). Raises PollError."""
    if err.strip():
        log.debug("%s stderr: %s", spec.label, err.strip()[-500:])
    meta = {"_exitCode": code}
    if code != 0:
        tail = err.strip().splitlines()[-1] if err.strip() else ""
        raise PollError(f"exit code {code}" + (f": {tail}" if tail else ""), meta)
    return interpret_text(spec, out.decode("utf-8", "replace"), meta)


def interpret_text(spec, text, meta, document=_MISSING):
    """Output text (a command's stdout, an HTTP body) as (values, field errors, metadata) by
    the source's output, fields and map. Raises PollError."""
    if spec.output == "json":
        if document is _MISSING:
            document = parse_json_output(text)
        if spec.fields is not None:
            values, errors = extract_fields(document, spec.fields)
        elif isinstance(document, dict):
            values, errors = scalars_of(document, spec.label), []
        elif spec.map is not None:
            values, errors = {}, []
        else:
            raise PollError("output is not a JSON object (configure fields or a map)", meta)
    else:
        lines = [line for line in text.splitlines() if line.strip()]
        if spec.output == "lines":
            values = {spec.key: len(lines)}
        elif not lines:
            raise PollError("empty output", meta)
        elif spec.output == "text":
            values = {spec.key: truncate_utf8(lines[0].strip(), MAX_STRING_BYTES)}
        else:
            try:
                values = {spec.key: coerce(lines[0].strip(), "number", spec.key)}
            except FieldError:
                raise PollError("output is not a number", meta) from None
        document, errors = values, []
    if spec.map is not None:
        mapped, map_errors = apply_map(document, values, spec.map)
        values = {**values, **mapped}
        errors = errors + map_errors
    return values, errors, meta


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


SECRET_NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}")
SECRET_PLACEHOLDER_RE = re.compile(r"\{secret:([A-Za-z0-9][A-Za-z0-9_.-]{0,63})\}")
KEYRING_TIMEOUT_S = 5.0


class Secrets:
    """Secrets by name, never logged or published: the environment (HYPRUNE_SOURCES_SECRET_<NAME>,
    for development), then the secrets file (a JSON object of names to strings; a regular file of
    this user's with no group or other permission bits, mode 0600), then the desktop keyring
    (`secret-tool lookup application hyprune secret <name>`, when secret-tool is installed)."""

    def __init__(self, path=None, keyring=True):
        self.path = pathlib.Path(path) if path else default_secrets_path()
        self.keyring = keyring

    def file(self):
        """(names to values, problem): the secrets file, {} when it doesn't exist."""
        try:
            st = os.stat(self.path, follow_symlinks=False)
        except FileNotFoundError:
            return {}, None
        except OSError as error:
            return {}, f"secrets file: {error.strerror}"
        if not stat.S_ISREG(st.st_mode) or st.st_uid != os.getuid():
            return {}, "secrets file must be a regular file of this user"
        if st.st_mode & 0o077:
            return {}, f"secrets file must be mode 0600 (is {stat.S_IMODE(st.st_mode):04o})"
        try:
            data = json.loads(self.path.read_text() or "{}")
        except (OSError, ValueError) as error:
            return {}, f"secrets file is not JSON: {error}"
        if not isinstance(data, dict) or not all(isinstance(v, str) for v in data.values()):
            return {}, "secrets file must map names to strings"
        return data, None

    def keyring_available(self):
        return self.keyring and shutil.which("secret-tool") is not None

    def lookup(self, name):
        """(value, origin): origin env, file or keyring; (None, None) when it is not set anywhere."""
        value = os.environ.get("HYPRUNE_SOURCES_SECRET_" + re.sub(r"[^A-Za-z0-9]", "_", name).upper())
        if value is not None:
            return value, "env"
        data, _ = self.file()
        if name in data:
            return data[name], "file"
        if self.keyring_available():
            try:
                done = subprocess.run(["secret-tool", "lookup", "application", "hyprune", "secret", name],
                                      capture_output=True, timeout=KEYRING_TIMEOUT_S, stdin=subprocess.DEVNULL)
            except (OSError, subprocess.TimeoutExpired):
                return None, None
            if done.returncode == 0 and done.stdout:
                return done.stdout.decode("utf-8", "replace").rstrip("\n"), "keyring"
        return None, None


def default_secrets_path():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return pathlib.Path(base) / "hyprune" / "secrets.json"


SECRETS = Secrets()


def _secret(name):
    return SECRETS.lookup(name)[0]


def resolve_secrets(value, used):
    """A string with {secret:name} placeholders filled in (the values appended to `used`), or a
    JSON value with every string so filled. Raises PollError naming a missing secret."""
    if isinstance(value, str):
        def fill(match):
            secret = _secret(match.group(1))
            if secret is None:
                raise PollError(f"secret {match.group(1)!r} is not set")
            used.append(secret)
            return secret
        return SECRET_PLACEHOLDER_RE.sub(fill, value)
    if isinstance(value, list):
        return [resolve_secrets(v, used) for v in value]
    if isinstance(value, dict):
        return {k: resolve_secrets(v, used) for k, v in value.items()}
    return value


def secret_names(value):
    """The secret names a JSON value refers to ({secret:name} placeholders, {"secret": name} headers)."""
    if isinstance(value, str):
        return SECRET_PLACEHOLDER_RE.findall(value)
    if isinstance(value, list):
        return [n for v in value for n in secret_names(v)]
    if isinstance(value, dict):
        if set(value) <= {"secret", "prefix"} and isinstance(value.get("secret"), str):
            return [value["secret"]]
        return [n for v in value.values() for n in secret_names(v)]
    return []


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


def http_fetch(spec):
    """One request: (status, content type, body bytes, secret values used). Raises PollError."""
    secrets = list(spec.secrets)
    headers = {"Accept": "application/json" if spec.output == "json" else "*/*", "User-Agent": "hyprune-source-helper/0"}
    for name, value in spec.headers.items():
        if isinstance(value, dict):
            secret = _secret(value["secret"])
            if secret is None:
                raise PollError(f"secret {value['secret']!r} is not set")
            secrets.append(secret)
            value = value.get("prefix", "") + secret
        else:
            value = resolve_secrets(value, secrets)
        headers[name] = value
    url = resolve_secrets(spec.url, secrets)
    data = None
    if spec.method != "GET":
        body = resolve_secrets(spec.body if spec.body is not None else {}, secrets)
        if isinstance(body, str):
            data = body.encode()
            headers.setdefault("Content-Type", "text/plain; charset=utf-8")
        else:
            data = json.dumps(body).encode()
            headers.setdefault("Content-Type", "application/json")
    _check_resolution(spec, urllib.parse.urlsplit(url).hostname)
    request = urllib.request.Request(url, data=data, headers=headers, method=spec.method)
    opener = urllib.request.build_opener(SameOrigin)
    try:
        with opener.open(request, timeout=spec.timeout) as response:
            status = response.status
            kind = response.headers.get("Content-Type", "")
            body = response.read(HTTP_BODY_CAP + 1)
    except urllib.error.HTTPError as error:
        retry = _retry_after(error.headers) if error.code in (429, 503) else None
        try:
            head = error.read(CONTROL_OUTPUT_CAP)
        except OSError:
            head = b""
        error.close()
        failure = PollError(f"HTTP {error.code} {error.reason}", {"_httpStatus": error.code}, retry)
        failure.stdout = head
        raise failure from None
    except PollError:
        raise
    except urllib.error.URLError as error:
        raise PollError(sanitize_error(f"request failed: {error.reason}", secrets)) from None
    except (OSError, ValueError) as error:
        raise PollError(sanitize_error(f"request failed: {error}", secrets)) from None
    return status, kind, body, secrets


def interpret_http(spec, status, body):
    """A response body as (values, field errors, metadata). Raises PollError."""
    meta = {"_httpStatus": status}
    if len(body) > HTTP_BODY_CAP:
        raise PollError("response body over 1 MiB", meta)
    try:
        text = body.decode("utf-8")
    except UnicodeDecodeError:
        raise PollError("response is not UTF-8 text", meta) from None
    if spec.output != "json":
        return interpret_text(spec, text, meta)
    try:
        document = json.loads(text)
    except ValueError:
        raise PollError("response is not JSON", meta) from None
    return interpret_text(spec, text, meta, document)


def http_values(spec):
    status, _, body, _ = http_fetch(spec)
    return interpret_http(spec, status, body)


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


def poll(spec, slot=None):
    """One fetch: (values, field errors, metadata). Raises PollError."""
    if spec.kind == "command":
        return command_values(spec, slot)
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
        self.slot = ProcessSlot()    # the running command, for a restart
        self.discard = False         # a restart killed the running fetch: ignore its result
        self.rerun = False           # a restart came while an uninterruptible fetch ran
        self.resets = 0              # resetAverage requests
        self.published_resets = 0    # _reset as published: it moves with the next run's values,
                                     # so core restarts the average on fresh data, never stale
        self.last_run = None         # the last finished fetch, for the control channel's status

    def interval(self, rnd=random):
        jitter = HTTP_JITTER if self.spec.kind == "http" else 0.0
        return self.spec.interval * (1 + rnd.uniform(-jitter, jitter)) if jitter else self.spec.interval

    def succeeded(self, values, errors, meta, rnd=random):
        label = self.spec.label
        limit = MAX_KEYS - META_KEYS
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
        self.published_resets = self.resets
        if self.failures:
            log.info("%s: recovered after %d failure(s)", label, self.failures)
        self.failures = 0
        self.next_due = self.clock() + self.interval(rnd)

    def failed(self, error, rnd=random):
        self.failures += 1
        message = sanitize_error(str(error), self.spec.secrets)
        self.meta = {"_status": "error", "_error": message, **getattr(error, "meta", {})}
        self.has_data = True  # an error is published too: last good values plus metadata
        self.published_resets = self.resets
        delay = backoff_delay(self.spec.interval, self.failures, rnd)
        if getattr(error, "retry_after", None):
            delay = max(delay, min(error.retry_after, 3600.0))
        self.next_due = self.clock() + delay
        log.warning("%s: %s (attempt %d, next in %.1f s)", self.spec.label, message, self.failures, delay)

    def publication(self):
        values = {**self.values, **self.meta}
        if self.published_resets:
            values["_reset"] = self.published_resets
        return values

    def record_run(self, duration_ms):
        meta = self.meta
        self.last_run = {"at": int(time.time() * 1000), "ok": meta.get("_status") == "ok",
                         "values": self.publication(),
                         "error": meta.get("_error"), "exitCode": meta.get("_exitCode"),
                         "durationMs": int(round(duration_ms))}

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
    """Launch tokens are one-use: a reconnect needs a fresh one.

    From a file (re-read on each connection), a value, or an inherited descriptor. A
    descriptor carries one token line per connection. With `refresh` (a supervisor that
    renews grants, like the shell's launcher), every connection after the first writes
    `renew\\n` to the descriptor and reads the next token line."""

    def __init__(self, file=None, value=None, fd=None, refresh=False, read_timeout=TOKEN_READ_TIMEOUT_S):
        self.file, self.value, self.used = file, value, set()
        self.fd, self.refresh, self.read_timeout = fd, refresh, read_timeout
        self.buffer, self.reads, self.outstanding, self.closed = b"", 0, 0, False

    def _line(self):
        """The next token line from the descriptor, or None (timeout, EOF)."""
        if self.closed:
            return None
        if self.refresh and self.reads > 0 and not self.outstanding:
            try:
                os.write(self.fd, b"renew\n")
                self.outstanding += 1
            except OSError as error:
                log.error("token channel closed (%s); cannot renew the grant", error.strerror)
                self.closed = True
                return None
        self.reads += 1
        deadline = time.monotonic() + self.read_timeout
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            readable = select.select([self.fd], [], [], max(0.0, remaining))[0] if remaining > 0 else []
            if not readable:
                log.warning("no token from the supervisor within %g s", self.read_timeout)
                return None
            try:
                data = os.read(self.fd, 256)
            except OSError:
                data = b""
            if not data:
                self.closed = True
                if self.buffer.strip():  # a last token without a newline
                    line, self.buffer = self.buffer, b""
                    return line.decode("ascii", "replace").strip()
                log.error("token channel closed; reconnecting needs a restart of the helper")
                return None
            self.buffer += data
            if len(self.buffer) > 1024:
                log.error("token channel sent an over-long line; ignoring it")
                self.closed = True
                return None
        line, self.buffer = self.buffer.split(b"\n", 1)
        self.outstanding = max(0, self.outstanding - 1)
        return line.decode("ascii", "replace").strip()

    def get(self):
        token = self.value
        if self.fd is not None:
            token = self._line()
        elif self.file:
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

    def exhausted(self):
        """True when tokens come from a descriptor that has closed: no grant can follow."""
        return bool(self.tokens and self.tokens.fd is not None and self.tokens.closed)

    def connect(self, now):
        if self.connection or now < self.reconnect_at:
            return False
        params = {"protocols": [self.protocol], "client": dict(self.client), "capabilities": ["data.publish"]}
        connection = token = None
        try:
            connection = Connection(self.path)
            # Only once the socket is reachable: a token (or a renewal) per real attempt.
            token = self.tokens.get() if self.tokens else None
            if token:
                params["token"] = token
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

    def reset_backoff(self):
        self.reconnect_at, self.reconnects = 0.0, 0

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

    def remove(self, source):
        """data.remove for a source published on this connection. False when the connection broke."""
        if not self.connection or source.published is None:
            return True
        try:
            self.connection.call("data.remove", {"sourceId": source.spec.source_id})
            log.info("removed %s", source.spec.source_id)
        except RpcError as error:
            log.warning("data.remove %s: %s", source.spec.source_id, error)
        except (OSError, EOFError, ValueError) as error:
            log.warning("data.remove %s: %s", source.spec.source_id, error)
            return False
        source.reset_connection()
        return True

    def remove_all(self, sources):
        if not self.connection:
            return
        for source in sources:
            if not self.remove(source):
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

    def reset_backoff(self):
        pass

    def publish(self, source, values, now):
        sequence = source.sequence.next()
        self.stream.write(json.dumps({"sourceId": source.spec.source_id, "sequence": sequence,
                                      "ttlMs": source.spec.ttl_ms, "values": values},
                                     separators=(",", ":")) + "\n")
        self.stream.flush()
        source.mark_published(values, now)
        return True

    def remove(self, source):
        if source.published is not None:
            self.stream.write(json.dumps({"remove": source.spec.source_id}) + "\n")
            self.stream.flush()
            source.reset_connection()
        return True

    def remove_all(self, sources):
        for source in sources:
            self.remove(source)
        self.stream.flush()


# --- scheduling ------------------------------------------------------------------------

def _file_signature(path):
    """What identifies a version of the config file: (inode, size, mtime), or None."""
    try:
        info = os.stat(path)
    except OSError:
        return None
    return (info.st_ino, info.st_size, info.st_mtime_ns)


class ConfigWatcher:
    """Re-reads the config file when it changes (checked about once a second)."""

    def __init__(self, path, config, dev=False, clock=time.monotonic, error=None):
        self.path, self.dev, self.clock = path, dev, clock
        self.client, self.protocol = config["client"], config["protocol"]
        self.signature = _file_signature(path)
        self.check_at = clock() + CONFIG_CHECK_S
        self.error = error  # why the current file is not in use (invalid), or None

    def _load(self, warn=True):
        try:
            config = load_config(self.path, self.dev)
        except ConfigMissing:
            raise
        except ConfigError as error:
            self.error = str(error)
            raise
        self.error = None
        if warn and (config["client"]["id"] != self.client["id"] or config["protocol"] != self.protocol):
            log.warning("client id and protocol changes apply after a restart (or restart-all)")
        return config

    def changed(self, now):
        """A newly loaded config when the file changed and is valid, else None."""
        if now < self.check_at:
            return None
        self.check_at = now + CONFIG_CHECK_S
        signature = _file_signature(self.path)
        if signature is None or signature == self.signature:
            return None  # a missing file (mid-save, or removed) keeps the running sources
        self.signature = signature
        try:
            return self._load()
        except ConfigMissing:
            return None
        except ConfigError as error:
            log.error("config not reloaded, keeping the running sources: %s", error)
            return None

    def load_now(self, warn=True):
        """Re-read the file now (the control channel's reload and restart-all). Raises ConfigError."""
        self.signature = _file_signature(self.path)
        self.check_at = self.clock() + CONFIG_CHECK_S
        return self._load(warn)

    def adopt(self, config):
        """restart-all reconnected with this config's client id and protocol."""
        self.client, self.protocol = config["client"], config["protocol"]


def _plain(number):
    """A whole float as an int, for JSON replies."""
    return int(number) if isinstance(number, float) and number.is_integer() else number


def limits(dev):
    """The interval and timeout bounds the helper enforces (the control channel's status)."""
    return {"dev": dev,
            "command": {"intervalS": [_plain(DEV_MIN_INTERVAL_S if dev else COMMAND_MIN_INTERVAL_S),
                                      _plain(MAX_INTERVAL_S)],
                        "devIntervalS": _plain(DEV_MIN_INTERVAL_S),
                        "timeoutS": [_plain(v) for v in COMMAND_TIMEOUT_RANGE_S]},
            "http": {"intervalS": [_plain(DEV_MIN_INTERVAL_S if dev else HTTP_MIN_INTERVAL_S), _plain(MAX_INTERVAL_S)],
                     "loopbackIntervalS": _plain(DEV_MIN_INTERVAL_S if dev else HTTP_LOOPBACK_MIN_INTERVAL_S),
                     "timeoutS": [_plain(v) for v in HTTP_TIMEOUT_RANGE_S]},
            "timer": {"intervalS": [_plain(TIMER_MIN_INTERVAL_S), _plain(MAX_INTERVAL_S)]}}


def tail_utf8(text, limit):
    """The last `limit` bytes of a text, on a character boundary."""
    data = text.encode("utf-8")
    return text if len(data) <= limit else data[-limit:].decode("utf-8", "ignore")


DEFAULT_INTERVALS = {"command": COMMAND_DEFAULT_INTERVAL_S, "http": HTTP_DEFAULT_INTERVAL_S,
                     "timer": TIMER_DEFAULT_INTERVAL_S}


def _failure(message):
    return {"ok": False, "error": message}


class Runner:
    def __init__(self, sources, publisher, watcher=None, disabled=(), dev=False, protocol=None):
        self.sources = sources
        self.publisher = publisher
        self.watcher = watcher
        self.disabled = list(disabled)   # disabled entries (raw), listed by status
        self.dev = dev
        self.protocol = protocol         # --protocol: overrides the config's on restart-all
        self.results = queue.Queue()     # fetch results and control requests, in order
        self.jobs = queue.Queue()
        self.stopping = threading.Event()
        self.exit_code = 0
        self.was_connected = False
        self.retired = []  # removed sources still to be withdrawn with data.remove
        self.tests = threading.BoundedSemaphore(CONTROL_TESTS)
        for _ in range(CONCURRENCY if watcher else min(CONCURRENCY, max(1, len(sources)))):
            threading.Thread(target=self._worker, daemon=True).start()

    def apply(self, specs, disabled=None):
        """Switch to a new list of source specs: added sources start, removed ones are
        withdrawn (data.remove), changed ones restart. Unchanged sources keep running.
        Returns the slugs added, changed and removed."""
        if disabled is not None:
            self.disabled = list(disabled)
        current = {source.spec.source_id: source for source in self.sources}
        wanted = {spec.source_id for spec in specs}
        sources, added, changed = [], [], []
        for spec in specs:
            old = current.get(spec.source_id)
            if old is not None and old.spec.spec == spec.spec:
                sources.append(old)
                continue
            source = Source(spec)
            if old is not None:
                source.sequence.floor(old.sequence.last)
                source.published, source.published_at = old.published, old.published_at
                source.resets, source.published_resets = old.resets, old.published_resets
                changed.append(spec.slug)
            else:
                added.append(spec.slug)
            sources.append(source)
        removed = [source for sid, source in current.items() if sid not in wanted]
        self.retired += removed
        self.sources = sources
        if added or changed or removed:
            log.info("config reloaded: %s", "; ".join(part for part in (
                "added " + ", ".join(added) if added else "",
                "changed " + ", ".join(changed) if changed else "",
                "removed " + ", ".join(s.spec.slug for s in removed) if removed else "") if part))
        else:
            log.info("config reloaded: no source changes")
        return {"added": added, "changed": changed, "removed": [s.spec.slug for s in removed]}

    def apply_config(self, config):
        self.dev = config["dev"]
        return self.apply(config["sources"], config.get("disabled", []))

    def _withdraw(self):
        if not self.retired:
            return
        if not self.publisher.connected():
            self.retired = []  # core already dropped this connection's sources
            return
        while self.retired:
            if not self.publisher.remove(self.retired[0]):
                self.publisher.drop(time.monotonic(), "data.remove failed")
                self.was_connected = False
                self.retired = []
                return
            self.retired.pop(0)

    def _worker(self):
        while True:
            source = self.jobs.get()
            if source is None:
                return
            start = time.monotonic()
            try:
                result = ("ok", poll(source.spec, source.slot))
            except PollError as error:
                result = ("error", error)
            except Exception as error:  # a bug in a fetcher must not stop the helper
                log.exception("%s: unexpected failure", source.spec.label)
                result = ("error", PollError(f"internal error: {type(error).__name__}"))
            self.results.put((source, result, (time.monotonic() - start) * 1000))

    def stop(self):
        # Safe from a signal handler: only an Event (the loop wakes at least every 0.25 s).
        self.stopping.set()

    def _handle(self, item):
        source, result, duration_ms = item
        if source is None:
            self._control(result)
            return
        source.running = False
        if source.discard:  # killed by a restart: run again now, the result does not count
            source.discard = False
            source.next_due = source.clock()
            return
        status, payload = result
        if status == "ok":
            source.succeeded(*payload)
        else:
            source.failed(payload)
        source.record_run(duration_ms)
        if source.rerun:
            source.rerun = False
            source.next_due = source.clock()

    def step(self, now):
        """Dispatch due fetches and publications; returns the next wake time."""
        if self.watcher:
            config = self.watcher.changed(now)
            if config is not None:
                self.apply_config(config)
        for source in self.sources:
            if not source.running and now >= source.next_due:
                if source.spec.kind == "timer":  # no I/O: computed inline
                    start = time.monotonic()
                    source.succeeded(*poll(source.spec))
                    source.record_run((time.monotonic() - start) * 1000)
                else:
                    source.running = True
                    self.jobs.put(source)
        if not self.publisher.connected():
            self.publisher.connect(now)
            if not self.publisher.connected() and getattr(self.publisher, "exhausted", lambda: False)():
                # One-use launch tokens: without a supervisor that renews them nothing can
                # reconnect, so stop running commands for a connection that cannot return.
                log.error("cannot reconnect: the token channel is closed (a new grant needs a restart); exiting")
                self.exit_code = EXIT_NO_GRANT
                self.stopping.set()
        connected = self.publisher.connected()
        if connected and not self.was_connected:
            for source in self.sources:
                source.reset_connection()
        self.was_connected = connected
        self._withdraw()
        connected = self.publisher.connected()
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
        if self.watcher:
            wake = min(wake, self.watcher.check_at)
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
        self.publisher.remove_all(self.retired + self.sources)
        while True:  # answer control requests that arrived while stopping
            try:
                source, job, _ = self.results.get_nowait()
            except queue.Empty:
                break
            if source is None:
                job["reply"] = _failure("the helper is stopping")
                job["done"].set()

    # --- control channel (README "Control channel") ------------------------------------

    def submit(self, request):
        """From a control connection: run a request on the loop thread and wait for its reply."""
        if self.stopping.is_set():
            return _failure("the helper is stopping")
        job = {"request": request, "done": threading.Event(), "reply": None}
        self.results.put((None, job, 0))
        if not job["done"].wait(CONTROL_REPLY_TIMEOUT_S):
            return _failure("the helper is busy; try again")
        return job["reply"]

    def _control(self, job):
        request = job["request"]
        operations = {"restart": self._restart, "restart-all": self._restart_all, "reload": self._reload}
        try:
            reply = operations[request["op"]](request)
        except Exception as error:
            log.exception("control %s failed", request.get("op"))
            reply = _failure(f"internal error: {type(error).__name__}")
        job["reply"] = reply
        job["done"].set()

    def _restart(self, request):
        ident, reset = request.get("id"), request.get("resetAverage", False)
        if not isinstance(ident, str) or not ident:
            return _failure("restart needs an id")
        if not isinstance(reset, bool):
            return _failure("resetAverage must be true or false")
        source = next((s for s in self.sources if ident in (s.spec.slug, s.spec.source_id)), None)
        if source is None:
            if any(entry.get("id") == ident for entry in self.disabled):
                return _failure(f"source {ident!r} is disabled")
            return _failure(f"no source {ident!r}")
        if reset:
            source.resets += 1
        source.failures = 0
        if source.running:
            if source.slot.kill():
                source.discard = True  # its result is dropped; it runs again when it ends
            else:
                source.rerun = True    # an HTTP fetch: let it finish, then run again
        else:
            source.next_due = source.clock()
        log.info("%s: restarted%s", source.spec.label, " (average reset)" if reset else "")
        return {"ok": True}

    def _reload(self, request):
        if self.watcher is None:
            return _failure("no config file")
        try:
            config = self.watcher.load_now()
        except ConfigError as error:
            log.error("config not reloaded, keeping the running sources: %s", error)
            return _failure(str(error))
        return {"ok": True, **self.apply_config(config)}

    def _restart_all(self, request):
        config, problem = None, None
        if self.watcher is not None:
            try:
                config = self.watcher.load_now(warn=False)
            except ConfigError as error:
                problem = str(error)
                log.error("restart-all: config not read, restarting the running sources: %s", error)
        for source in self.sources:
            if source.running and source.slot.kill():
                source.discard = True
        self.publisher.remove_all(self.retired + self.sources)  # data.remove each, then close
        self.retired = []
        previous = {source.spec.source_id: source for source in self.sources}
        if config is not None:
            self.dev, self.disabled = config["dev"], list(config["disabled"])
            if isinstance(self.publisher, Publisher):
                self.publisher.client = config["client"]
                self.publisher.protocol = self.protocol or config["protocol"]
            self.watcher.adopt(config)
            specs = config["sources"]
        else:
            specs = [source.spec for source in self.sources]
        sources = []
        for spec in specs:
            source, old = Source(spec), previous.get(spec.source_id)
            if old is not None:
                source.sequence.floor(old.sequence.last)
                source.resets, source.published_resets = old.resets, old.published_resets
            sources.append(source)
        self.sources = sources
        self.publisher.reset_backoff()
        self.was_connected = False
        log.info("restart-all: %d source(s); reconnecting", len(sources))
        return {"ok": True, **({"configError": problem} if problem else {})}

    def status(self, config_path):
        """Read from a control connection's thread: every field is replaced, never mutated."""
        wall, mono = time.time(), time.monotonic()
        entries = {}
        for source in list(self.sources):
            spec, running = source.spec, source.running
            entry = {"kind": spec.kind}
            if spec.kind == "command":
                entry["argv"] = list(spec.argv)
            elif spec.kind == "http":
                entry["url"] = spec.url
            entry.update({"intervalS": _plain(spec.interval), "enabled": True, "running": running,
                          "lastRun": source.last_run,
                          "nextRunAt": None if running else int((wall + max(0.0, source.next_due - mono)) * 1000),
                          "resets": source.resets})
            entries[spec.slug] = entry
        for raw in list(self.disabled):
            kind = raw.get("kind")
            entry = {"kind": kind}
            for key in ("argv", "url"):
                if key in raw:
                    entry[key] = raw[key]
            entry.update({"intervalS": raw.get("intervalS", _plain(DEFAULT_INTERVALS.get(kind, 0)) or None),
                          "enabled": False, "running": False, "lastRun": None, "nextRunAt": None, "resets": 0})
            entries[raw["id"]] = entry
        return {"ok": True, "config": config_path, "connected": self.publisher.connected(),
                "configError": self.watcher.error if self.watcher else None,
                "sources": entries, "limits": limits(self.dev)}

    def test(self, source, config_path):
        """Run a source definition once with the helper's rules; publish nothing."""
        if not isinstance(source, dict):
            return _failure("test needs a source object")
        kind = source.get("kind")
        if kind not in ("command", "http"):
            return _failure("test runs command and http sources only")
        if kind == "command":
            argv = source.get("argv")
            if not isinstance(argv, list) or not argv or not all(isinstance(a, str) and a for a in argv):
                return _failure("argv must be a non-empty list of strings")
        definition = {key: value for key, value in source.items() if key not in ("id", "enabled")}
        client = self.watcher.client["id"] if self.watcher else DEFAULT_CLIENT_ID
        try:
            spec = SourceSpec({**definition, "id": "test"}, client, pathlib.Path(config_path).resolve().parent,
                              self.dev)
        except ConfigError as error:
            return _failure(str(error).removeprefix("source 'test': "))
        if not self.tests.acquire(blocking=False):
            return _failure("another test is running")
        stdout, stderr, code, values, error = b"", "", None, {}, None
        status, content_type, raw, used = None, None, b"", list(spec.secrets)
        start = time.monotonic()
        try:
            if kind == "command":
                code, stdout, stderr = run_command(spec.argv, spec.cwd, spec.env, spec.timeout)
                raw = stdout
                values, errors, _ = interpret_command(spec, code, stdout, stderr)
            else:
                status, content_type, raw, used = http_fetch(spec)
                values, errors, _ = interpret_http(spec, status, raw)
            values = clean_values(values, MAX_KEYS - META_KEYS, "test")
            if errors:
                error = sanitize_error("; ".join(errors), used)
        except PollError as failure:
            error = sanitize_error(str(failure), used)
            code = failure.meta.get("_exitCode", code)
            status = failure.meta.get("_httpStatus", status)
            stdout, stderr = stdout or failure.stdout, stderr or failure.stderr
            raw = raw or failure.stdout
        finally:
            self.tests.release()
        text = truncate_utf8(raw.decode("utf-8", "replace"), CONTROL_OUTPUT_CAP)
        for secret in used:  # a response that echoes a secret back shows it masked
            if secret and len(secret) >= 4:
                text = text.replace(secret, "***")
        return {"ok": True, "values": values,
                "stdout": truncate_utf8(stdout.decode("utf-8", "replace"), CONTROL_OUTPUT_CAP),
                "stderr": tail_utf8(stderr, CONTROL_OUTPUT_CAP), "error": error, "exitCode": code,
                "raw": text, "rawBytes": len(raw), "httpStatus": status, "contentType": content_type,
                "secrets": {name: SECRETS.lookup(name)[1] for name in spec.secret_names},
                "durationMs": int(round((time.monotonic() - start) * 1000))}

    def secrets(self):
        """Where each secret the config and the sources name is found (env, file, keyring or
        null), never the values; the secrets file and whether the keyring can be read."""
        names = set()
        for source in list(self.sources):
            names.update(source.spec.secret_names)
        for raw in self.disabled:
            names.update(n for n in secret_names({k: raw.get(k) for k in ("url", "headers", "body")}) if SECRET_NAME_RE.fullmatch(n))
        _, problem = SECRETS.file()
        return {"ok": True, "file": str(SECRETS.path), "fileProblem": problem, "keyring": SECRETS.keyring_available(),
                "names": {name: SECRETS.lookup(name)[1] for name in sorted(names)}}


class ControlServer:
    """The local control channel: newline-delimited JSON over a Unix socket (mode 0600,
    same-uid peers only). One request line, one reply line. See README "Control channel"."""

    def __init__(self, path, runner, config_path, uid=None):
        self.path, self.runner, self.config_path = path, runner, config_path
        self.uid = os.getuid() if uid is None else uid
        self.listener, self.inode = None, None
        self.lock, self.clients = threading.Lock(), set()

    def open(self):
        """Bind the socket. False (logged; the helper runs without it) when that is not possible."""
        path = self.path
        try:
            info = os.lstat(path)
        except FileNotFoundError:
            info = None
        except OSError as error:
            log.error("control socket %s: %s; control channel off", path, error.strerror)
            return False
        if info is not None:
            if not stat.S_ISSOCK(info.st_mode):
                log.error("control socket %s: something that is not a socket is in the way; control channel off", path)
                return False
            probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            probe.settimeout(1.0)
            try:
                probe.connect(path)
                alive = True
            except OSError:
                alive = False
            finally:
                probe.close()
            if alive:
                log.error("control socket %s is served by another process; control channel off", path)
                return False
            try:
                os.unlink(path)  # a stale socket left by a helper that did not exit cleanly
            except FileNotFoundError:
                pass
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        mask = os.umask(0o177)
        try:
            listener.bind(path)
            os.chmod(path, 0o600)
            listener.listen(8)
            self.inode = os.stat(path).st_ino
        except OSError as error:
            listener.close()
            log.error("control socket %s: %s; control channel off", path, error.strerror or error)
            return False
        finally:
            os.umask(mask)
        self.listener = listener
        threading.Thread(target=self._accept, daemon=True).start()
        log.info("control channel at %s", path)
        return True

    def _accept(self):
        while True:
            try:
                connection, _ = self.listener.accept()
            except OSError:
                return
            threading.Thread(target=self._serve, args=(connection,), daemon=True).start()

    def _peer_ok(self, connection):
        try:
            credentials = connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i"))
        except OSError:
            return False
        _pid, uid, _gid = struct.unpack("3i", credentials)
        return uid == self.uid

    @staticmethod
    def _send(connection, reply):
        connection.sendall(json.dumps(reply, separators=(",", ":")).encode() + b"\n")

    def _serve(self, connection):
        with self.lock:
            full = len(self.clients) >= CONTROL_CONNECTIONS
            if not full:
                self.clients.add(connection)
        try:
            if full:
                self._send(connection, _failure("too many control connections"))
                return
            if not self._peer_ok(connection):
                log.warning("control: refused a connection from another user")
                self._send(connection, _failure("permission denied"))
                return
            buffer = b""
            while True:
                data = connection.recv(65536)
                if not data:
                    return
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    if len(line) > CONTROL_REQUEST_CAP:
                        self._send(connection, _failure("request over 64 KiB"))
                        return
                    if line.strip():
                        self._send(connection, self.handle(line))
                if len(buffer) > CONTROL_REQUEST_CAP:
                    self._send(connection, _failure("request over 64 KiB"))
                    return
        except OSError:
            pass
        finally:
            with self.lock:
                self.clients.discard(connection)
            connection.close()

    def handle(self, line):
        try:
            request = json.loads(line)
        except (ValueError, UnicodeDecodeError):
            return _failure("request is not JSON")
        if not isinstance(request, dict) or not isinstance(request.get("op"), str):
            return _failure("request needs an op")
        op = request["op"]
        try:
            if op == "status":
                return self.runner.status(self.config_path)
            if op == "test":
                return self.runner.test(request.get("source"), self.config_path)
            if op == "secrets":
                return self.runner.secrets()
            if op in ("restart", "restart-all", "reload"):
                return self.runner.submit(request)
        except Exception as error:
            log.exception("control %s failed", op)
            return _failure(f"internal error: {type(error).__name__}")
        return _failure(f"unknown op {op!r}")

    def close(self):
        if self.listener is None:
            return
        try:
            self.listener.shutdown(socket.SHUT_RDWR)  # wakes the accepting thread
        except OSError:
            pass
        self.listener.close()
        with self.lock:
            for connection in list(self.clients):
                try:
                    connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        try:
            if os.stat(self.path).st_ino == self.inode:  # never remove a successor's socket
                os.unlink(self.path)
        except OSError:
            pass
        self.listener = None


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


def default_config_path():
    """$XDG_CONFIG_HOME/hyprune/sources.json (~/.config when unset)."""
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(base, "hyprune", "sources.json")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Publish command, HTTP and timer sources to core with data.publish.")
    parser.add_argument("--config", default=None,
                        help="sources JSON file (default $XDG_CONFIG_HOME/hyprune/sources.json); re-read on change")
    parser.add_argument("--socket", default=os.environ.get("HYPRUNE_SOCKET"),
                        help="core IPC socket (default $HYPRUNE_SOCKET); required unless --once/--dry-run")
    tokens = parser.add_mutually_exclusive_group()
    tokens.add_argument("--token-file", help="file holding the launch token (re-read on each connect)")
    tokens.add_argument("--token-fd", type=int, help="inherited descriptor to read launch token lines from")
    parser.add_argument("--token-refresh", action="store_true",
                        default=os.environ.get("HYPRUNE_TOKEN_REFRESH") == "1",
                        help="with --token-fd: write 'renew' to the descriptor for each reconnection's token "
                             "(default from HYPRUNE_TOKEN_REFRESH=1)")
    control = parser.add_mutually_exclusive_group()
    control.add_argument("--control", metavar="PATH",
                         help="control socket (default: sources.sock in the directory of --socket)")
    control.add_argument("--no-control", action="store_true", help="no control socket")
    parser.add_argument("--protocol", help=f"IPC protocol to offer (default: config, else {DEFAULT_PROTOCOL})")
    parser.add_argument("--dev", action="store_true",
                        help="prototype only: allow 1 s intervals (same as devFastIntervals)")
    parser.add_argument("--once", action="store_true", help="fetch every source once, print, exit")
    parser.add_argument("--dry-run", action="store_true", help="run the loop but print publications")
    parser.add_argument("-v", "--verbose", action="store_true", help="log one line per publication")
    parser.add_argument("--secrets", default=os.environ.get("HYPRUNE_SOURCES_SECRETS"),
                        help="secrets file (default $XDG_CONFIG_HOME/hyprune/secrets.json; mode 0600)")
    parser.add_argument("--no-keyring", action="store_true", help="never ask the desktop keyring (secret-tool)")
    args = parser.parse_args(argv)
    global SECRETS
    SECRETS = Secrets(args.secrets, keyring=not args.no_keyring)
    if args.token_refresh and args.token_fd is None:
        parser.error("--token-refresh needs --token-fd")
    args.config = args.config or default_config_path()
    logging.basicConfig(stream=sys.stderr, level=logging.DEBUG if args.verbose else logging.INFO,
                        format="source-helper: %(levelname)s %(message)s")
    config_error = None
    try:
        config = load_config(args.config, args.dev)
    except ConfigMissing as error:
        if args.once:
            log.error("%s", error)
            return 2
        log.info("%s; idle until it appears", error)
        config = empty_config(args.dev)
    except ConfigError as error:
        if args.once:
            log.error("%s", error)
            return 2
        log.error("%s; idle until the file is valid", error)
        config, config_error = empty_config(args.dev), str(error)
    if config["dev"]:
        log.info("devFastIntervals: 1 s intervals allowed (prototype simulators only)")
    if args.once:
        return run_once(config["sources"])
    token = None
    if args.token_fd is not None:
        os.set_inheritable(args.token_fd, False)  # never passed on to commands
    elif not args.token_file:
        token = os.environ.get("HYPRUNE_SOURCES_TOKEN") or os.environ.get("HYPRUNE_MACHINES_TOKEN")
    for name in ("HYPRUNE_SOURCES_TOKEN", "HYPRUNE_MACHINES_TOKEN", "HYPRUNE_TOKEN_REFRESH"):
        os.environ.pop(name, None)  # commands inherit the environment: keep tokens out of it
    if args.dry_run:
        publisher = DryRunPublisher()
    else:
        if not args.socket:
            log.error("no socket: pass --socket or set HYPRUNE_SOCKET (there is deliberately no default)")
            return 2
        if not (token or args.token_file or args.token_fd is not None):
            log.warning("no launch token: core will not grant data.publish")
        publisher = Publisher(args.socket, TokenSource(args.token_file, token, args.token_fd, args.token_refresh),
                              config["client"], args.protocol or config["protocol"])
    watcher = ConfigWatcher(args.config, config, args.dev, error=config_error)
    runner = Runner([Source(spec) for spec in config["sources"]], publisher, watcher,
                    disabled=config["disabled"], dev=config["dev"], protocol=args.protocol)
    control_path = args.control
    if control_path is None and not args.no_control and args.socket and not args.dry_run:
        control_path = os.path.join(os.path.dirname(os.path.abspath(args.socket)), "sources.sock")
    server = None
    if control_path and not args.no_control:
        server = ControlServer(control_path, runner, os.path.abspath(args.config))
        if not server.open():
            server = None
    for number in (signal.SIGTERM, signal.SIGINT):
        signal.signal(number, lambda *_: runner.stop())
    try:
        runner.run()
    except BrokenPipeError:  # --dry-run piped into a reader that went away
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
        return 1
    finally:
        if server is not None:
            server.close()
    return runner.exit_code


if __name__ == "__main__":
    sys.exit(main())
