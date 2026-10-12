"""Tests for the source helper prototype and the machine simulators.

Run from the repository root: python3 -m unittest dev/machines/test_machines.py
"""
import collections
import http.server
import importlib.util
import json
import os
import pathlib
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import unittest.mock

HERE = pathlib.Path(__file__).resolve().parent
SIMS = HERE / "sims"
SCRIPTS = HERE / "scripts"
_spec = importlib.util.spec_from_file_location("source_helper", HERE / "source-helper.py")
helper = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(helper)
helper.log.setLevel("CRITICAL")
_spec = importlib.util.spec_from_file_location("system_gauges", HERE / "scripts" / "system-gauges.py")
gauges = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gauges)


class FakeClock:
    def __init__(self, now=1000.0):
        self.now = now

    def __call__(self):
        return self.now


def spec_for(source, dev=True, client="com.hyprune.sources", base=HERE):
    return helper.SourceSpec(source, client, base, dev)


def command(argv, **extra):
    return spec_for({"id": "t", "kind": "command", "argv": argv, "intervalS": 1, "timeoutS": 5, **extra})


PY = sys.executable


class ScriptOutput(unittest.TestCase):
    def test_single_object(self):
        self.assertEqual(helper.parse_json_output('{"a": 1, "b": "x"}\n'), {"a": 1, "b": "x"})

    def test_pretty_object(self):
        self.assertEqual(helper.parse_json_output('{\n  "a": 1,\n  "b": [1]\n}\n'), {"a": 1, "b": [1]})

    def test_lines_merge_in_order(self):
        text = '{"a": 1}\n\n{"b": 2, "a": 3}\n'
        self.assertEqual(helper.parse_json_output(text), {"a": 3, "b": 2})

    def test_bad_output(self):
        for text in ["", "  \n", "[1, 2]", '{"a": 1}\nnot json', '{"a": 1}\n[1]']:
            with self.assertRaises(helper.PollError, msg=text):
                helper.parse_json_output(text)

    def test_scalars_only_and_caps(self):
        values = {"s": "x" * 400, "n": 1.5, "b": True, "i": 3, "list": [1], "obj": {}, "nan": float("nan"),
                  "_status": "mine", "k" * 129: 1}
        clean = helper.clean_values(values)
        self.assertEqual(set(clean), {"s", "n", "b", "i"})
        self.assertEqual(len(clean["s"].encode()), 256)
        many = {f"k{i}": i for i in range(40)}
        self.assertEqual(len(helper.clean_values(many, 29)), 29)

    def test_utf8_truncation_keeps_characters(self):
        text = helper.truncate_utf8("é" * 200, 256)
        self.assertEqual(text, "é" * 128)

    def test_json_without_fields_drops_non_scalars(self):
        spec = command([PY, "-c", 'print(\'{"a": 1, "b": {"c": 2}, "d": "x"}\')'])
        values, errors, meta = helper.command_values(spec)
        self.assertEqual((values, errors, meta), ({"a": 1, "d": "x"}, [], {"_exitCode": 0}))

    def test_json_lines_from_a_script(self):
        spec = command([PY, "-c", 'print(\'{"a": 1}\'); print(\'{"b": 2}\')'])
        self.assertEqual(helper.command_values(spec)[0], {"a": 1, "b": 2})

    def test_json_fields(self):
        spec = command([PY, "-c", 'print(\'{"p": [{"s": "ok", "n": "7"}]}\')'],
                       fields=[{"name": "status", "pointer": "/p/0/s", "type": "string"},
                               {"name": "count", "pointer": "/p/0/n", "type": "counter"}])
        self.assertEqual(helper.command_values(spec)[0], {"status": "ok", "count": 7})

    def test_other_outputs(self):
        lines = [PY, "-c", 'print("3.5 apples"); print("b"); print(""); print("c")']
        self.assertEqual(helper.command_values(command(lines, output="lines", key="n"))[0], {"n": 3})
        self.assertEqual(helper.command_values(command(lines, output="text"))[0], {"value": "3.5 apples"})
        number = command([PY, "-c", 'print(" 42 ")'], output="number", key="x")
        self.assertEqual(helper.command_values(number)[0], {"x": 42})
        with self.assertRaises(helper.PollError):
            helper.command_values(command(lines, output="number"))

    def test_exit_code_is_an_error(self):
        spec = command([PY, "-c", 'import sys; print("{}"); sys.stderr.write("boom\\n"); sys.exit(3)'])
        with self.assertRaises(helper.PollError) as caught:
            helper.command_values(spec)
        self.assertEqual(caught.exception.meta, {"_exitCode": 3})
        self.assertIn("boom", str(caught.exception))

    def test_stdout_cap(self):
        argv = [PY, "-c", "import sys; sys.stdout.write('x' * 70000)"]
        with self.assertRaises(helper.PollError) as caught:
            helper.run_command(argv, HERE, {}, 5)
        self.assertIn("64 KiB", str(caught.exception))

    def test_stderr_tail_kept(self):
        argv = [PY, "-c", "import sys; sys.stderr.write('a' * 9000 + 'END')"]
        code, out, err = helper.run_command(argv, HERE, {}, 5)
        self.assertEqual((code, len(err)), (0, 4096))
        self.assertTrue(err.endswith("END"))

    def test_timeout_kills_the_process_group(self):
        with tempfile.TemporaryDirectory() as tmp:
            pidfile = pathlib.Path(tmp) / "pid"
            argv = ["sh", "-c", f"sleep 30 & echo $! > {pidfile}; sleep 30"]
            start = time.monotonic()
            with self.assertRaises(helper.PollError) as caught:
                helper.run_command(argv, HERE, {}, 1)
            self.assertLess(time.monotonic() - start, 3)
            self.assertIn("timed out", str(caught.exception))
            child = int(pidfile.read_text())
            time.sleep(0.1)
            state = pathlib.Path(f"/proc/{child}/stat")
            self.assertTrue(not state.exists() or state.read_text().split()[2] == "Z")

    def test_env_and_cwd(self):
        with tempfile.TemporaryDirectory() as tmp:
            spec = spec_for({"id": "t", "kind": "command", "intervalS": 5, "timeoutS": 5, "env": {"X_TEST": "y"},
                             "argv": [PY, "-c", 'import os, json; print(json.dumps({"x": os.environ["X_TEST"], "cwd": os.getcwd()}))']},
                            dev=False, base=pathlib.Path(tmp))
            values = helper.command_values(spec)[0]
            self.assertEqual(values["x"], "y")
            self.assertEqual(pathlib.Path(values["cwd"]).resolve(), pathlib.Path(tmp).resolve())


class Pointers(unittest.TestCase):
    DOC = {"foo": ["bar", "baz"], "": 0, "a/b": 1, "c%d": 2, "e^f": 3, "g|h": 4, "i\\j": 5, "k\"l": 6,
           " ": 7, "m~n": 8}

    def test_rfc6901_examples(self):
        p = helper.json_pointer
        self.assertEqual(p(self.DOC, ""), self.DOC)
        self.assertEqual(p(self.DOC, "/foo"), ["bar", "baz"])
        self.assertEqual(p(self.DOC, "/foo/0"), "bar")
        for pointer, value in [("/", 0), ("/a~1b", 1), ("/c%d", 2), ("/e^f", 3), ("/g|h", 4), ("/i\\j", 5),
                               ('/k"l', 6), ("/ ", 7), ("/m~0n", 8)]:
            self.assertEqual(p(self.DOC, pointer), value, pointer)

    def test_errors(self):
        for pointer in ["foo", "/missing", "/foo/2", "/foo/01", "/foo/-", "/foo/0/x"]:
            with self.assertRaises(helper.FieldError, msg=pointer):
                helper.json_pointer(self.DOC, pointer)

    def test_fields_and_aggregates(self):
        doc = {"jobs": [{"status": "queued", "name": "a"}, {"status": "running", "name": "b"},
                        {"status": "queued", "name": "c"}], "build": {"number": 12, "ok": "true"}, "obj": {"x": 1}}
        fields = [{"name": "jobs", "length": "/jobs", "type": "int"},
                  {"name": "queued", "countWhere": {"array": "/jobs", "pointer": "/status", "equals": "queued"}},
                  {"name": "running", "first": {"array": "/jobs", "pointer": "/status", "equals": "running",
                                                "select": "/name"}},
                  {"name": "failed", "first": {"array": "/jobs", "pointer": "/status", "equals": "failed",
                                               "select": "/name"}, "default": "none"},
                  {"name": "number", "pointer": "/build/number", "type": "counter"},
                  {"name": "ok", "pointer": "/build/ok", "type": "bool"},
                  {"name": "obj", "pointer": "/obj"},
                  {"name": "gone", "pointer": "/nope"}]
        values, errors = helper.extract_fields(doc, fields)
        self.assertEqual(values, {"jobs": 3, "queued": 2, "running": "b", "failed": "none", "number": 12, "ok": True})
        self.assertEqual(len(errors), 2)
        self.assertTrue(errors[0].startswith("obj:") and errors[1].startswith("gone:"))

    def test_types(self):
        c = helper.coerce
        self.assertEqual(c("3", "int", "x"), 3)
        self.assertEqual(c(2.6, "int", "x"), 3)
        self.assertEqual(c(4.0, "number", "x"), 4)
        self.assertEqual(c(True, "string", "x"), "true")
        self.assertEqual(c(5, "string", "x"), "5")
        self.assertEqual(c(0, "bool", "x"), False)
        for value, kind in [(-1, "counter"), (1.5, "counter"), ("abc", "number"), (True, "int"), ("maybe", "bool")]:
            with self.assertRaises(helper.FieldError, msg=(value, kind)):
                c(value, kind, "x")


