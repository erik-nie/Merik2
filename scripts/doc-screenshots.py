#!/usr/bin/env python3
"""Maakt de screenshots van de webpagina voor de handleiding (docs/images/web-*.png).

Gebruik (macOS):
  1. pip install websocket-client      (eenmalig)
  2. Start een testkopie van Merik met eigen instellingen (poort 80, zoals een gewone start; een
     draaiend Merik op poort 8888 blijft ongemoeid):
       MERIK_SETTINGS_NAME=MerikDemo MERIK_DEV=1 Merik.app/Contents/MacOS/Merik
     met in ~/Library/Application Support/MerikDemo/MerikDemo.settings twee setlists
     ("Saturday night" en "Warm-up set") met de demo-MIDI's uit docs/demo. Zet daarin ook een SoundFont.
     Heb je Merik op poort 8899 gestart (MERIK_WEB_PORT=8899), zet dan dezelfde variabele voor dit script.
  3. python3 scripts/doc-screenshots.py

MERIK_DEV schakelt een paar ontwikkelopdrachten in: "seek:<seconden>" (springen in het nummer),
"snapshot:<bestand>" (het hoofdvenster als PNG) en "settings:<tabblad>:<bestand>" (een tabblad van Settings).
"""
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.request
import base64

import websocket

PORT = os.environ.get("MERIK_WEB_PORT", "80")
BASE = f"http://127.0.0.1:{PORT}"
KEY = "merik"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "images")
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
WIDTH, HEIGHT = 1280, 800


RAW = tempfile.mkdtemp(prefix="merik-raw-")

FRAME = """<!doctype html><html><body style="margin:0;background:transparent">
<div id="w" style="display:inline-block;margin:24px 34px 44px;border-radius:12px;overflow:hidden;
 background:#101114;box-shadow:0 0 0 1px rgba(255,255,255,.12),0 22px 50px rgba(0,0,0,.45),0 6px 14px rgba(0,0,0,.3)">
 <div style="height:36px;background:linear-gradient(#3a3a3d,#2c2c2f);position:relative;font:600 14px -apple-system,Helvetica,sans-serif;
  color:#c9c9cc;text-align:center;line-height:36px">
  <span style="position:absolute;left:14px;top:12px;width:12px;height:12px;border-radius:6px;background:#ff5f57"></span>
  <span style="position:absolute;left:34px;top:12px;width:12px;height:12px;border-radius:6px;background:#febc2e"></span>
  <span style="position:absolute;left:54px;top:12px;width:12px;height:12px;border-radius:6px;background:#28c840"></span>
  TITLE</div>
 <img src="data:image/png;base64,DATA" style="display:block"></div></body></html>"""


def frame(page, raw, final, title):
    """Zet een venster-snapshot in een macOS-venster (titelbalk met stoplichten, schaduw)."""
    with open(raw, "rb") as f:
        data = base64.b64encode(f.read()).decode()
    html = FRAME.replace("TITLE", title).replace("DATA", data)
    page.call("Emulation.setDefaultBackgroundColorOverride", color={"r": 0, "g": 0, "b": 0, "a": 0})
    page.call("Page.navigate", url="data:text/html;base64," + base64.b64encode(html.encode()).decode())
    time.sleep(0.8)
    box = page.call("Runtime.evaluate", returnByValue=True, expression=
                    "(function(){var r=document.getElementById('w').getBoundingClientRect();"
                    "return [r.width+68,r.height+68]})()")["result"]["value"]
    page.call("Emulation.setDeviceMetricsOverride", width=int(box[0]), height=int(box[1]), deviceScaleFactor=2, mobile=False)
    data = page.call("Page.captureScreenshot", format="png",
                     clip={"x": 0, "y": 0, "width": box[0], "height": box[1], "scale": 1})["data"]
    with open(final, "wb") as f:
        f.write(base64.b64decode(data))
    page.call("Emulation.setDeviceMetricsOverride", width=WIDTH, height=HEIGHT, deviceScaleFactor=1, mobile=False)
    page.call("Emulation.setDefaultBackgroundColorOverride")
    print("saved", final)


def control(command):
    urllib.request.urlopen(
        urllib.request.Request(f"{BASE}/api/control?cmd={command}&key={KEY}", data=b"", method="POST"), timeout=5).read()


