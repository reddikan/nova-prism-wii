#!/usr/bin/env python3
"""Cuts the hand-drawn art out of its paper and bakes it into source/art_data.h.

    tools/art/rocket_photo.jpg  -> the player's ship   (a photo of a marker-and-pencil drawing)
    tools/art/logo_source.png   -> the title logo

Each picture is evened out (shadows on the paper removed), cut out from the paper (everything the outside paper
can't reach through the ink outlines stays, so the white body of the rocket and the letters stay solid),
shrunk, and stored as a 256-colour palette image with alpha. Only needed when changing the art:
    python3 tools/make_art.py
"""
import os
import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "art")


def flood_outside(passable):
    """True for passable pixels connected to the image border."""
    lab, _ = ndimage.label(passable)
    border = set(np.unique(np.concatenate([lab[0, :], lab[-1, :], lab[:, 0], lab[:, -1]]))) - {0}
    return np.isin(lab, list(border))


def finish(rgb, inside, size, pad=6):
    """crop to the art, soften the cut edge, fit into size (w, h) keeping aspect, return RGBA image"""
    ys, xs = np.nonzero(inside)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    rgb, inside = rgb[y0:y1, x0:x1], inside[y0:y1, x0:x1]
    a = ndimage.gaussian_filter(inside.astype(np.float32), 1.0)
    rgba = np.dstack([np.clip(rgb, 0, 255), a * 255.0]).astype(np.uint8)
    img = Image.fromarray(rgba, "RGBA")
    W, H = size
    k = min((W - 2 * pad) / img.width, (H - 2 * pad) / img.height)
    img = img.resize((max(1, round(img.width * k)), max(1, round(img.height * k))), Image.LANCZOS)
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    out.paste(img, ((W - img.width) // 2, (H - img.height) // 2))
    return out


def rocket():
    im = np.asarray(Image.open(os.path.join(ART, "rocket_photo.jpg")).convert("RGB")).astype(np.float32)
    h, w, _ = im.shape
    im = im[int(h * 0.30):, :]                                    # skip the desk / screen at the top of the photo
    # even out the lighting: estimate the paper colour everywhere (ignoring ink) and divide it out
    paper = np.stack([ndimage.uniform_filter(ndimage.maximum_filter(im[:, :, c], 41), 81) for c in range(3)], -1)
    norm = np.clip(im / np.maximum(paper, 1) * 255.0, 0, 255)
    lum = norm.mean(-1)
    sat = norm.max(-1) - norm.min(-1)
    ink = (lum < 120) & (sat < 120) | (lum < 90)
    ink = ndimage.binary_closing(ink, iterations=2)
    passable = ~ink
    outside = flood_outside(passable)
    inside = ~outside
    inside = ndimage.binary_opening(inside, iterations=3)
    lab, n = ndimage.label(inside)                               # keep only the rocket (largest blob)
    sizes = ndimage.sum(inside, lab, range(1, n + 1))
    inside = lab == (1 + int(np.argmax(sizes)))
    inside = ndimage.binary_fill_holes(inside)
    # tidy colours: unpainted paper inside -> clean white, ink -> deep navy
    body = (lum > 185) & (sat < 45) & inside
    norm[body] = norm[body] * 0.25 + np.array([255, 253, 248]) * 0.75
    mean = norm.mean(-1, keepdims=True)                          # the photo washed the crayon out a bit
    crayon = (sat > 40)[..., None] & inside[..., None]
    norm = np.where(crayon, mean + (norm - mean) * 1.6 - 18, norm)
    return finish(norm, inside, (128, 128), pad=2)


def logo():
    im = np.asarray(Image.open(os.path.join(ART, "logo_source.png")).convert("RGB")).astype(np.float32)
    im = im[:, 112:]                                             # drop the binder holes
    r, g, b = im[:, :, 0], im[:, :, 1], im[:, :, 2]
    lum, sat = im.mean(-1), im.max(-1) - im.min(-1)
    margin = (np.arange(im.shape[1])[None, :] >= 24) & (np.arange(im.shape[1])[None, :] <= 50) & \
             (r > 190) & (g > 95) & (r - g > 35) & (r - b > 35) & (g < 200)
    rule = (b > r + 12) & (lum > 140) & (b > 170)                # the page's blue ruled lines
    paperish = ((sat < 42) & (lum > 178)) | rule | margin
    outside = flood_outside(paperish)
    # paper trapped inside a swoosh is still paper: any enclosed paper pocket with ruled lines in it goes too
    lab, n = ndimage.label(paperish & ~outside)
    rows = rule.mean(1) > 0.25                                   # the actual ruled rows of the page
    on_rows = rule & rows[:, None]
    hits = ndimage.sum(on_rows, lab, range(1, n + 1))
    def long_run(mask):                                          # a real ruled line runs on, unbroken
        best = 0
        for row in mask:
            if not row.any():
                continue
            d = np.diff(np.concatenate([[0], row.astype(np.int8), [0]]))
            best = max(best, int((np.nonzero(d == -1)[0] - np.nonzero(d == 1)[0]).max()))
        return best
    keep = []
    for i in range(n):
        if hits[i] > 40:
            ys, xs = np.nonzero(lab == i + 1)
            sub = (lab[ys.min():ys.max() + 1, xs.min():xs.max() + 1] == i + 1) & on_rows[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
            if long_run(sub) > 90:
                keep.append(i + 1)
    trapped = np.isin(lab, keep)
    inside = ~(outside | ndimage.binary_dilation(trapped, iterations=1))
    inside = ndimage.binary_opening(inside, iterations=1)
    cream = np.median(im[(sat < 30) & (lum > 200)], axis=0)
    im[(rule | margin) & inside] = cream                         # no ruled lines baked into the artwork
    return finish(im, inside, (768, 272), pad=4)


def emit(f, name, img):
    q = img.quantize(colors=256, method=Image.Quantize.FASTOCTREE)
    pal = q.getpalette(rawmode="RGBA")[: 256 * 4]
    pal += [0] * (256 * 4 - len(pal))
    idx = q.tobytes()
    f.write("#define %s_W %d\n#define %s_H %d\n" % (name, img.width, name, img.height))
    f.write("static const unsigned char %s_PAL[1024] = {\n" % name)
    for i in range(0, 1024, 32):
        f.write("    " + ",".join(str(v) for v in pal[i:i + 32]) + ",\n")
    f.write("};\nstatic const unsigned char %s_IDX[%d] = {\n" % (name, len(idx)))
    for i in range(0, len(idx), 40):
        f.write("    " + ",".join(str(v) for v in idx[i:i + 40]) + ",\n")
    f.write("};\n")


if __name__ == "__main__":
    ship, lg = rocket(), logo()
    ship.save(os.path.join(ART, "ship_cutout.png"))
    lg.save(os.path.join(ART, "logo_cutout.png"))
    with open(os.path.join(HERE, "..", "source", "art_data.h"), "w") as f:
        f.write("/* Generated by tools/make_art.py from tools/art/ - hand-drawn art as palette images with alpha */\n")
        emit(f, "SHIP", ship)
        emit(f, "LOGO", lg)
    print("ok", ship.size, lg.size)