class BackoffAndSequence(unittest.TestCase):
    def test_backoff_growth_and_cap(self):
        class Mid:
            @staticmethod
            def uniform(a, b):
                return (a + b) / 2
        delays = [helper.backoff_delay(5, n, Mid) for n in range(0, 12)]
        self.assertEqual(delays[:4], [5, 10, 20, 40])
        self.assertEqual(delays[-1], 600)
        self.assertTrue(all(a <= b for a, b in zip(delays, delays[1:])))
        for n in range(1, 20):
            self.assertLessEqual(helper.backoff_delay(60, n), 600)

    def test_source_backs_off_and_resets(self):
        clock = FakeClock()
        source = helper.Source(command(["true"]), clock)
        source.succeeded({"a": 1}, [], {"_exitCode": 0})
        self.assertEqual(source.next_due, clock.now + 1)
        gaps = []
        for _ in range(4):
            source.failed(helper.PollError("exit code 1", {"_exitCode": 1}))
            gaps.append(source.next_due - clock.now)
        self.assertEqual(source.failures, 4)
        self.assertTrue(gaps[0] < gaps[1] < gaps[2] < gaps[3], gaps)
        self.assertEqual(source.publication(),
                         {"a": 1, "_status": "error", "_error": "exit code 1", "_exitCode": 1})
        source.succeeded({"a": 2}, [], {"_exitCode": 0})
        self.assertEqual((source.failures, source.next_due), (0, clock.now + 1))
        self.assertEqual(source.publication(), {"a": 2, "_status": "ok", "_exitCode": 0})

    def test_retry_after_wins(self):
        clock = FakeClock()
        source = helper.Source(command(["true"]), clock)
        source.failed(helper.PollError("HTTP 429", {"_httpStatus": 429}, retry_after=120))
        self.assertGreaterEqual(source.next_due - clock.now, 120)

    def test_field_error_keeps_last_good_value(self):
        spec = command(["true"], fields=[{"name": "a", "pointer": "/a"}, {"name": "b", "pointer": "/b"}])
        source = helper.Source(spec, FakeClock())
        source.succeeded({"a": 1, "b": 2}, [], {})
        source.succeeded({"a": 3}, ["b: /b: no member 'b'"], {})
        self.assertEqual(source.publication()["a"], 3)
        self.assertEqual(source.publication()["b"], 2)
        self.assertEqual(source.publication()["_status"], "error")
        self.assertEqual(source.failures, 0)

    def test_sequence_strictly_increases(self):
        clock = FakeClock(1_700_000_000.0)
        sequence = helper.Sequence(clock)
        seen = [sequence.next() for _ in range(5)]
        clock.now -= 10  # the wall clock steps back
        seen += [sequence.next() for _ in range(3)]
        clock.now += 100
        seen.append(sequence.next())
        self.assertTrue(all(a < b for a, b in zip(seen, seen[1:])), seen)
        self.assertEqual(seen[0], 1_700_000_000_000)
        self.assertLessEqual(seen[-1], 2**53 - 1)

    def test_sanitized_errors(self):
        message = helper.sanitize_error("GET https://ci.example.com/api?token=abc123&x=1 failed: Bearer s3cret!",
                                        ["s3cret!"])
        self.assertEqual(message, "GET https://ci.example.com/api failed: Bearer ***")
        self.assertEqual(len(helper.sanitize_error("x" * 1000)), 256)


class Publishing(unittest.TestCase):
    def source(self, ttl=8000):
        clock = FakeClock()
        return helper.Source(command(["true"], ttlMs=ttl), clock), clock

    def test_nothing_before_data(self):
        source, clock = self.source()
        self.assertIsNone(source.due_publication(clock.now))

    def test_change_then_keepalive(self):
        source, clock = self.source(ttl=8000)
        source.succeeded({"a": 1}, [], {})
        first = source.due_publication(clock.now)
        self.assertEqual(first, {"a": 1, "_status": "ok"})
        source.mark_published(first, clock.now)
        clock.now += 1
        self.assertIsNone(source.due_publication(clock.now))  # unchanged
        source.succeeded({"a": 1}, [], {})
        self.assertIsNone(source.due_publication(clock.now))  # same values again
        clock.now += 3.0  # 4 s = ttl / 2 since the publication
        self.assertEqual(source.due_publication(clock.now), first)
        self.assertEqual(source.next_publish_time(), 1000.0 + 4.0)

    def test_change_is_rate_limited(self):
        source, clock = self.source()
        source.succeeded({"a": 1}, [], {})
        source.mark_published(source.publication(), clock.now)
        source.succeeded({"a": 2}, [], {})
        clock.now += 0.05
        self.assertIsNone(source.due_publication(clock.now))
        self.assertAlmostEqual(source.next_publish_time(), 1000.1)
        clock.now += 0.06
        self.assertEqual(source.due_publication(clock.now)["a"], 2)

    def test_reconnect_republishes(self):
        source, clock = self.source()
        source.succeeded({"a": 1}, [], {})
        source.mark_published(source.publication(), clock.now)
        source.reset_connection()
        self.assertIsNotNone(source.due_publication(clock.now + 0.2))


class Config(unittest.TestCase):
    def load(self, sources, **top):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "sources.json"
            path.write_text(json.dumps({"sources": sources, **top}))
            return helper.load_config(path)

    def test_example_config_loads(self):
        config = helper.load_config(HERE / "sources.example.json")
        ids = [s.source_id for s in config["sources"]]
        self.assertEqual(ids, [f"com.hyprune.sources.{n}" for n in ["ci", "queue", "rack", "deploy", "build"]])
        self.assertTrue(all(s.ttl_ms == 8000 and s.interval == 1 for s in config["sources"]))
        for s in config["sources"]:
            self.assertTrue(helper.ID_RE.fullmatch(s.source_id))

    def test_interval_floors(self):
        fast = {"id": "a", "kind": "command", "argv": ["true"], "intervalS": 1, "timeoutS": 1}
        with self.assertRaisesRegex(helper.ConfigError, "devFastIntervals"):
            self.load([fast])
        self.assertEqual(len(self.load([fast], devFastIntervals=True)["sources"]), 1)
        self.assertEqual(self.load([dict(fast, intervalS=5)])["sources"][0].timeout, 1)
        http = {"id": "h", "kind": "http", "url": "https://example.com/x", "intervalS": 5}
        with self.assertRaises(helper.ConfigError):
            self.load([http])
        self.load([dict(http, url="http://127.0.0.1:9/x", intervalS=2)])
        with self.assertRaises(helper.ConfigError):
            self.load([dict(http, url="http://127.0.0.1:9/x", intervalS=1)])

    def test_defaults(self):
        source = self.load([{"id": "h", "kind": "http", "url": "https://example.com/x"}])["sources"][0]
        self.assertEqual((source.interval, source.timeout, source.ttl_ms), (60, 10, 60000))
        source = self.load([{"id": "c", "kind": "command", "argv": ["true"]}])["sources"][0]
        self.assertEqual((source.interval, source.timeout, source.ttl_ms), (60, 10, 60000))

    def test_rejections(self):
        bad = [
            {"id": "Bad", "kind": "command", "argv": ["true"]},
            {"id": "a", "kind": "shell", "argv": ["true"]},
            {"id": "a", "kind": "command", "argv": "true"},
            {"id": "a", "kind": "command", "argv": ["true"], "timeoutS": 90},
            {"id": "a", "kind": "command", "argv": ["true"], "ttlMs": 70000},
            {"id": "a", "kind": "command", "argv": ["true"], "fields": [{"name": "_x", "pointer": "/x"}]},
            {"id": "a", "kind": "command", "argv": ["true"], "fields": [{"name": "x"}]},
            {"id": "a", "kind": "command", "argv": ["true"], "fields": [{"name": "x", "pointer": "/x", "type": "date"}]},
            {"id": "a", "kind": "http", "url": "http://example.com/x"},
            {"id": "a", "kind": "http", "url": "file:///etc/passwd"},
            {"id": "a", "kind": "http", "url": "https://example.com/x", "method": "PUT"},
            {"id": "a", "kind": "timer", "timer": "schedule"},
        ]
        for source in bad:
            with self.assertRaises(helper.ConfigError, msg=source):
                self.load([source])
        with self.assertRaises(helper.ConfigError):
            self.load([{"id": "a", "kind": "timer", "timer": "pulse"}] * 2)
        with self.assertRaises(helper.ConfigError):
            self.load([], client={"id": "nodots"})

    def test_disabled_sources_are_skipped(self):
        config = self.load([{"id": "a", "kind": "http", "url": "http://example.com/x", "enabled": False}])
        self.assertEqual(config["sources"], [])

    def test_timers(self):
        pulse = self.load([{"id": "p", "kind": "timer", "timer": "pulse", "everyS": 10}])["sources"][0]
        self.assertEqual(helper.timer_values(pulse, 1005.0)[0], {"pulse": 100})
        until = self.load([{"id": "c", "kind": "timer", "timer": "countdown", "untilS": 1100}])["sources"][0]
        self.assertEqual(helper.timer_values(until, 1000.5)[0], {"remaining": 100})
        self.assertEqual(helper.timer_values(until, 2000)[0], {"remaining": 0})
        span = self.load([{"id": "d", "kind": "timer", "timer": "countdown", "durationS": 30}])["sources"][0]
        self.assertEqual(helper.timer_values(span, 50.0)[0], {"remaining": 30})
        self.assertEqual(helper.timer_values(span, 70.0)[0], {"remaining": 10})


