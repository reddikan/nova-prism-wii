#!/usr/bin/env python3
"""Cuts the hand-drawn art out of its paper and bakes it into source/art_data.h.

    tools/art/rocket_photo.jpg  -> the player's ship   (a photo of a marker-and-pencil drawing)
    tools/art/logo_source.png   -> the title logo
    tools/art/boss2_drawing.jpg -> the level 2 boss (a pencil drawing, coloured in with crayon here)

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


def crayon_strokes(h, w, width, seed, angle=-0.55, density=1.0):
    """a sheet of waxy diagonal crayon strokes (0..1), like a kid colouring in"""
    from PIL import ImageDraw
    rng = np.random.default_rng(seed)
    sheet = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(sheet)
    for _ in range(int(w * h / (width * 70) * density)):
        x, y = rng.uniform(-60, w + 60), rng.uniform(-60, h + 60)
        a = angle + rng.uniform(-0.12, 0.12)
        L = rng.uniform(width * 6, width * 18)
        d.line([(x, y), (x + np.cos(a) * L, y + np.sin(a) * L)], fill=int(rng.uniform(150, 255)), width=int(width))
    m = np.asarray(sheet.filter(ImageFilter.GaussianBlur(width * 0.15))).astype(np.float32) / 255.0
    tooth = ndimage.gaussian_filter(rng.random((h, w)).astype(np.float32), width * 0.4)
    tooth = (tooth - tooth.min()) / (tooth.max() - tooth.min() + 1e-6)
    return np.clip(m * (0.65 + 0.5 * tooth), 0, 1)


def boss2():
    """the level 2 boss: a pencil drawing of a big head, coloured in with crayon (pencil lines kept on top)"""
    im = np.asarray(Image.open(os.path.join(ART, "boss2_drawing.jpg")).convert("RGB")).astype(np.float32)
    H, W, _ = im.shape
    paper = np.stack([ndimage.uniform_filter(ndimage.maximum_filter(im[:, :, c], 31), 61) for c in range(3)], -1)
    norm = np.clip(im / np.maximum(paper, 1) * 255.0, 0, 255)
    lum = norm.mean(-1)
    ink = lum < 160
    lab, n = ndimage.label(ink)
    sz = ndimage.sum(ink, lab, range(1, n + 1))
    ink = np.isin(lab, [i + 1 for i in range(n) if sz[i] >= 40])
    walls = ndimage.binary_closing(ink, iterations=5) | ndimage.binary_dilation(ink, iterations=2)
    yy, xx = np.mgrid[0:H, 0:W]
    # hoop earrings (ellipses traced from the drawing): thick gold rings, the pencil circles stay drawn on top
    hoops = [(322.0, 1205.0, 100.0, 130.0), (1280.0, 1298.0, 142.0, 142.0)]
    gold = np.zeros((H, W), bool)
    hoop_zone = np.zeros((H, W), bool)
    for cx, cy, rx, ry in hoops:
        d = np.hypot((xx - cx) / rx, (yy - cy) / ry)
        gold |= np.abs(d - 1.0) * min(rx, ry) < 13
        hoop_zone |= np.abs(d - 1.0) * min(rx, ry) < 24
    # the head: seal the gaps in the sketchy outline (ignoring the hoops), then fill it in
    from scipy.spatial import ConvexHull
    from PIL import ImageDraw
    body = ink & ~hoop_zone
    lab, n = ndimage.label(ndimage.binary_dilation(body, iterations=3))
    sz = ndimage.sum(body, lab, range(1, n + 1))
    body &= np.isin(lab, [i + 1 for i in range(n) if sz[i] > 400])   # the head's real lines (no stray specks)
    ys, xs = np.nonzero(body[::4, ::4])
    pts = np.c_[xs * 4, ys * 4]
    hull = pts[ConvexHull(pts).vertices]
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).polygon([tuple(map(float, p)) for p in hull], fill=255)
    inside = np.asarray(mask) > 0
    gold &= ~(inside & ~ndimage.binary_dilation(ink, iterations=3))      # behind the face only where the pencil ring is
    # colour regions of the drawing
    regions, _ = ndimage.label(inside & ~walls)
    def region_at(x, y, max_frac=0.2):
        k = regions[y, x]
        if k == 0:
            return np.zeros((H, W), bool)
        m = regions == k
        return m if m.mean() < max_frac else np.zeros((H, W), bool)
    col = np.zeros((H, W, 3), np.float32)
    col[:] = (250, 190, 150)                                         # skin
    mouth = region_at(760, 1360, 0.08)
    col[mouth] = (205, 55, 70)
    tongue = mouth & (np.hypot((xx - 760) / 150.0, (yy - 1420) / 55.0) < 1.0)
    col[tongue] = (255, 120, 140)
    for x, y in ((612, 1040), (585, 990), (955, 1050), (985, 1010)):  # eye whites
        col[region_at(x, y, 0.01)] = (255, 255, 255)
    for x, y in ((540, 928), (900, 928)):                             # eyebrows
        col[region_at(x, y, 0.01)] = (110, 70, 45)
    blush = (np.hypot((xx - 470) / 70.0, (yy - 1165) / 40.0) < 1) | (np.hypot((xx - 1070) / 75.0, (yy - 1150) / 40.0) < 1)
    col[blush & inside] = col[blush & inside] * 0.35 + np.array([255, 110, 140]) * 0.65
    # crayon it in: strokes over a light wash, so paper peeks through like real colouring
    strokes = crayon_strokes(H, W, 11, 7)
    cover = 0.45 + 0.55 * strokes
    paper_c = np.array([250, 246, 236], np.float32)
    rgb = paper_c * (1 - cover[..., None]) + col * cover[..., None]
    gstrokes = crayon_strokes(H, W, 9, 11, angle=0.9)
    gcol = np.array([255, 200, 40], np.float32) * (0.75 + 0.25 * gstrokes[..., None]) + np.array([200, 130, 10]) * (0.25 - 0.25 * gstrokes[..., None])
    rgb = np.where(gold[..., None], gcol, rgb)
    # pencil lines on top, a bit darker so they read at game size
    pencil = np.clip((lum / 235.0), 0, 1) ** 1.6
    lines = ndimage.binary_dilation(ink, iterations=1)
    pencil = np.where(lines, pencil, np.clip(pencil * 0.35 + 0.65, 0, 1))   # real lines full strength, smudges faint
    rgb = rgb * pencil[..., None] + np.array([40, 40, 60]) * (1 - pencil[..., None]) * 0.0
    keep = inside | gold
    keep = ndimage.binary_opening(keep, iterations=2)
    return finish(rgb, keep, (256, 256), pad=3)


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
    ship, lg, b2 = rocket(), logo(), boss2()
    ship.save(os.path.join(ART, "ship_cutout.png"))
    lg.save(os.path.join(ART, "logo_cutout.png"))
    b2.save(os.path.join(ART, "boss2_cutout.png"))
    with open(os.path.join(HERE, "..", "source", "art_data.h"), "w") as f:
        f.write("/* Generated by tools/make_art.py from tools/art/ - hand-drawn art as palette images with alpha */\n")
        emit(f, "SHIP", ship)
        emit(f, "LOGO", lg)
        emit(f, "BOSS2", b2)
    print("ok", ship.size, lg.size)
