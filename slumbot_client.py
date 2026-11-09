#!/usr/bin/env python3
"""Plays the trained bot against Slumbot's public research API (200BB, blinds 50/100).

usage: slumbot_client.py data/hu200bb.policy <hands> [results.csv]
Slumbot's action strings: k=check, c=call, f=fold, b<N>=bet to N on this street, /=next street.
"""
import csv
import json
import math
import os
import ssl
import subprocess
import sys
import time
import urllib.request

API = "https://slumbot.com/api"
SYSTEM_CA = "/etc/ssl/cert.pem"  # python.org builds on macOS ship without CA certificates
TLS = ssl.create_default_context(cafile=SYSTEM_CA if os.path.exists(SYSTEM_CA) else None)


def post(path, body):
    req = urllib.request.Request(f"{API}/{path}", json.dumps(body).encode(), {"Content-Type": "application/json"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(req, timeout=30, context=TLS) as resp:
                return json.loads(resp.read())
        except OSError as err:
            if attempt == 4:
                raise
            print("retrying after", err, file=sys.stderr)
            time.sleep(2 ** attempt)


class Engine:
    def __init__(self, policy):
        self.proc = subprocess.Popen(["bin/bench", "serve", policy], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def send(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        return self.proc.stdout.readline().strip()


class Replay:
    """Tracks how much of the Slumbot action string the engine has already seen."""

    def __init__(self, engine):
        self.engine = engine
        self.seen = 0
        self.street = 0
        self.prior = 0            # chips each player committed on earlier streets
        self.bet = [50, 100]      # chips committed on this street, by seat
        self.to_act = 0

    def tokens(self, action):
        i = 0
        while i < len(action):
            ch = action[i]
            if ch == "b":
                j = i + 1
                while j < len(action) and action[j].isdigit():
                    j += 1
                yield i, j, action[i:j]
                i = j
            else:
                yield i, i + 1, ch
                i += 1

    def apply(self, action):
        for start, end, tok in self.tokens(action):
            if start < self.seen:
                continue
            self.seen = end
            if tok == "/":
                self.prior += max(self.bet)
                self.bet = [0, 0]
                self.street += 1
                self.to_act = 1
                continue
            who = self.to_act
            if tok == "f":
                self.engine.send(f"act {who} f")
            elif tok in ("k", "c"):
                self.bet[who] = max(self.bet)
                self.engine.send(f"act {who} c")
            else:
                self.bet[who] = int(tok[1:])
                self.engine.send(f"act {who} r{self.prior + self.bet[who]}")
            self.to_act = 1 - who

    def to_slumbot(self, reply):
        if reply == "f":
            return "f"
        if reply == "c":
            return "c" if self.bet[0] != self.bet[1] else "k"
        total = int(reply[1:])
        return f"b{total - self.prior}"


def play_hand(engine, token):
    body = {"token": token} if token else {}
    r = post("new_hand", body)
    if "error_msg" in r:
        raise RuntimeError(r["error_msg"])
    token = r.get("token", token)
    # client_pos 0 means the client is the big blind and acts first after the flop.
    seat = 1 if r["client_pos"] == 0 else 0
    engine.send(f"new {seat} {r['hole_cards'][0]} {r['hole_cards'][1]}")
    replay = Replay(engine)
    while True:
        action = r["action"]
        if r.get("winnings") is not None:
            return token, r["winnings"], seat, action
        # Feed actions street by street, sending the new board once its "/" is reached.
        board = r.get("board", [])
        replay.apply(action)
        if board:
            engine.send("board " + " ".join(board))
        reply = engine.send("go")
        r = post("act", {"token": token, "incr": replay.to_slumbot(reply)})
        if "error_msg" in r:
            raise RuntimeError(f"{r['error_msg']} after action {action!r}, reply {reply}")


def main():
    policy, hands = sys.argv[1], int(sys.argv[2])
    out = sys.argv[3] if len(sys.argv) > 3 else "logs/slumbot.csv"
    engine = Engine(policy)
    token, total, sq = None, 0.0, 0.0
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["hand", "seat", "winnings_chips", "action"])
        for i in range(1, hands + 1):
            token, win, seat, action = play_hand(engine, token)
            total += win
            sq += win * win
            w.writerow([i, seat, win, action])
            f.flush()
            if i % 100 == 0 or i == hands:
                mean = total / i
                se = math.sqrt(max(sq / i - mean * mean, 0) / i)
                print(f"hands={i} result={mean * 10:+.0f} mbb/hand +/- {1.96 * se * 10:.0f} (95% CI)", flush=True)


if __name__ == "__main__":
    main()
