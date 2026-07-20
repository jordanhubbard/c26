#!/usr/bin/env python3
"""Boot the desktop headless, drive a click, and screendump to PNG.

Usage: shot.py OUT.png "CLICK 250,14" ["CLICK ..."]...
Each extra arg is a serial line sent after boot (2s apart) before the dump.
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(ROOT, "build", "c26.elf")
SRC_DISK = os.path.join(ROOT, "build", "c26.img")


def hmp(sock, line):
    sock.sendall((line + "\n").encode())
    time.sleep(0.4)
    try:
        return sock.recv(65536).decode(errors="replace")
    except Exception:
        return ""


def main():
    out_png = os.path.abspath(sys.argv[1])
    lines = sys.argv[2:]
    tmp = tempfile.mkdtemp()
    disk = os.path.join(tmp, "disk.img")
    subprocess.run(["cp", SRC_DISK, disk], check=True)
    mon = os.path.join(tmp, "mon.sock")
    ser = os.path.join(tmp, "ser.sock")
    ppm = os.path.join(tmp, "shot.ppm")

    cmd = [
        "qemu-system-riscv64", "-M", "virt", "-global",
        "virtio-mmio.force-legacy=false", "-cpu", "rv64", "-m", "256M",
        "-display", "none",
        "-chardev", f"socket,id=ser0,path={ser},server=on,wait=off",
        "-serial", "chardev:ser0",
        "-monitor", f"unix:{mon},server,nowait",
        "-bios", "none", "-no-reboot", "-kernel", ELF,
        "-device", "virtio-gpu-device", "-device", "virtio-keyboard-device",
        "-device", "virtio-mouse-device",
        "-audiodev", "driver=none,id=audio0",
        "-device", "virtio-sound-device,audiodev=audio0",
        "-drive", f"if=none,format=raw,file={disk},id=c26disk",
        "-device", "virtio-blk-device,drive=c26disk",
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        time.sleep(2.0)
        s_ser = socket.socket(socket.AF_UNIX)
        for _ in range(50):
            try:
                s_ser.connect(ser)
                break
            except OSError:
                time.sleep(0.1)
        s_mon = socket.socket(socket.AF_UNIX)
        for _ in range(50):
            try:
                s_mon.connect(mon)
                break
            except OSError:
                time.sleep(0.1)
        time.sleep(6.0)  # let the desktop come up
        for ln in lines:
            s_ser.sendall((ln + "\n").encode())
            time.sleep(1.5)
        time.sleep(0.5)
        hmp(s_mon, f"screendump {ppm}")
        time.sleep(1.0)
        subprocess.run(["sips", "-s", "format", "png", ppm, "--out", out_png],
                       check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        print(out_png)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    main()