class _Handler(http.server.BaseHTTPRequestHandler):
    routes = {}
    seen = []

    def log_message(self, *args):
        pass

    def _reply(self):
        self.seen.append((self.command, self.path, dict(self.headers),
                          self.rfile.read(int(self.headers.get("Content-Length", 0) or 0))))
        status, headers, body = self.routes[self.path.split("?")[0]]
        self.send_response(status)
        for k, v in headers.items():
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    do_GET = do_POST = _reply


class Http(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _Handler)
        cls.other = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _Handler)
        for server in (cls.server, cls.other):
            threading.Thread(target=server.serve_forever, daemon=True).start()
        base = f"http://127.0.0.1:{cls.server.server_port}"
        other = f"http://127.0.0.1:{cls.other.server_port}"
        doc = json.dumps({"current": {"temperature": 21.5, "name": "x"}, "list": [1, 2], "obj": {}}).encode()
        _Handler.routes = {
            "/ok": (200, {"Content-Type": "application/json"}, doc),
            "/busy": (429, {"Retry-After": "90"}, b"{}"),
            "/big": (200, {}, b'{"a": "' + b"x" * (1024 * 1024) + b'"}'),
            "/text": (200, {}, b"<html>"),
            "/same": (302, {"Location": base + "/ok"}, b""),
            "/away": (302, {"Location": other + "/ok"}, b""),
        }
        cls.base = base

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.other.shutdown()

    def spec(self, path, **extra):
        return spec_for({"id": "h", "kind": "http", "url": self.base + path, "intervalS": 2, "timeoutS": 5,
                         **extra}, dev=False)

    def test_fields(self):
        spec = self.spec("/ok?key=1", fields=[{"name": "temp", "pointer": "/current/temperature", "type": "number"},
                                               {"name": "items", "length": "/list"}])
        self.assertEqual(helper.http_values(spec), ({"temp": 21.5, "items": 2}, [], {"_httpStatus": 200}))

    def test_without_fields_publishes_scalars(self):
        self.assertEqual(helper.http_values(self.spec("/ok"))[0], {})  # top level has objects only

    def test_non_scalar_target_is_a_field_error(self):
        spec = self.spec("/ok", fields=[{"name": "obj", "pointer": "/obj"}])
        values, errors, _ = helper.http_values(spec)
        self.assertEqual(values, {})
        self.assertIn("not a scalar", errors[0])

    def test_post_body_and_secret_header(self):
        os.environ["HYPRUNE_SOURCES_SECRET_CI_TOKEN"] = "abcd1234"
        try:
            spec = self.spec("/ok", method="POST", body={"q": 1},
                             headers={"Authorization": {"prefix": "Bearer ", "secret": "ci-token"}},
                             fields=[{"name": "n", "pointer": "/current/name"}])
            helper.http_values(spec)
        finally:
            del os.environ["HYPRUNE_SOURCES_SECRET_CI_TOKEN"]
        method, _, headers, body = _Handler.seen[-1]
        self.assertEqual((method, json.loads(body), headers["Authorization"]), ("POST", {"q": 1}, "Bearer abcd1234"))

    def test_missing_secret(self):
        spec = self.spec("/ok", headers={"X-Key": {"secret": "absent"}})
        with self.assertRaisesRegex(helper.PollError, "secret"):
            helper.http_values(spec)

    def test_429_retry_after(self):
        with self.assertRaises(helper.PollError) as caught:
            helper.http_values(self.spec("/busy"))
        self.assertEqual((caught.exception.retry_after, caught.exception.meta), (90.0, {"_httpStatus": 429}))

    def test_body_cap_is_an_error(self):
        with self.assertRaisesRegex(helper.PollError, "1 MiB"):
            helper.http_values(self.spec("/big"))

    def test_not_json(self):
        with self.assertRaisesRegex(helper.PollError, "not JSON"):
            helper.http_values(self.spec("/text"))

    def test_redirects_stay_in_origin(self):
        spec = self.spec("/same", fields=[{"name": "t", "pointer": "/current/temperature"}])
        self.assertEqual(helper.http_values(spec)[0], {"t": 21.5})
        with self.assertRaisesRegex(helper.PollError, "another origin"):
            helper.http_values(self.spec("/away"))


# --- simulators ----------------------------------------------------------------------

T0 = 1_800_000_000 - 600  # crosses a template super-cycle boundary of every block simulator
KEYS = {
    "ci": ["stage.1", "stage.2", "stage.3", "stage.4", "status", "jobsStarted", "jobsSucceeded", "jobsFailed",
           "queued", "active", "progress", "lastJob", "lastResult"],
    "queue": ["depth", "oldestAgeS", "rateIn", "rateOut", "level"],
    "rack": [f"health.{i}" for i in range(1, 9)] + [f"load.{i}" for i in range(1, 9)] + ["load", "down", "name"],
    "deploy": ["phase", "progress", "version", "environment", "deploys"],
    "build": ["status", "progress", "successes", "failures", "streak", "lastJob"],
}
COUNTERS = {"ci": ["jobsStarted", "jobsSucceeded", "jobsFailed"], "deploy": ["deploys"],
            "build": ["successes", "failures"]}
_timelines = {}


def sim_env(**extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith("MACHINES_SIM_")}
    env.update(extra)
    return env


def timeline(name, start=T0, seconds=1800, step=1, **env):
    key = (name, start, seconds, step, tuple(sorted(env.items())))
    if key not in _timelines:
        out = subprocess.run([str(SIMS / f"{name}-sim.py"), "--timeline", str(start), str(start + seconds), str(step)],
                             capture_output=True, text=True, check=True, env=sim_env(**env)).stdout
        _timelines[key] = [json.loads(line) for line in out.splitlines()]
    return _timelines[key]


def increments(rows, key):
    return [i for i in range(1, len(rows)) if rows[i][key] > rows[i - 1][key]]


