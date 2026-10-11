#!/usr/bin/env python3
"""Put your own image, GIF or video on the screen of a qmk-armed keyboard (ARMED_F_SCREEN).

  screen.py info                       what the keyboard has
  screen.py upload FILE [FILE ...]     image, animated GIF / WebP / PNG, or video (needs ffmpeg)
  screen.py dump OUT.gif|OUT.bin       read the stored animation back (.bin = exact copy for upload --raw)
  screen.py mode hud|animation         what the main screen shows

Needs: pip install hid pillow numpy (or the 'hidapi' package instead of 'hid').
The keyboard must be connected over USB. See docs/armed.md in qmk-armed for the protocol.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image, ImageOps, ImageSequence

RAW_USAGE_PAGE, RAW_USAGE = 0xFF60, 0x61
REPORT = 32
ARMED = 0xAD
INFO, LIST, GET, SET, SAVE, S_INFO, S_BEGIN, S_DATA, S_END, S_READ = range(10)
V_SCREEN = 12
F_SCREEN = 1 << 5
HEADER = 256
DATA_PAYLOAD = REPORT - 4
READ_PAYLOAD = REPORT - 7
WINDOW = 8  # requests in flight


class ArmedError(Exception):
    pass


# --------------------------------------------------------------------------- transport

def _open_hid(path):
    import hid
    if hasattr(hid, "Device"):  # 'hid' (ctypes) package
        return hid.Device(path=path)
    d = hid.device()  # 'hidapi' (cython) package
    d.open_path(path)
    return d


def find_keyboards():
    import hid
    for d in hid.enumerate():
        if d["usage_page"] == RAW_USAGE_PAGE and d["usage"] == RAW_USAGE:
            yield d


class Keyboard:
    def __init__(self, info):
        self.info = info
        self.dev = _open_hid(info["path"])

    def close(self):
        self.dev.close()

    def write(self, data):
        self.dev.write(b"\x00" + bytes(data).ljust(REPORT, b"\x00"))

    def read(self, timeout_ms=5000):
        try:
            r = self.dev.read(REPORT, timeout_ms)
        except TypeError:
            r = self.dev.read(REPORT, timeout=timeout_ms)
        if not r:
            raise ArmedError("keyboard did not answer")
        return bytes(r)

    def request(self, cmd, payload=b"", timeout_ms=5000):
        self.write(bytes([ARMED, cmd]) + payload)
        r = self.read(timeout_ms)
        if r[0] == 0xFF:
            return None
        if r[0] != ARMED or r[1] != cmd:
            raise ArmedError(f"unexpected reply {r[:4].hex()}")
        return r

    def armed_info(self):
        r = self.request(INFO)
        if not r or r[2:6] != b"ARMD":
            raise ArmedError("not a qmk-armed keyboard")
        return {"protocol": r[6], "version": tuple(r[7:10]), "features": r[10] | r[11] << 8}

    def screen_info(self):
        if not self.armed_info()["features"] & F_SCREEN:
            raise ArmedError("this firmware has no screen animation support")
        r = self.request(S_INFO)
        if not r:
            raise ArmedError("SCREEN_INFO failed")
        w, h, fmt, max_frames, frames, capacity, header = struct.unpack_from("<HHBBBIH", r, 2)
        return {"width": w, "height": h, "format": fmt, "max_frames": max_frames, "frames": frames,
                "capacity": capacity, "header": header}

    def get(self, value):
        r = self.request(GET, bytes([value]))
        return None if r is None else r[3]

    def set(self, value, v):
        return self.request(SET, bytes([value, v])) is not None

    def read_blob(self, offset, size, progress=None):
        """Pipelined SCREEN_READ"""
        out = bytearray()
        reqs = [(o, min(READ_PAYLOAD, offset + size - o)) for o in range(offset, offset + size, READ_PAYLOAD)]
        sent = 0
        pending = []
        while len(out) < size:
            while sent < len(reqs) and len(pending) < WINDOW:
                o, n = reqs[sent]
                self.write(bytes([ARMED, S_READ]) + struct.pack("<IB", o, n))
                pending.append(reqs[sent])
                sent += 1
            o, n = pending.pop(0)
            r = self.read()
            if r[0] != ARMED or r[1] != S_READ or struct.unpack_from("<I", r, 2)[0] != o:
                raise ArmedError(f"read failed at {o}")
            out += r[7:7 + n]
            if progress:
                progress(len(out), size)
        return bytes(out)

    def upload(self, size, stream, progress=None):
        if not self.request(S_BEGIN, struct.pack("<I", size), timeout_ms=10000):
            raise ArmedError("the keyboard refused the upload (too big?)")
        chunks = [stream[i:i + DATA_PAYLOAD] for i in range(0, len(stream), DATA_PAYLOAD)]
        sent = 0
        pending = []
        try:
            for done in range(len(chunks)):
                while sent < len(chunks) and len(pending) < WINDOW:
                    c = chunks[sent]
                    self.write(bytes([ARMED, S_DATA, sent & 0xFF, len(c)]) + c)
                    pending.append(sent)
                    sent += 1
                seq = pending.pop(0)
                r = self.read(timeout_ms=10000)
                if r[1] != S_DATA or r[2] != seq & 0xFF or r[3] != 0:
                    raise ArmedError(f"upload failed at packet {seq}, byte {struct.unpack_from('<I', r, 4)[0]}")
                if progress:
                    progress(struct.unpack_from("<I", r, 4)[0], size)
        except BaseException:
            for _ in pending:  # drain the replies still in flight
                try:
                    self.read(timeout_ms=2000)
                except ArmedError:
                    break
            self.request(S_END, b"\x00")
            raise
        if not self.request(S_END, b"\x01", timeout_ms=10000):
            raise ArmedError("the keyboard rejected the finished upload")


def open_keyboard():
    kbs = list(find_keyboards())
    for info in kbs:
        kb = Keyboard(info)
        try:
            if kb.armed_info()["features"] & F_SCREEN:
                return kb
        except ArmedError:
            pass
        kb.close()
    sys.exit("No qmk-armed keyboard with screen support found (USB connected? permissions? see udev/)")


# --------------------------------------------------------------------------- frames

def fit(img, w, h, mode, bg):
    img = img.convert("RGBA")
    canvas = Image.new("RGBA", (w, h), bg)
    if mode == "stretch":
        img = img.resize((w, h), Image.LANCZOS)
    elif mode == "cover":
        img = ImageOps.fit(img, (w, h), Image.LANCZOS)
    else:
        img = ImageOps.contain(img, (w, h), Image.LANCZOS)
    canvas.alpha_composite(img, ((w - img.width) // 2, (h - img.height) // 2))
    return canvas.convert("RGB")


def load_image_frames(path, delay_ms):
    """[(PIL image, delay ms)] from an image file, animated or not"""
    img = Image.open(path)
    frames = []
    for f in ImageSequence.Iterator(img):
        frames.append((f.convert("RGBA"), f.info.get("duration", delay_ms) or delay_ms))
    return frames


def load_video_frames(path, fps, max_frames, w, h):
    if not shutil.which("ffmpeg"):
        sys.exit("ffmpeg is needed for video")
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["ffmpeg", "-loglevel", "error", "-i", path, "-vf", f"fps={fps}", "-frames:v", str(max_frames),
                        os.path.join(tmp, "%04d.png")], check=True)
        names = sorted(os.listdir(tmp))
        return [(Image.open(os.path.join(tmp, n)).convert("RGBA"), 1000 / fps) for n in names]


def load_frames(paths, args, w, h, max_frames):
    frames = []
    for p in paths:
        try:
            Image.open(p)
            frames += load_image_frames(p, args.delay)
        except Exception:
            frames += load_video_frames(p, args.fps, max_frames, w, h)
    if args.fps and len(paths) == 1 and len(frames) > 1 and not is_video(paths[0]):
        frames = resample(frames, args.fps)
    frames = stretch_delays(frames)
    if len(frames) > max_frames:
        print(f"{len(frames)} frames, the keyboard keeps {max_frames}: the rest is dropped", file=sys.stderr)
        frames = frames[:max_frames]
    return [(fit(img, w, h, args.fit, args.background), d) for img, d in frames]


def stretch_delays(frames):
    """ A frame stays up to 510 ms (one header byte, 2 ms units); longer ones are repeated, which costs
        flash but almost nothing to upload """
    if len(frames) < 2:
        return frames
    out = []
    for img, d in frames:
        n = max(1, -(-int(d) // 510))
        out += [(img, d / n)] * n
    return out


def is_video(path):
    try:
        Image.open(path)
        return False
    except Exception:
        return True


def resample(frames, fps):
    """Pick frames at a fixed rate (fewer frames for long GIFs)"""
    total = sum(d for _, d in frames)
    step = 1000 / fps
    out, t, i, acc = [], 0.0, 0, frames[0][1]
    while t < total:
        while acc <= t and i < len(frames) - 1:
            i += 1
            acc += frames[i][1]
        out.append((frames[i][0], step))
        t += step
    return out


def rgb565(img):
    a = np.asarray(img, dtype=np.uint16)
    v = ((a[..., 0] & 0xF8) << 8) | ((a[..., 1] & 0xFC) << 3) | (a[..., 2] >> 3)
    return v.reshape(-1).astype("<u2")


def build_blob(frames):
    header = bytearray(b"\xff" * HEADER)
    header[0] = len(frames)
    for i, (_, d) in enumerate(frames):
        header[1 + i] = max(0, min(255, round(d / 2)))  # 2 ms units, like the stock configurator
    pixels = [rgb565(img) for img, _ in frames]
    return bytes(header), pixels


# --------------------------------------------------------------------------- stream encoder

OP_LIT, OP_RUN, OP_PREV = 0, 1, 2
MAX_OP = 0xFFFF


def op(kind, n):
    return bytes([kind << 6 | n]) if n < 64 else bytes([kind << 6]) + struct.pack("<H", n)


def encode_words(words, prev):
    """words: uint16 array of one frame (or the header); prev: previous frame or None"""
    n = len(words)
    out = bytearray()
    raw = words.astype("<u2").tobytes()
    # where a value run ends: first index > i with a different value
    change = np.flatnonzero(words[1:] != words[:-1]) + 1
    run_end = lambda i: int(change[np.searchsorted(change, i, side="right")]) if np.searchsorted(change, i, side="right") < len(change) else n
    if prev is not None:
        diff = np.flatnonzero(words != prev)
        prev_end = lambda i: int(diff[np.searchsorted(diff, i)]) if np.searchsorted(diff, i) < len(diff) else n
        same2 = (words[:-1] == prev[:-1]) & (words[1:] == prev[1:])
    else:
        prev_end = lambda i: i
        same2 = np.zeros(max(n - 1, 0), bool)
    run3 = np.zeros(max(n - 2, 0), bool)
    if n > 2:
        run3 = (words[:-2] == words[1:-1]) & (words[1:-1] == words[2:])
    # positions where a literal should stop: a PREV of 2+ or a RUN of 3+ starts there
    cand = np.flatnonzero(np.concatenate([same2, [False]])[:n] | np.concatenate([run3, [False, False]])[:n])

    i = 0
    while i < n:
        pl = prev_end(i) - i
        rl = run_end(i) - i
        if pl >= 2 and pl >= rl:
            k = min(pl, MAX_OP)
            out += op(OP_PREV, k)
            i += k
        elif rl >= 3:
            k = min(rl, MAX_OP)
            out += op(OP_RUN, k) + raw[2 * i:2 * i + 2]
            i += k
        else:
            j = np.searchsorted(cand, i + 1)
            end = int(cand[j]) if j < len(cand) else n
            k = min(end - i, MAX_OP)
            out += op(OP_LIT, k) + raw[2 * i:2 * (i + k)]
            i += k
    return bytes(out)


def encode(header, pixels):
    out = bytearray(encode_words(np.frombuffer(header, "<u2"), None))
    for k, p in enumerate(pixels):
        out += encode_words(p, pixels[k - 1] if k else None)
    return bytes(out)


# --------------------------------------------------------------------------- commands

def bar(done, total, t0, label):
    pct = done * 100 // max(total, 1)
    rate = done / max(time.time() - t0, 1e-3) / 1024
    sys.stderr.write(f"\r{label} {pct:3d}%  {done // 1024} / {total // 1024} KiB  {rate:.0f} KiB/s ")
    sys.stderr.flush()


def cmd_info(kb, args):
    i = kb.screen_info()
    mode = kb.get(V_SCREEN)
    print(f"screen     {i['width']}x{i['height']}, RGB565")
    print(f"animation  {i['frames'] or 'none'}{' frames' if i['frames'] else ''} (up to {i['max_frames']})")
    print(f"shows      {['HUD', 'animation'][mode] if mode is not None and mode < 2 else mode}")


def cmd_mode(kb, args):
    if not kb.set(V_SCREEN, {"hud": 0, "animation": 1}[args.mode]):
        sys.exit("the keyboard refused")


def cmd_upload(kb, args):
    i = kb.screen_info()
    w, h, frame_bytes = i["width"], i["height"], i["width"] * i["height"] * 2
    if args.raw:
        blob = open(args.files[0], "rb").read()
        header = blob[:HEADER]
        pixels = [np.frombuffer(blob[HEADER + k * frame_bytes:HEADER + (k + 1) * frame_bytes], "<u2")
                  for k in range((len(blob) - HEADER) // frame_bytes)]
        pixels = pixels[:i["max_frames"]]  # a 152-frame stock animation loses its last frame
        header = bytes([len(pixels)]) + header[1:]
    else:
        frames = load_frames(args.files, args, w, h, min(i["max_frames"], args.max_frames))
        header, pixels = build_blob(frames)
        if args.preview:
            save_gif(args.preview, frames)
    size = HEADER + len(pixels) * frame_bytes
    stream = encode(header, pixels)
    print(f"{len(pixels)} frames, {size // 1024} KiB -> {len(stream) // 1024} KiB to send", file=sys.stderr)
    t0 = time.time()
    kb.upload(size, stream, lambda d, t: bar(d, t, t0, "upload"))
    print(f"\ndone in {time.time() - t0:.0f} s", file=sys.stderr)
    if not args.keep_mode:
        kb.set(V_SCREEN, 1)


def save_gif(path, frames):
    imgs = [img.convert("RGB") for img, _ in frames]
    imgs[0].save(path, save_all=True, append_images=imgs[1:], duration=[max(20, int(d)) for _, d in frames], loop=0)


def cmd_dump(kb, args):
    i = kb.screen_info()
    w, h, frame_bytes = i["width"], i["height"], i["width"] * i["height"] * 2
    header = kb.read_blob(0, HEADER)
    n = i["frames"]
    if not n:
        sys.exit("no animation stored")
    if args.frames:
        n = min(n, args.frames)
    print(f"{i['frames']} frames stored, reading {n}", file=sys.stderr)
    t0 = time.time()
    body = kb.read_blob(HEADER, n * frame_bytes, lambda d, t: bar(d, t, t0, "read"))
    print(file=sys.stderr)
    if args.out.endswith(".bin"):
        hdr = bytearray(header)
        hdr[0] = n
        open(args.out, "wb").write(bytes(hdr) + body)
        return
    frames = []
    for k in range(n):
        v = np.frombuffer(body[k * frame_bytes:(k + 1) * frame_bytes], "<u2").reshape(h, w).astype(np.uint32)
        rgb = np.stack([(v >> 11) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3], -1).astype(np.uint8)
        frames.append((Image.fromarray(rgb, "RGB"), header[1 + k] * 2))
    save_gif(args.out, frames)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    m = sub.add_parser("mode")
    m.add_argument("mode", choices=["hud", "animation"])
    u = sub.add_parser("upload")
    u.add_argument("files", nargs="+")
    u.add_argument("--fit", choices=["contain", "cover", "stretch"], default="contain",
                   help="contain: whole picture, bars around (default); cover: fill the screen, crop; stretch")
    u.add_argument("--background", default="#000000", help="colour of the bars and of transparent pixels")
    u.add_argument("--fps", type=float, default=0, help="frame rate for video (default 10) or to resample a GIF")
    u.add_argument("--delay", type=int, default=1000, help="ms per still image (slideshow of several files)")
    u.add_argument("--max-frames", type=int, default=255)
    u.add_argument("--preview", metavar="OUT.gif", help="also save what will be uploaded as a GIF")
    u.add_argument("--raw", action="store_true", help="upload a .bin made by dump as is")
    u.add_argument("--keep-mode", action="store_true", help="don't switch the main screen to the animation")
    d = sub.add_parser("dump")
    d.add_argument("out")
    d.add_argument("--frames", type=int, help="read only the first N frames")
    args = ap.parse_args()
    if args.cmd == "upload" and not args.fps and any(is_video(f) for f in args.files):
        args.fps = 10

    kb = open_keyboard()
    try:
        {"info": cmd_info, "mode": cmd_mode, "upload": cmd_upload, "dump": cmd_dump}[args.cmd](kb, args)
    except ArmedError as e:
        sys.exit(f"\nerror: {e}")
    finally:
        kb.close()


if __name__ == "__main__":
    main()