class Page:
    def __init__(self, port=9333):
        self.profile = tempfile.mkdtemp(prefix="merik-shot-")
        self.chrome = subprocess.Popen([
            CHROME, "--headless=new", f"--remote-debugging-port={port}", f"--user-data-dir={self.profile}",
            "--remote-allow-origins=*", "--hide-scrollbars", "--no-first-run", "--disable-gpu", f"--window-size={WIDTH},{HEIGHT}", "about:blank",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        targets = None
        for _ in range(50):
            try:
                targets = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=1))
                if any(t.get("type") == "page" for t in targets):
                    break
            except Exception:
                time.sleep(0.2)
        page = next(t for t in targets if t.get("type") == "page")
        self.ws = websocket.create_connection(page["webSocketDebuggerUrl"], timeout=20)
        self.next_id = 0
        self.call("Emulation.setDeviceMetricsOverride",
                  width=WIDTH, height=HEIGHT, deviceScaleFactor=1, mobile=False)

    def call(self, method, **params):
        self.next_id += 1
        self.ws.send(json.dumps({"id": self.next_id, "method": method, "params": params}))
        while True:
            message = json.loads(self.ws.recv())
            if message.get("id") == self.next_id:
                return message.get("result", {})

    def open(self, url, wait=2.5):
        self.call("Page.navigate", url=url)
        time.sleep(wait)

    def run(self, script, wait=0.8):
        self.call("Runtime.evaluate", expression=script)
        time.sleep(wait)

    def shot(self, name):
        data = self.call("Page.captureScreenshot", format="png")["data"]
        path = os.path.normpath(os.path.join(OUT, name))
        with open(path, "wb") as f:
            f.write(base64.b64decode(data))
        print("saved", path)

    def close(self):
        self.ws.close()
        self.chrome.terminate()


def main():
    page = Page()
    try:
        # --- Het hoofdvenster van de app (zonder vensterrand), via de ontwikkelopdracht "snapshot:" ---
        control("jump:0:0")
        time.sleep(1.5)
        control("seek:22")
        time.sleep(1.0)
        control("snapshot:" + os.path.join(RAW, "main-window.png"))
        time.sleep(0.5)
        frame(page, os.path.join(RAW, "main-window.png"), os.path.normpath(os.path.join(OUT, "main-window.png")), "Merik")

        # De tabbladen van Settings (General, Soundfont, Audio, Midi, Tools).
        for index, name in enumerate(["general", "soundfont", "audio", "midi", "tools"]):
            raw = os.path.join(RAW, "settings-%s.png" % name)
            control("settings:%d:%s" % (index, raw))
            time.sleep(0.8)
            frame(page, raw, os.path.normpath(os.path.join(OUT, "settings-%s.png" % name)), "Settings")

        # --- Basis: alleen kijken (zonder sleutel) ---
        control("jump:0:0")           # aurora
        time.sleep(1.5)
        control("seek:14.2")
        page.open(f"{BASE}/")
        page.shot("web-verse.png")

        control("seek:27.5")
        time.sleep(1.5)
        page.shot("web-chorus.png")

        control("jump:0:1")           # marble: instrumentaal stuk, dus de wacht-pill
        time.sleep(1.5)
        control("seek:58")
        page.open(f"{BASE}/")
        page.shot("web-countdown.png")

        control("jump:0:3")           # glass (geen tekst)
        time.sleep(1.5)
        control("seek:30")
        page.open(f"{BASE}/")
        page.shot("web-nolyrics.png")

        # --- Geavanceerd: met sleutel ---
        control("jump:0:0")
        time.sleep(1.5)
        control("seek:27.5")
        page.open(f"{BASE}/?key={KEY}")
        page.shot("web-advanced.png")

        page.run("document.getElementById('btnSearch').click()", wait=1.0)
        page.run("(function(){var i=document.getElementById('searchInput');i.value='mar';"
                 "i.dispatchEvent(new Event('input',{bubbles:true}));})()", wait=1.5)
        page.shot("web-search.png")
        page.run("document.getElementById('searchClose').click()", wait=0.5)

        page.run("document.getElementById('btnSetlist').click()", wait=1.2)
        page.shot("web-setlist.png")
    finally:
        page.close()


if __name__ == "__main__":
    sys.exit(main())
