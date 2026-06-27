#!/usr/bin/env python3
"""
EduOS RAG Engine — Test Client
Connects to the engine socket and lets you talk to it from terminal.

Usage:
    python3 src/rag/client.py
    python3 src/rag/client.py --socket /tmp/eduos_rag.sock --session test_01
    python3 src/rag/client.py --age-mode explorer
"""

import socket
import json
import argparse
import sys

SOCKET_PATH = "/tmp/eduos_rag.sock"


def main():
    parser = argparse.ArgumentParser(description="EduOS RAG Test Client")
    parser.add_argument("--socket",     default=SOCKET_PATH)
    parser.add_argument("--session",    default="test_session_01")
    parser.add_argument("--age-mode",   default="launchpad", dest="age_mode",
                        choices=["playground", "explorer", "launchpad", "professional"])
    args = parser.parse_args()

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.connect(args.socket)
    except FileNotFoundError:
        print(f"❌ Engine not running. Start it with:")
        print(f"   python3 src/rag/engine.py")
        sys.exit(1)
    except ConnectionRefusedError:
        print(f"❌ Connection refused at {args.socket}")
        sys.exit(1)

    print(f"✅ Connected to EduOS RAG Engine")
    print(f"   session_id = {args.session}")
    print(f"   age_mode   = {args.age_mode}")
    print(f"   Type a topic or question to begin. Ctrl+C to quit.\n")

    buf = ""

    try:
        while True:
            try:
                text = input("You: ").strip()
            except EOFError:
                break

            if not text:
                continue

            msg = json.dumps({
                "session_id": args.session,
                "text":       text,
                "age_mode":   args.age_mode,
            }) + "\n"

            sock.sendall(msg.encode("utf-8"))

            # Read response (newline delimited)
            while "\n" not in buf:
                chunk = sock.recv(4096).decode("utf-8")
                if not chunk:
                    print("Engine disconnected.")
                    sys.exit(0)
                buf += chunk

            line, buf = buf.split("\n", 1)
            try:
                resp = json.loads(line)

                print(f"\n🤖 Tutor: {resp.get('text', '')}")
                print(
                    f"   [{resp.get('sequence_state', '?')}]"
                    + (f" step {resp.get('step_index', 0) + 1}" if resp.get('sequence_state') == 'STEPS' else "")
                    + (f" | question: {resp.get('question_id', 'none')}" if resp.get('question_id') else "")
                    + (f" | can_skip: {resp.get('can_skip', False)}")
                )
                if resp.get("error"):
                    print(f"   ⚠️  error: {resp['error']}")
                print()

            except json.JSONDecodeError:
                print(f"⚠️  Bad response: {line}")

    except KeyboardInterrupt:
        print("\nBye.")
    finally:
        sock.close()


if __name__ == "__main__":
    main()