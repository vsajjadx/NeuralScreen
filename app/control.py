"""A small TCP control endpoint for other programs on this machine.

shadPS4's launcher starts NeuralScreen attached to the game window and drives
it with its own in-game keys, which cannot be global hotkeys: RegisterHotKey
would take the digits away from every other program (see hotkeys.py). The
endpoint listens on 127.0.0.1 only, takes one command per line and answers
"ok" (or "err: ...") per line, so the caller can log instead of guessing.

Every command is the same word the tray and the keyboard put into
st.tray_commands, so the endpoint decides nothing by itself - the menu state,
the config writes and the alerts all behave exactly as they do for the keys.
Unknown words are answered, not raised: the caller may be built against a
different version.
"""
from __future__ import annotations

import queue
import socket
import socketserver
import threading

# Word -> the command the keyboard uses. "nr"/"fg" read better in a
# launcher's log than "toggle"/"framegen"; both spellings are taken.
COMMANDS = {
    "nr": "toggle",
    "toggle": "toggle",
    "fg": "framegen",
    "framegen": "framegen",
    "boost": "boost",
    "reload": "reload",
    "quit": "quit",
    "ping": None,
}


class ControlHandler(socketserver.StreamRequestHandler):

    def handle(self) -> None:
        try:
            for line in self.rfile:
                word = line.decode("utf-8", "replace").strip().lower()
                if word == "ping":
                    self.wfile.write(b"ok\n")
                    continue
                cmd = COMMANDS.get(word)
                if cmd is None:
                    self.wfile.write(f"err: unknown command {word!r}\n".encode())
                    continue
                self.server.commands.put(cmd)
                self.wfile.write(b"ok\n")
        except (ConnectionError, OSError):
            # A caller that hangs up mid-line is normal, not a traceback.
            pass


class ControlServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, port: int, commands: "queue.Queue[str]"):
        self.commands = commands
        super().__init__(("127.0.0.1", port), ControlHandler)


def start(port: int, commands: "queue.Queue[str]") -> ControlServer:
    server = ControlServer(port, commands)
    threading.Thread(target=server.serve_forever, name="control",
                     daemon=True).start()
    return server