class Simulators(unittest.TestCase):
    def test_keys_exactly_and_scalar(self):
        for name, keys in KEYS.items():
            rows = timeline(name)
            self.assertEqual(len(rows), 1801)
            for row in rows:
                self.assertEqual(list(row), keys, name)
                self.assertTrue(all(helper.is_scalar(v) for v in row.values()))
                self.assertLessEqual(helper.values_size(row), 16384)

    def test_counters_monotonic_and_moving(self):
        for name, keys in COUNTERS.items():
            rows = timeline(name)
            for key in keys:
                values = [r[key] for r in rows]
                self.assertTrue(all(isinstance(v, int) and v >= 0 for v in values))
                self.assertTrue(all(a <= b for a, b in zip(values, values[1:])), (name, key))
                self.assertGreater(values[-1], values[0], (name, key))

    def test_counters_monotonic_on_a_coarse_day(self):
        for name, keys in COUNTERS.items():
            rows = timeline(name, start=T0, seconds=86400, step=97)
            for key in keys:
                values = [r[key] for r in rows]
                self.assertTrue(all(a <= b for a, b in zip(values, values[1:])), (name, key))

    def test_ci(self):
        rows = timeline("ci")
        states = {"pending", "running", "success", "failed"}
        for r in rows:
            for k in range(1, 5):
                self.assertIn(r[f"stage.{k}"], states)
            self.assertIn(r["status"], states)
            self.assertIn(r["lastResult"], {"success", "failed", "none"})
            self.assertTrue(0 <= r["progress"] <= 1)
            self.assertTrue(0 <= r["queued"] <= 12)
            self.assertTrue(r["queued"] <= r["active"])
            self.assertEqual(r["active"], r["jobsStarted"] - r["jobsSucceeded"] - r["jobsFailed"])
            self.assertRegex(r["lastJob"], r"^main #\d+$")
        self.assertTrue(increments(rows, "jobsFailed") and increments(rows, "jobsSucceeded"))
        started = increments(rows, "jobsStarted")
        self.assertTrue(150 <= len(started) <= 360, len(started))  # a job every ~5-9 s while busy
        seen = collections.Counter(r[f"stage.{k}"] for r in rows for k in range(1, 5))
        self.assertTrue(all(seen[s] for s in states), seen)
        self.assertTrue(any(r["queued"] for r in rows))
        # A job's number appears when it starts.
        for i in started:
            self.assertEqual(rows[i]["lastJob"], f"main #{1000 + rows[i]['jobsStarted'] % 100000}")

    def test_queue(self):
        rows = timeline("queue")
        for r in rows:
            self.assertTrue(0 <= r["depth"] <= 500 and r["oldestAgeS"] >= 0)
            self.assertTrue(r["rateIn"] >= 0 and r["rateOut"] >= 0)
            expected = "fail" if r["oldestAgeS"] > 90 else "warn" if r["oldestAgeS"] > 30 else "ok"
            self.assertEqual(r["level"], expected)
        # Backlogs are occasional (a 45% chance per 7 minutes): look over two hours.
        longer = timeline("queue", seconds=7200, step=2)
        self.assertGreaterEqual(max(r["depth"] for r in longer), 300)
        self.assertEqual({r["level"] for r in longer}, {"ok", "warn", "fail"})
        jumps = [abs(a["depth"] - b["depth"]) for a, b in zip(rows, rows[1:])]
        self.assertLess(max(jumps), 30)  # continuous at 1 s steps

    def test_rack(self):
        rows = timeline("rack")
        for r in rows:
            loads = [r[f"load.{i}"] for i in range(1, 9)]
            health = [r[f"health.{i}"] for i in range(1, 9)]
            self.assertTrue(all(0 <= x <= 1 for x in loads))
            self.assertAlmostEqual(r["load"], sum(loads) / 8, places=2)
            self.assertEqual(r["down"], health.count("fail"))
            self.assertLessEqual(r["down"], 2)
            for h, x in zip(health, loads):
                self.assertIn(h, {"ok", "warn", "fail", "off"})
                if h in ("fail", "off"):
                    self.assertEqual(x, 0)
                else:
                    self.assertEqual(h == "warn", x > 0.85)
            self.assertEqual(r["name"], "rack-a")
        self.assertTrue(any(r["down"] for r in rows))
        self.assertTrue(any(r[f"health.{i}"] == "warn" for r in rows for i in range(1, 9)))
        outage = [i for i, r in enumerate(rows) if r["down"]]
        self.assertTrue(20 <= len(outage) <= 900)

    def test_deploy(self):
        rows = timeline("deploy")
        phases = collections.Counter(r["phase"] for r in rows)
        self.assertEqual(set(phases), {"queued", "building", "rolling", "live", "failed"})
        for a, b in zip(rows, rows[1:]):
            self.assertTrue(0 <= b["progress"] <= 1)
            self.assertRegex(b["version"], r"^v\d+\.\d+\.\d+$")
            self.assertEqual(b["environment"], "production")
            if b["phase"] == "live":
                self.assertEqual(b["progress"], 1)
            if b["phase"] == "building":
                self.assertLessEqual(b["progress"], 0.4)
            if b["deploys"] > a["deploys"]:
                self.assertEqual((b["phase"], b["deploys"] - a["deploys"]), ("live", 1))
            if a["phase"] == "failed" and b["phase"] != "failed":
                self.assertEqual(b["phase"], "queued")
                self.assertEqual(b["version"], a["version"])  # a retry of the same version
        held = [len(list(g)) for g in _runs([r["phase"] for r in rows]) if g[0] == "failed"]
        self.assertTrue(held and all(7 <= h <= 9 for h in held[1:-1] or held), held)
        self.assertTrue(20 <= len(increments(rows, "deploys")) <= 30)

    def test_build(self):
        rows = timeline("build")
        for r in rows:
            self.assertIn(r["status"], {"idle", "running", "success", "failed"})
            self.assertTrue(0 <= r["progress"] <= 1)
            if r["status"] == "idle":
                self.assertEqual(r["progress"], 0)
            self.assertRegex(r["lastJob"], r"^web #\d+$")
        ok, bad = increments(rows, "successes"), increments(rows, "failures")
        self.assertTrue(ok and bad)
        for i in bad:
            self.assertEqual(rows[i]["streak"], 0)
            self.assertEqual(rows[i]["status"], "failed")
        for i in ok:
            self.assertEqual(rows[i]["streak"], rows[i - 1]["streak"] + 1)
            self.assertEqual(rows[i]["status"], "success")
        # A burst: three or more successes within 30 s.
        self.assertTrue(any(ok[j + 2] - ok[j] <= 30 for j in range(len(ok) - 2)))

    def test_deterministic_seeded_and_epoch(self):
        for name in KEYS:
            a = timeline(name, start=T0, seconds=20, step=5)
            b = timeline(name, start=T0, seconds=20, step=5, MACHINES_SIM_SEED="hyprune")
            other = timeline(name, start=T0, seconds=20, step=5, MACHINES_SIM_SEED="other")
            self.assertEqual(a, b)
            self.assertNotEqual(a, other, name)
            now = json.loads(subprocess.run([str(SIMS / f"{name}-sim.py")], capture_output=True, text=True,
                                            check=True, env=sim_env(MACHINES_SIM_TIME=str(T0))).stdout)
            self.assertEqual(now, a[0])
        for name, keys in COUNTERS.items():
            before = timeline(name, start=T0 - 10, seconds=10, step=10, MACHINES_SIM_EPOCH=str(T0))
            self.assertTrue(all(r[k] == 0 for r in before for k in keys))
            after = timeline(name, start=T0, seconds=1800, step=10, MACHINES_SIM_EPOCH=str(T0))
            self.assertTrue(all(after[-1][k] < 1000 for k in keys))

    def test_fast(self):
        for name in KEYS:
            spec = importlib.util.spec_from_file_location(f"{name}_sim", SIMS / f"{name}-sim.py")
            sys.path.insert(0, str(SIMS))
            try:
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                start = time.perf_counter()
                module.simulate(T0 + 123.4)
                self.assertLess(time.perf_counter() - start, 0.1, name)
            finally:
                sys.path.remove(str(SIMS))


def _runs(items):
    runs, current = [], []
    for item in items:
        if current and item != current[0]:
            runs.append(current)
            current = []
        current.append(item)
    if current:
        runs.append(current)
    return runs


# --- gauge scripts -------------------------------------------------------------------

def gauge_sim(*args, **env):
    out = subprocess.run([str(SCRIPTS / "gauge-sim.py"), *args], capture_output=True, text=True, check=True,
                         env=sim_env(**env)).stdout
    return [json.loads(line) for line in out.splitlines()]


def expected_level(value):
    return "fail" if value >= 90 else "warn" if value >= 75 else "ok"


class GaugeSim(unittest.TestCase):
    def test_domains_and_keys(self):
        rows = gauge_sim("--label", "RAM", "--timeline", str(T0), str(T0 + 600), "1")
        self.assertEqual(len(rows), 601)
        for row in rows:
            self.assertEqual(list(row), ["value", "display", "label", "level"])
            self.assertTrue(0 <= row["value"] <= 100)
            self.assertEqual(row["display"], f"{int(row['value'] + 0.5)}%")
            self.assertEqual(row["label"], "RAM")
            self.assertEqual(row["level"], expected_level(row["value"]))
        self.assertEqual({r["level"] for r in rows}, {"ok", "warn", "fail"})

    def test_piecewise_constant_steps(self):
        rows = gauge_sim("--timeline", str(T0), str(T0 + 119), "1", "--period", "6", MACHINES_SIM_EPOCH=str(T0))
        steps = [rows[i:i + 6] for i in range(0, 120, 6)]
        self.assertTrue(all(len({r["value"] for r in step}) == 1 for step in steps))
        self.assertGreater(len({step[0]["value"] for step in steps}), 15)

    def test_deterministic_seeded_and_labelled(self):
        args = ["--label", "CPU", "--timeline", str(T0), str(T0 + 60), "6"]
        self.assertEqual(gauge_sim(*args), gauge_sim(*args))
        self.assertNotEqual(gauge_sim(*args), gauge_sim(*args, MACHINES_SIM_SEED="other"))
        self.assertNotEqual([r["value"] for r in gauge_sim(*args)],
                            [r["value"] for r in gauge_sim("--label", "GPU", *args[2:])])
        self.assertEqual(gauge_sim("--label", "CPU", "--at", str(T0 + 12))[0],
                         gauge_sim("--label", "CPU", MACHINES_SIM_TIME=str(T0 + 12))[0])
        self.assertEqual(gauge_sim("--unit", " GB", "--at", str(T0))[0]["display"][-3:], " GB")

    def test_hold_stops_changing(self):
        epoch = {"MACHINES_SIM_EPOCH": str(T0)}
        rows = gauge_sim("--steps", "4", "--hold", "--timeline", str(T0 - 12), str(T0 + 120), "1", **epoch)
        values = [r["value"] for r in rows]
        self.assertEqual(values[:12], [values[12]] * 12)  # before the epoch: the first step
        held = values[12 + 24:]
        self.assertEqual(set(held), {values[12 + 18]})  # the 4th step, for good
        self.assertGreater(len(set(values[12:12 + 24])), 1)
        looped = [r["value"] for r in gauge_sim("--steps", "4", "--timeline", str(T0), str(T0 + 47), "1", **epoch)]
        self.assertEqual(looped[:24], looped[24:])

    def test_bad_arguments(self):
        for args in (["--period", "0"], ["--steps", "-1"]):
            result = subprocess.run([str(SCRIPTS / "gauge-sim.py"), *args], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn("Traceback", result.stderr)


class FakeSystem:
    """A fake /proc and /sys tree for system-gauges.py."""

    def __init__(self, root):
        self.root = pathlib.Path(root)

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def stat(self, total, idle):
        # user nice system idle iowait irq softirq steal guest guest_nice
        self.write("proc/stat", f"cpu  {total - idle} 0 0 {idle} 0 0 0 0 7 7\ncpu0 1 2 3 4\n")

    def gpu(self, bus, vendor, status="active", boot="0", **files):
        base = f"sys/bus/pci/devices/{bus}"
        self.write(f"{base}/class", "0x030000")
        self.write(f"{base}/vendor", vendor)
        self.write(f"{base}/boot_vga", boot)
        self.write(f"{base}/power/runtime_status", status)
        for name, value in files.items():
            self.write(f"{base}/{name.replace('__', '/')}", str(value))
        return self.root / base

    def opens(self, pid, target):
        fd = self.root / f"proc/{pid}/fd"
        fd.mkdir(parents=True, exist_ok=True)
        os.symlink(target, fd / str(len(list(fd.iterdir())) + 3))


class Recorder:
    def __init__(self, stdout="37, 2048, 8192\n", error=None):
        self.calls, self.stdout, self.error = [], stdout, error

    def __call__(self, argv, **kwargs):
        self.calls.append(argv)
        if self.error:
            raise self.error
        return subprocess.CompletedProcess(argv, 0, self.stdout, "")


class SystemGauges(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.system = FakeSystem(self.tmp.name)
        self.root = self.system.root

    def tearDown(self):
        self.tmp.cleanup()

    def measure(self, kind, choice="auto", run=None, which=lambda name: "/usr/bin/" + name):
        return gauges.measure(kind, self.root, choice, run=run or Recorder(), which=which)

    def test_cpu_first_run_samples_then_uses_the_state(self):
        state = self.root / "state.json"
        self.system.stat(1000, 800)
        clock = FakeClock(50.0)

        def sleep(seconds):
            self.assertEqual(seconds, 0.25)
            self.system.stat(1100, 850)  # 50 % busy over the sample
            clock.now += seconds

        first = gauges.cpu(self.root, state, sleep, clock)
        self.assertEqual(first, {"value": 50.0, "display": "50%", "label": "CPU", "level": "ok"})
        self.system.stat(1200, 860)  # 90 % busy since the last run
        clock.now += 5
        self.assertEqual(gauges.cpu(self.root, state, lambda s: self.fail("sampled again"), clock)["level"], "fail")
        clock.now += 500  # a stale state samples afresh
        sampled = []
        gauges.cpu(self.root, state, sampled.append, clock)
        self.assertEqual(sampled, [0.25])
        self.assertEqual(state.stat().st_mode & 0o777, 0o600)

    def test_ram(self):
        self.system.write("proc/meminfo", "MemTotal:       16777216 kB\nMemFree:  1000 kB\nMemAvailable:    4194304 kB\n")
        self.assertEqual(self.measure("ram"), {"value": 75.0, "display": "12.0 GB", "label": "RAM", "level": "warn"})

    def test_suspended_nvidia_is_never_woken(self):
        self.system.gpu("0000:00:02.0", "0x8086", boot="1")
        self.system.gpu("0000:01:00.0", "0x10de", status="suspended")
        self.system.opens(100, "/dev/nvidia0")
        run = Recorder()
        for kind in ("gpu", "vram"):
            self.assertEqual(self.measure(kind, run=run), {"value": 0, "display": "asleep", "label": kind.upper(),
                                                           "level": "ok"})
        self.assertEqual(run.calls, [])

    def test_nvidia_queried_only_while_in_use(self):
        self.system.gpu("0000:01:00.0", "0x10de")
        run = Recorder()
        self.assertEqual(self.measure("gpu", run=run)["display"], "idle")
        self.assertEqual(run.calls, [])
        self.system.opens(100, "/dev/nvidiactl")
        self.assertEqual(self.measure("gpu", run=run)["display"], "idle")
        self.system.opens(101, "/dev/nvidia0")
        self.assertEqual(self.measure("gpu", run=run), {"value": 37.0, "display": "37%", "label": "GPU", "level": "ok"})
        self.assertEqual(self.measure("vram", run=run), {"value": 25.0, "display": "2.0 GB", "label": "VRAM", "level": "ok"})
        self.assertEqual(run.calls[0], gauges.NVIDIA_QUERY)
        self.assertEqual(self.measure("gpu", which=lambda name: None)["display"], "n/a")
        self.assertEqual(self.measure("gpu", run=Recorder(error=subprocess.TimeoutExpired("nvidia-smi", 3)))["display"], "n/a")
        self.assertEqual(self.measure("gpu", run=Recorder(stdout="[N/A], 1, 2\n"))["display"], "n/a")
        self.assertEqual(self.measure("gpu", run=Recorder(stdout="95, 1, 2\n"))["level"], "fail")

    def test_amd(self):
        self.system.gpu("0000:03:00.0", "0x1002", gpu_busy_percent=80, mem_info_vram_used=8 * 2**30,
                        mem_info_vram_total=16 * 2**30)
        self.assertEqual(self.measure("gpu"), {"value": 80.0, "display": "80%", "label": "GPU", "level": "warn"})
        self.assertEqual(self.measure("vram"), {"value": 50.0, "display": "8.0 GB", "label": "VRAM", "level": "ok"})
        self.system.write("sys/bus/pci/devices/0000:03:00.0/power/runtime_status", "suspended")
        self.assertEqual(self.measure("gpu")["display"], "asleep")

    def test_intel_and_choice(self):
        self.system.gpu("0000:00:02.0", "0x8086", boot="1", drm__card1__gt_act_freq_mhz=450, drm__card1__gt_max_freq_mhz=1500)
        self.assertEqual(self.measure("gpu"), {"value": 30.0, "display": "~30%", "label": "GPU", "level": "ok"})
        self.assertEqual(self.measure("vram")["display"], "shared")
        self.system.gpu("0000:01:00.0", "0x10de", status="suspended")
        self.assertEqual(self.measure("gpu")["display"], "asleep")  # auto prefers the dGPU
        self.assertEqual(self.measure("gpu", "intel")["display"], "~30%")
        self.assertEqual(self.measure("gpu", "amd")["display"], "n/a")

    def test_intel_xe(self):
        self.system.gpu("0000:00:02.0", "0x8086", tile0__gt0__freq0__act_freq=0, tile0__gt0__freq0__max_freq=2000)
        self.assertEqual(self.measure("gpu")["display"], "~0%")

    def test_nothing_readable(self):
        for kind in ("cpu", "ram", "gpu", "vram"):
            self.assertEqual(self.measure(kind), {"value": 0, "display": "n/a", "label": kind.upper(), "level": "ok"})

    def test_script_runs(self):
        env = dict(os.environ, XDG_RUNTIME_DIR=self.tmp.name)
        for kind in ("cpu", "ram"):
            result = subprocess.run([str(SCRIPTS / "system-gauges.py"), kind], capture_output=True, text=True, env=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            row = json.loads(result.stdout)
            self.assertEqual(list(row), ["value", "display", "label", "level"])
            self.assertTrue(0 <= row["value"] <= 100)
        self.assertTrue((self.root / gauges.STATE_NAME).exists())

    def test_example_config_loads(self):
        config = helper.load_config(HERE / "sources.gauges.example.json")
        self.assertEqual([s.source_id for s in config["sources"]],
                         [f"com.hyprune.sources.u-00000{n}" for n in range(1, 5)])
        self.assertFalse(config["dev"])
        self.assertTrue(all(s.interval >= 5 for s in config["sources"]))
        for spec in config["sources"]:
            self.assertTrue(pathlib.Path(spec.argv[0]).is_file())


# --- the helper against a fake core ---------------------------------------------------

class FakeCore:
    """A Unix socket speaking core's framing: 4-byte big-endian length + JSON-RPC 2.0."""

    def __init__(self, path, token, refuse_first_publish=False):
        # Launch tokens are one-use, like core's.
        self.path, self.refuse = path, refuse_first_publish
        self.tokens = {token} if isinstance(token, str) else set(token)
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener.bind(path)
        self.listener.listen(4)
        self.requests, self.problems, self.connections = [], [], []
        self.lock = threading.Lock()
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        while True:
            try:
                connection, _ = self.listener.accept()
            except OSError:
                return
            with self.lock:
                self.connections.append(connection)
            threading.Thread(target=self.session, args=(connection,), daemon=True).start()

    def grant(self, token):
        with self.lock:
            self.tokens.add(token)

    def kick(self):
        """Drop every open connection (the helper must reconnect with a fresh token)."""
        with self.lock:
            connections, self.connections = self.connections, []
        for connection in connections:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def session(self, connection):
        ids, hello = set(), False

        def exact(n):
            data = b""
            while len(data) < n:
                chunk = connection.recv(n - len(data))
                if not chunk:
                    raise EOFError
                data += chunk
            return data

        try:
            while True:
                request = json.loads(exact(struct.unpack(">I", exact(4))[0]))
                with self.lock:
                    self.requests.append(request)
                if set(request) != {"jsonrpc", "id", "method", "params"} or request["jsonrpc"] != "2.0" \
                        or not isinstance(request["id"], str) or request["id"] in ids:
                    self.problems.append(f"bad request profile: {request}")
                ids.add(request["id"])
                method, params = request["method"], request["params"]
                result, error = {}, None
                if not hello:
                    if method != "session.hello":
                        self.problems.append("hello required")
                    hello = True
                    with self.lock:
                        granted = params.get("token") in self.tokens
                        self.tokens.discard(params.get("token"))
                    result = {"protocol": params["protocols"][0], "sessionId": "s",
                              "capabilities": ["data.publish"] if granted else [],
                              "methods": ["session.hello", "session.ping"] + (["data.publish", "data.remove"] if granted else []),
                              "events": []}
                elif method == "data.publish":
                    if self.refuse:
                        self.refuse = False
                        error = {"code": -32008, "message": "publication rate exceeds 10 Hz"}
                elif method != "data.remove":
                    error = {"code": -32601, "message": "unknown method"}
                reply = {"jsonrpc": "2.0", "id": request["id"]}
                reply.update({"error": error} if error else {"result": result})
                data = json.dumps(reply).encode()
                connection.sendall(struct.pack(">I", len(data)) + data)
        except (EOFError, OSError):
            pass
        finally:
            connection.close()

    def methods(self, name):
        with self.lock:
            return [r for r in self.requests if r["method"] == name]

    def close(self):
        self.listener.close()


class HelperAgainstFakeCore(unittest.TestCase):
    def test_hello_publish_and_remove_on_sigterm(self):
        token = "ab" * 32
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            core = FakeCore(str(tmp / "control.sock"), token, refuse_first_publish=True)
            config = tmp / "sources.json"
            config.write_text(json.dumps({"devFastIntervals": True, "sources": [
                {"id": "ci", "kind": "command", "argv": [str(SIMS / "ci-sim.py")], "intervalS": 1, "timeoutS": 2,
                 "ttlMs": 8000},
                {"id": "tick", "kind": "timer", "timer": "pulse", "everyS": 1},
                {"id": "off", "kind": "command", "argv": ["true"], "enabled": False}]}))
            (tmp / "token").write_text(token + "\n")
            process = subprocess.Popen([PY, str(HERE / "source-helper.py"), "--config", str(config),
                                        "--socket", str(tmp / "control.sock"), "--token-file", str(tmp / "token"), "-v"],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    ci = [r for r in core.methods("data.publish") if r["params"]["sourceId"].endswith(".ci")]
                    if len(ci) >= 3:
                        break
                    time.sleep(0.05)
                process.send_signal(signal.SIGTERM)
                out, err = process.communicate(timeout=10)
            finally:
                if process.poll() is None:
                    process.kill()
                core.close()
            self.assertEqual(process.returncode, 0, err)
            self.assertEqual(core.problems, [])
            hello = core.requests[0]
            self.assertEqual(hello["method"], "session.hello")
            self.assertEqual(hello["params"], {"protocols": ["0.2"], "client": {"id": "com.hyprune.sources",
                                               "name": "Source helper (prototype)"},
                                               "capabilities": ["data.publish"], "token": token})
            publishes = core.methods("data.publish")
            by_source = collections.defaultdict(list)
            for request in publishes:
                params = request["params"]
                self.assertEqual(set(params), {"sourceId", "sequence", "values", "ttlMs"})
                by_source[params["sourceId"]].append(params)
            self.assertEqual(set(by_source), {"com.hyprune.sources.ci", "com.hyprune.sources.tick"})
            ci = by_source["com.hyprune.sources.ci"]
            self.assertGreaterEqual(len(ci), 3)  # the refused first publication was retried
            sequences = [p["sequence"] for p in ci]
            self.assertTrue(all(a < b for a, b in zip(sequences, sequences[1:])))
            for params in ci:
                self.assertEqual(params["ttlMs"], 8000)
                values = params["values"]
                self.assertEqual(list(values), KEYS["ci"] + ["_status", "_exitCode"])
                self.assertEqual((values["_status"], values["_exitCode"]), ("ok", 0))
            self.assertEqual(by_source["com.hyprune.sources.tick"][0]["ttlMs"], 10000)
            self.assertIn("pulse", by_source["com.hyprune.sources.tick"][0]["values"])
            removes = [r["params"] for r in core.methods("data.remove")]
            self.assertEqual(sorted(p["sourceId"] for p in removes),
                             ["com.hyprune.sources.ci", "com.hyprune.sources.tick"])
            self.assertGreater(core.requests.index(core.methods("data.remove")[0]),
                               core.requests.index(publishes[-1]))
            self.assertRegex(err, re.compile(r"publish com\.hyprune\.sources\.ci #\d+ "))

    def test_reconnects_without_grant_then_with_fresh_token(self):
        token = "cd" * 32
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            core = FakeCore(str(tmp / "control.sock"), token)
            config = tmp / "sources.json"
            config.write_text(json.dumps({"sources": [{"id": "tick", "kind": "timer", "timer": "pulse", "everyS": 1}]}))
            (tmp / "token").write_text("ef" * 32)  # not granted
            process = subprocess.Popen([PY, str(HERE / "source-helper.py"), "--config", str(config),
                                        "--socket", str(tmp / "control.sock"), "--token-file", str(tmp / "token")],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                time.sleep(0.5)
                self.assertEqual(core.methods("data.publish"), [])
                (tmp / "token").write_text(token)
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline and not core.methods("data.publish"):
                    time.sleep(0.05)
                process.send_signal(signal.SIGINT)
                _, err = process.communicate(timeout=10)
            finally:
                if process.poll() is None:
                    process.kill()
                core.close()
            self.assertTrue(core.methods("data.publish"), err)
            self.assertGreaterEqual(len(core.methods("session.hello")), 2)
            self.assertIn("no data.publish grant", err)
            self.assertEqual(process.returncode, 0)


class Tokens(unittest.TestCase):
    def test_default_config_path(self):
        with unittest.mock.patch.dict(os.environ, {"XDG_CONFIG_HOME": "/config/home"}):
            self.assertEqual(helper.default_config_path(), "/config/home/hyprune/sources.json")
        with unittest.mock.patch.dict(os.environ, {"HOME": "/home/someone"}):
            os.environ.pop("XDG_CONFIG_HOME", None)
            self.assertEqual(helper.default_config_path(), "/home/someone/.config/hyprune/sources.json")

    def test_fd_renews_after_the_first_connection(self):
        a, b = socket.socketpair()
        with a, b:
            b.sendall(b"aa" * 32 + b"\n")
            tokens = helper.TokenSource(fd=a.fileno(), refresh=True, read_timeout=1)
            self.assertEqual(tokens.get(), "aa" * 32)
            b.setblocking(False)
            with self.assertRaises(BlockingIOError):
                b.recv(16)  # the first token is the launch grant: no renewal asked
            b.setblocking(True)
            for token in ("bb" * 32, "cc" * 32):
                b.sendall(token.encode() + b"\n")
                self.assertEqual(tokens.get(), token)
                self.assertEqual(b.recv(16), b"renew\n")

    def test_fd_timeout_does_not_pile_up_renewals(self):
        a, b = socket.socketpair()
        with a, b:
            b.sendall(b"aa" * 32 + b"\n")
            tokens = helper.TokenSource(fd=a.fileno(), refresh=True, read_timeout=0.1)
            tokens.get()
            self.assertIsNone(tokens.get())  # the supervisor did not answer in time
            self.assertIsNone(tokens.get())
            b.settimeout(1)
            self.assertEqual(b.recv(64), b"renew\n")  # one request outstanding, not two
            b.sendall(b"bb" * 32 + b"\n")
            self.assertEqual(tokens.get(), "bb" * 32)  # the late answer is used
            b.setblocking(False)
            with self.assertRaises(BlockingIOError):
                b.recv(16)

    def test_fd_without_refresh_and_eof(self):
        read, write = os.pipe()
        os.write(write, b"dd" * 32)  # no newline, then EOF
        os.close(write)
        try:
            tokens = helper.TokenSource(fd=read, read_timeout=1)
            self.assertEqual(tokens.get(), "dd" * 32)
            self.assertIsNone(tokens.get())
            self.assertTrue(tokens.closed)
        finally:
            os.close(read)

    def test_bad_token_line_is_ignored(self):
        a, b = socket.socketpair()
        with a, b:
            b.sendall(b"not-a-token\n")
            self.assertIsNone(helper.TokenSource(fd=a.fileno(), read_timeout=1).get())


def wait_for(test, seconds=15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if test():
            return True
        time.sleep(0.05)
    return False


def write_atomic(path, data):
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(data))
    os.replace(temp, path)


class HelperRenewalAndReload(unittest.TestCase):
    def published(self, core, slug, key=None):
        return [r["params"] for r in core.methods("data.publish")
                if r["params"]["sourceId"] == f"com.hyprune.sources.{slug}" and (key is None or key in r["params"]["values"])]

    def test_renews_the_grant_over_the_token_fd(self):
        first, second = "12" * 32, "34" * 32
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            core = FakeCore(str(tmp / "control.sock"), first)
            config = tmp / "sources.json"
            config.write_text(json.dumps({"sources": [{"id": "tick", "kind": "timer", "timer": "pulse", "everyS": 1}]}))
            supervisor, child = socket.socketpair()
            supervisor.sendall(first.encode() + b"\n")
            process = subprocess.Popen([PY, str(HERE / "source-helper.py"), "--config", str(config),
                                        "--socket", str(tmp / "control.sock"), "--token-fd", str(child.fileno()),
                                        "--token-refresh", "-v"],
                                       pass_fds=(child.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            child.close()
            try:
                self.assertTrue(wait_for(lambda: self.published(core, "tick")))
                supervisor.settimeout(1)
                with self.assertRaises(socket.timeout):
                    supervisor.recv(16)  # connected with the launch token alone
                core.kick()
                supervisor.settimeout(10)
                self.assertEqual(supervisor.recv(16), b"renew\n")
                core.grant(second)
                supervisor.sendall(second.encode() + b"\n")
                count = len(core.methods("data.publish"))
                self.assertTrue(wait_for(lambda: len(core.methods("session.hello")) >= 2
                                         and len(core.methods("data.publish")) > count))
                process.send_signal(signal.SIGTERM)
                _, err = process.communicate(timeout=10)
            finally:
                if process.poll() is None:
                    process.kill()
                supervisor.close()
                core.close()
            self.assertEqual(process.returncode, 0, err)
            self.assertEqual([h["params"].get("token") for h in core.methods("session.hello")], [first, second])
            self.assertNotIn(first, err)
            self.assertNotIn(second, err)

    def test_one_use_token_over_a_pipe_exits_when_the_connection_is_lost(self):
        # A session without renewal passes one token line and closes its end.
        token = "78" * 32
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            core = FakeCore(str(tmp / "control.sock"), token)
            config = tmp / "sources.json"
            config.write_text(json.dumps({"sources": [{"id": "tick", "kind": "timer", "timer": "pulse", "everyS": 1}]}))
            read, write = os.pipe()
            os.write(write, token.encode() + b"\n")
            os.close(write)
            process = subprocess.Popen([PY, str(HERE / "source-helper.py"), "--config", str(config),
                                        "--socket", str(tmp / "control.sock"), "--token-fd", str(read)],
                                       pass_fds=(read,), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            os.close(read)
            try:
                self.assertTrue(wait_for(lambda: self.published(core, "tick")))
                core.kick()
                _, err = process.communicate(timeout=15)
            finally:
                if process.poll() is None:
                    process.kill()
                core.close()
            self.assertEqual(process.returncode, helper.EXIT_NO_GRANT, err)
            self.assertIn("token channel is closed", err)
            self.assertEqual([h["params"].get("token") for h in core.methods("session.hello")], [token, None])
            self.assertNotIn(token, err)

    def test_reloads_the_config_on_change(self):
        token = "56" * 32
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            core = FakeCore(str(tmp / "control.sock"), token)
            config = tmp / "sources.json"
            pulse = {"kind": "timer", "timer": "pulse", "everyS": 1}
            write_atomic(config, {"sources": [{"id": "a", **pulse}, {"id": "b", **pulse}]})
            (tmp / "token").write_text(token)
            process = subprocess.Popen([PY, str(HERE / "source-helper.py"), "--config", str(config),
                                        "--socket", str(tmp / "control.sock"), "--token-file", str(tmp / "token")],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                self.assertTrue(wait_for(lambda: self.published(core, "a") and self.published(core, "b")))
                write_atomic(config, {"sources": [{"id": "b", **pulse, "key": "beats"}, {"id": "c", **pulse},
                                                  {"id": "a", **pulse, "enabled": False}]})
                self.assertTrue(wait_for(lambda: self.published(core, "c") and self.published(core, "b", "beats")
                                         and core.methods("data.remove")))
                removed = [r["params"]["sourceId"] for r in core.methods("data.remove")]
                self.assertEqual(removed, ["com.hyprune.sources.a"])
                count = len(self.published(core, "a"))
                write_atomic(config, {"sources": "not a list"})  # invalid: keeps running sources
                time.sleep(2.5)
                self.assertEqual(len(self.published(core, "a")), count)
                self.assertTrue(len(self.published(core, "c")) >= 2)
                process.send_signal(signal.SIGTERM)
                _, err = process.communicate(timeout=10)
            finally:
                if process.poll() is None:
                    process.kill()
                core.close()
            self.assertEqual(process.returncode, 0, err)
            self.assertIn("config not reloaded", err)
            self.assertIn("added c; changed b; removed a", err)
            removed = sorted(r["params"]["sourceId"] for r in core.methods("data.remove"))
            self.assertEqual(removed, [f"com.hyprune.sources.{n}" for n in "abc"])


def control(path, request, timeout=20):
    """One request on the helper's control socket; the reply (or None when it closed)."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(timeout)
        client.connect(str(path))
        client.sendall(request if isinstance(request, bytes) else json.dumps(request).encode() + b"\n")
        data = b""
        while not data.endswith(b"\n"):
            chunk = client.recv(65536)
            if not chunk:
                break
            data += chunk
    return json.loads(data) if data else None


SCRIPT_JSON = "import json, sys; print(json.dumps({'a': 1, 'b': 'x', 'obj': {}})); sys.stderr.write('note\\n')"


class HelperControl(unittest.TestCase):
    """The control channel (README "Control channel") against a fake core."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        self.process = self.core = None

    def tearDown(self):
        if self.process is not None and self.process.poll() is None:
            self.process.kill()
            self.process.communicate()
        if self.core is not None:
            self.core.close()
        self.tmp.cleanup()

    def start(self, config, token, *extra, write=True, **popen):
        self.config = self.dir / "sources.json"
        if write:
            write_atomic(self.config, config)
        self.core = FakeCore(str(self.dir / "control.sock"), token)
        if not any(a in extra for a in ("--token-fd",)):
            (self.dir / "token").write_text(token if isinstance(token, str) else "")
            extra = ("--token-file", str(self.dir / "token")) + extra
        self.process = subprocess.Popen([PY, str(HERE / "source-helper.py"), "--config", str(self.config),
                                         "--socket", str(self.dir / "control.sock"), "-v", *extra],
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **popen)
        return self.process

    def stop(self):
        self.process.send_signal(signal.SIGTERM)
        _, err = self.process.communicate(timeout=10)
        self.assertEqual(self.process.returncode, 0, err)
        return err

    def published(self, slug, key=None):
        return [r["params"]["values"] for r in self.core.methods("data.publish")
                if r["params"]["sourceId"] == f"com.hyprune.sources.{slug}"
                and (key is None or key in r["params"]["values"])]

    def test_status_shape_and_test_publishes_nothing(self):
        self.start({"sources": [
            {"id": "cmd", "kind": "command", "argv": [PY, "-c", SCRIPT_JSON], "intervalS": 5, "timeoutS": 4},
            {"id": "tick", "kind": "timer", "timer": "pulse", "everyS": 1},
            {"id": "off", "kind": "command", "argv": ["true"], "intervalS": 30, "enabled": False}]}, "a1" * 32)
        sock = self.dir / "sources.sock"
        self.assertTrue(wait_for(lambda: sock.is_socket() and self.published("cmd") and self.published("tick")))
        self.assertEqual(sock.stat().st_mode & 0o777, 0o600)
        before = int(time.time() * 1000)
        status = control(sock, {"op": "status"})
        self.assertEqual(set(status), {"ok", "config", "connected", "configError", "sources", "limits"})
        self.assertEqual((status["ok"], status["config"], status["connected"], status["configError"]),
                         (True, str(self.config), True, None))
        self.assertEqual(status["limits"]["command"], {"intervalS": [5, 86400], "devIntervalS": 1, "timeoutS": [1, 60]})
        self.assertFalse(status["limits"]["dev"])
        self.assertEqual(list(status["sources"]), ["cmd", "tick", "off"])
        cmd = status["sources"]["cmd"]
        self.assertEqual({k: cmd[k] for k in ("kind", "argv", "intervalS", "enabled", "resets")},
                         {"kind": "command", "argv": [PY, "-c", SCRIPT_JSON], "intervalS": 5, "enabled": True,
                          "resets": 0})
        self.assertIsInstance(cmd["running"], bool)
        run = cmd["lastRun"]
        self.assertEqual(set(run), {"at", "ok", "values", "error", "exitCode", "durationMs"})
        self.assertEqual((run["ok"], run["error"], run["exitCode"]), (True, None, 0))
        self.assertEqual(run["values"], {"a": 1, "b": "x", "_status": "ok", "_exitCode": 0})
        self.assertTrue(before - 10000 < run["at"] <= before and run["durationMs"] >= 0)
        self.assertGreater(cmd["nextRunAt"], before)
        self.assertEqual(status["sources"]["tick"]["kind"], "timer")
        self.assertEqual(status["sources"]["off"], {"kind": "command", "argv": ["true"], "intervalS": 30,
                                                     "enabled": False, "running": False, "lastRun": None,
                                                     "nextRunAt": None, "resets": 0})

        count = len(self.core.methods("data.publish"))
        reply = control(sock, {"op": "test", "source": {"kind": "command", "argv": [PY, "-c", SCRIPT_JSON]}})
        self.assertEqual(set(reply), {"ok", "values", "stdout", "stderr", "error", "exitCode", "durationMs"})
        self.assertEqual((reply["ok"], reply["values"], reply["error"], reply["exitCode"]),
                         (True, {"a": 1, "b": "x"}, None, 0))
        self.assertEqual((reply["stdout"], reply["stderr"]), ('{"a": 1, "b": "x", "obj": {}}\n', "note\n"))
        failing = control(sock, {"op": "test", "source": {"kind": "command", "output": "number", "argv": [
            PY, "-c", "import sys; print('x' * 5000); sys.stderr.write('y' * 5000 + 'END'); sys.exit(3)"]}})
        self.assertEqual((failing["ok"], failing["exitCode"], failing["values"]), (True, 3, {}))
        self.assertTrue(failing["error"].startswith("exit code 3"))
        self.assertEqual((len(failing["stdout"]), len(failing["stderr"])), (4096, 4096))
        self.assertTrue(failing["stderr"].endswith("END"))
        slow = control(sock, {"op": "test", "source": {"kind": "command", "timeoutS": 1, "argv": [
            PY, "-c", "import time; print('partial', flush=True); time.sleep(30)"]}})
        self.assertIn("timed out", slow["error"])
        self.assertEqual((slow["stdout"], slow["exitCode"]), ("partial\n", -9))
        self.assertLess(slow["durationMs"], 5000)
        number = control(sock, {"op": "test", "source": {"kind": "command", "output": "number", "key": "n",
                                                          "argv": [PY, "-c", "print(42)"]}})
        self.assertEqual(number["values"], {"n": 42})
        for bad in ({"kind": "command", "argv": "true"}, {"kind": "command", "argv": []},
                    {"kind": "command", "argv": ["true", 1]}, {"kind": "timer", "timer": "pulse"},
                    {"kind": "command", "argv": ["true"], "intervalS": 1}, "x"):
            reply = control(sock, {"op": "test", "source": bad})
            self.assertFalse(reply["ok"], bad)
            self.assertIsInstance(reply["error"], str)
        self.assertEqual([p["params"]["sourceId"] for p in self.core.methods("data.publish")[count:]
                          if p["params"]["sourceId"].endswith(".test")], [])
        for request, message in ((b"not json\n", "not JSON"), (b'{"op": "fly"}\n', "unknown op"),
                                 (b"[1]\n", "needs an op"), (b"x" * (64 * 1024 + 10), "64 KiB"),
                                 (b'{"op": "restart", "id": "nope"}\n', "no source"),
                                 (b'{"op": "restart", "id": "off"}\n', "disabled")):
            reply = control(sock, request)
            self.assertEqual(reply["ok"], False)
            self.assertIn(message, reply["error"])
        self.stop()
        self.assertFalse(sock.exists())  # removed on exit

    def test_restart_kills_reruns_and_resets_the_average(self):
        marker = self.dir / "runs"
        script = ("import json, os, sys, time\n"
                  f"path = {str(marker)!r}\n"
                  "open(path, 'a').write(f'{os.getpid()}\\n')\n"
                  "n = len(open(path).read().split())\n"
                  "if n == 1: time.sleep(30)\n"
                  "print(json.dumps({'run': n}))\n")
        self.start({"sources": [{"id": "slow", "kind": "command", "argv": [PY, "-c", script],
                                 "intervalS": 60, "timeoutS": 60}]}, "b2" * 32)
        sock = self.dir / "sources.sock"
        self.assertTrue(wait_for(lambda: marker.exists() and sock.is_socket()))
        self.assertTrue(control(sock, {"op": "status"})["sources"]["slow"]["running"])
        first = int(marker.read_text().split()[0])
        started = time.monotonic()
        self.assertEqual(control(sock, {"op": "restart", "id": "slow", "resetAverage": True}), {"ok": True})
        self.assertTrue(wait_for(lambda: self.published("slow"), 10))
        self.assertLess(time.monotonic() - started, 5)  # not the 30 s sleep, not the 60 s interval
        state = pathlib.Path(f"/proc/{first}/stat")
        self.assertTrue(not state.exists() or state.read_text().split()[2] == "Z")
        self.assertEqual(self.published("slow")[0], {"run": 2, "_status": "ok", "_exitCode": 0, "_reset": 1})
        status = control(sock, {"op": "status"})["sources"]["slow"]
        self.assertEqual((status["resets"], status["lastRun"]["values"]["run"]), (1, 2))
        count = len(self.published("slow"))
        self.assertEqual(control(sock, {"op": "restart", "id": "com.hyprune.sources.slow"}), {"ok": True})
        self.assertTrue(wait_for(lambda: len(self.published("slow")) > count, 10))
        self.assertEqual(self.published("slow")[-1], {"run": 3, "_status": "ok", "_exitCode": 0, "_reset": 1})
        self.assertEqual(control(sock, {"op": "restart", "id": "slow", "resetAverage": True}), {"ok": True})
        self.assertTrue(wait_for(lambda: self.published("slow")[-1].get("_reset") == 2, 10))
        values = self.published("slow")
        self.assertTrue(all(v["_status"] == "ok" and "_reset" in v for v in values), values)  # the kill never published
        self.assertEqual(control(sock, {"op": "restart", "id": "slow", "resetAverage": "yes"})["ok"], False)
        self.stop()

    def test_restart_all_reconnects_with_a_renewed_token(self):
        first, second = "c3" * 32, "d4" * 32
        supervisor, child = socket.socketpair()
        supervisor.sendall(first.encode() + b"\n")
        try:
            self.start({"sources": [{"id": "tick", "kind": "timer", "timer": "pulse", "everyS": 1}]}, first,
                       "--token-fd", str(child.fileno()), "--token-refresh", pass_fds=(child.fileno(),))
            child.close()
            sock = self.dir / "sources.sock"
            self.assertTrue(wait_for(lambda: self.published("tick") and sock.is_socket()))
            write_atomic(self.config, {"sources": [{"id": "tick", "kind": "timer", "timer": "pulse", "everyS": 1},
                                                   {"id": "new", "kind": "timer", "timer": "pulse", "everyS": 1}]})
            self.core.grant(second)
            supervisor.sendall(second.encode() + b"\n")  # answers the renew the helper is about to ask for
            self.assertEqual(control(sock, {"op": "restart-all"}), {"ok": True})
            supervisor.settimeout(10)
            self.assertEqual(supervisor.recv(16), b"renew\n")
            hello = lambda: len(self.core.methods("session.hello")) >= 2
            self.assertTrue(wait_for(lambda: hello() and self.published("new")))
            requests = self.core.requests
            removed = [i for i, r in enumerate(requests) if r["method"] == "data.remove"]
            second_hello = [i for i, r in enumerate(requests) if r["method"] == "session.hello"][1]
            self.assertEqual([requests[i]["params"]["sourceId"] for i in removed], ["com.hyprune.sources.tick"])
            self.assertLess(removed[0], second_hello)
            self.assertTrue(any(r["method"] == "data.publish" and r["params"]["sourceId"].endswith(".tick")
                                for r in requests[second_hello:]))
            self.assertTrue(control(self.dir / "sources.sock", {"op": "status"})["connected"])
            err = self.stop()
        finally:
            supervisor.close()
        self.assertEqual([h["params"].get("token") for h in self.core.methods("session.hello")], [first, second])
        self.assertNotIn(first, err)
        self.assertNotIn(second, err)

    def test_reload_applies_an_edit_immediately(self):
        def config(key):
            return {"sources": [{"id": "a", "kind": "timer", "timer": "pulse", "everyS": 1, "key": key}]}
        self.start(config("aaaa"), "e5" * 32)
        sock = self.dir / "sources.sock"
        self.assertTrue(wait_for(lambda: self.published("a", "aaaa") and sock.is_socket()))
        # Same inode, size and mtime: the once-a-second check cannot see this edit.
        info = self.config.stat()
        text = self.config.read_text()
        self.config.write_text(text.replace("aaaa", "bbbb"))
        os.utime(self.config, ns=(info.st_atime_ns, info.st_mtime_ns))
        time.sleep(1.5)
        self.assertEqual(self.published("a", "bbbb"), [])
        started = time.monotonic()
        self.assertEqual(control(sock, {"op": "reload"}), {"ok": True, "added": [], "changed": ["a"], "removed": []})
        self.assertTrue(wait_for(lambda: self.published("a", "bbbb"), 5))
        self.assertLess(time.monotonic() - started, 1.0)
        self.config.write_text("{")
        reply = control(sock, {"op": "reload"})
        self.assertFalse(reply["ok"])
        self.assertIsNotNone(control(sock, {"op": "status"})["configError"])
        count = len(self.published("a", "bbbb"))
        self.assertTrue(wait_for(lambda: len(self.published("a", "bbbb")) > count, 5))  # still running
        write_atomic(self.config, config("cccc"))
        self.assertEqual(control(sock, {"op": "reload"})["changed"], ["a"])
        self.assertIsNone(control(sock, {"op": "status"})["configError"])
        self.stop()

    def test_idles_without_a_config_and_starts_it_when_it_appears(self):
        sock = self.dir / "elsewhere.sock"
        self.start(None, "f6" * 32, "--control", str(sock), write=False)
        self.assertTrue(wait_for(lambda: sock.is_socket() and self.core.methods("session.hello")))
        self.assertFalse((self.dir / "sources.sock").exists())
        status = control(sock, {"op": "status"})
        self.assertEqual((status["sources"], status["connected"], status["configError"]), ({}, True, None))
        self.config.write_text("")  # empty: no sources, not an error
        time.sleep(1.5)
        self.assertIsNone(control(sock, {"op": "status"})["configError"])
        self.config.write_text("{not json")
        self.assertTrue(wait_for(lambda: control(sock, {"op": "status"})["configError"], 5))
        write_atomic(self.config, {"sources": [{"id": "late", "kind": "timer", "timer": "pulse", "everyS": 1}]})
        self.assertTrue(wait_for(lambda: self.published("late"), 5))
        status = control(sock, {"op": "status"})
        self.assertEqual((list(status["sources"]), status["configError"]), (["late"], None))
        self.assertEqual(len(self.core.methods("session.hello")), 1)  # the same connection
        self.stop()

    def test_no_control(self):
        self.start({"sources": [{"id": "tick", "kind": "timer", "timer": "pulse", "everyS": 1}]}, "a7" * 32,
                   "--no-control")
        self.assertTrue(wait_for(lambda: self.published("tick")))
        self.assertFalse((self.dir / "sources.sock").exists())
        self.stop()


class ControlSocket(unittest.TestCase):
    """The control socket's own rules, in process."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = pathlib.Path(self.tmp.name) / "sources.sock"
        self.runner = helper.Runner([], helper.DryRunPublisher(open(os.devnull, "w")), None)

    def tearDown(self):
        self.runner.publisher.stream.close()
        self.tmp.cleanup()

    def test_other_users_are_refused(self):
        server = helper.ControlServer(str(self.path), self.runner, "/x/sources.json", uid=os.getuid() + 1)
        self.assertTrue(server.open())
        try:
            self.assertEqual(control(self.path, {"op": "status"}), {"ok": False, "error": "permission denied"})
        finally:
            server.close()
        server = helper.ControlServer(str(self.path), self.runner, "/x/sources.json")
        self.assertTrue(server.open())
        try:
            self.assertTrue(control(self.path, {"op": "status"})["ok"])
        finally:
            server.close()

    def test_stale_socket_is_replaced_and_other_files_are_kept(self):
        stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        stale.bind(str(self.path))
        stale.close()  # left behind, nobody listening
        server = helper.ControlServer(str(self.path), self.runner, "/x/sources.json")
        self.assertTrue(server.open())
        try:
            self.assertEqual(self.path.stat().st_mode & 0o777, 0o600)
            other = helper.ControlServer(str(self.path), self.runner, "/x/sources.json")
            self.assertFalse(other.open())  # a live socket is never taken over
            self.assertTrue(control(self.path, {"op": "status"})["ok"])
        finally:
            server.close()
        self.assertFalse(self.path.exists())
        self.path.write_text("not a socket")
        self.assertFalse(helper.ControlServer(str(self.path), self.runner, "/x/sources.json").open())
        self.assertEqual(self.path.read_text(), "not a socket")


if __name__ == "__main__":
    unittest.main()
