#!/usr/bin/env python3
"""
generate_image_variants.py

A partir de un PDF de un extracto, produce 8 variantes de imagen para
estresar el pipeline (preprocessor + OCR):

  base.png        - render directo del PDF (referencia)
  hi.jpg          - JPG 95% (sin perdida visible)
  lo.jpg          - JPG 50% (compresion fuerte tipo celular)
  rot90.jpg       - rotada 90° (foto de costado)
  rot180.jpg      - rotada 180° (foto al reves)
  blur.jpg        - desenfocada (Gaussian blur)
  perspective.jpg - skew de perspectiva (foto desde angulo)
  lowlight.jpg    - contraste reducido + brillo bajo

Salidas en pdfs/test_variants/<nombre>/.

Uso:
    python tools/generate_image_variants.py [pdf_path]
Default: pdfs/examples/extracto_05_carlos.pdf
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pypdfium2 as pdfium
from PIL import Image, ImageFilter, ImageEnhance


def render_pdf_to_png(pdf_path: Path, dpi: int = 300) -> Image.Image:
    pdf = pdfium.PdfDocument(str(pdf_path))
    page = pdf[0]
    bitmap = page.render(scale=dpi / 72)
    img = bitmap.to_pil().convert("RGB")
    pdf.close()
    return img


def add_perspective(img: Image.Image, intensity: float = 0.08) -> Image.Image:
    """Aplica un warp de perspectiva 'tipo foto desde el angulo'."""
    w, h = img.size
    dx = int(w * intensity)
    dy = int(h * intensity * 0.4)
    # Coeficientes calculados con metodo PIL: necesitamos los 8 coefs
    # de la transformacion proyectiva tal que:
    #   src=(0,0), (w,0), (w,h), (0,h)
    #   dst=(dx,dy), (w,0), (w-dx,h-dy), (0,h)
    src = [(0, 0), (w, 0), (w, h), (0, h)]
    dst = [(dx, dy), (w - dx, 0), (w, h - dy), (0, h)]

    # Resolver coeficientes (metodo de los 8 numeros)
    matrix = []
    for s, d in zip(src, dst):
        x, y = s
        u, v = d
        matrix.append([x, y, 1, 0, 0, 0, -u * x, -u * y])
        matrix.append([0, 0, 0, x, y, 1, -v * x, -v * y])
    A = np.matrix(matrix, dtype=float)
    B = np.array([d for s, d in zip(src, dst) for d in s]).flatten()  # placeholder
    # Construir B correctamente: u, v intercalados
    B = np.array([c for d in dst for c in d], dtype=float)
    coeffs = np.array(np.linalg.solve(A, B)).reshape(8)

    return img.transform((w, h), Image.PERSPECTIVE,
                          tuple(coeffs.tolist()),
                          resample=Image.BICUBIC,
                          fillcolor=(255, 255, 255))


def add_lowlight(img: Image.Image) -> Image.Image:
    # Bajar brillo y contraste
    img = ImageEnhance.Brightness(img).enhance(0.65)
    img = ImageEnhance.Contrast(img).enhance(0.55)
    # Anadir un poco de tinte amarillento (luz incandescente)
    arr = np.array(img).astype(np.int16)
    arr[..., 2] = np.clip(arr[..., 2] - 25, 0, 255)  # menos azul
    return Image.fromarray(arr.astype(np.uint8))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pdf", nargs="?",
                     default="pdfs/examples/extracto_05_carlos.pdf")
    ap.add_argument("--out", default="pdfs/test_variants")
    args = ap.parse_args()

    pdf_path = Path(args.pdf)
    if not pdf_path.exists():
        print(f"ERROR: no existe {pdf_path}", file=sys.stderr)
        sys.exit(1)

    name = pdf_path.stem
    out_dir = Path(args.out) / name
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Renderizando {pdf_path} a 300 DPI...")
    base = render_pdf_to_png(pdf_path, dpi=300)
    print(f"  {base.size}")

    # 1. base.png
    p = out_dir / f"{name}_base.png"
    base.save(p)
    print(f"  -> {p.name}")

    # 2. hi.jpg
    p = out_dir / f"{name}_hi.jpg"
    base.save(p, "JPEG", quality=95)
    print(f"  -> {p.name}")

    # 3. lo.jpg
    p = out_dir / f"{name}_lo.jpg"
    base.save(p, "JPEG", quality=50)
    print(f"  -> {p.name}")

    # 4. rot90.jpg
    p = out_dir / f"{name}_rot90.jpg"
    base.rotate(-90, expand=True).save(p, "JPEG", quality=85)
    print(f"  -> {p.name}")

    # 5. rot180.jpg
    p = out_dir / f"{name}_rot180.jpg"
    base.rotate(180, expand=True).save(p, "JPEG", quality=85)
    print(f"  -> {p.name}")

    # 6. blur.jpg
    p = out_dir / f"{name}_blur.jpg"
    base.filter(ImageFilter.GaussianBlur(radius=2.5)).save(p, "JPEG", quality=85)
    print(f"  -> {p.name}")

    # 7. perspective.jpg
    p = out_dir / f"{name}_perspective.jpg"
    add_perspective(base, intensity=0.08).save(p, "JPEG", quality=85)
    print(f"  -> {p.name}")

    # 8. lowlight.jpg
    p = out_dir / f"{name}_lowlight.jpg"
    add_lowlight(base).save(p, "JPEG", quality=85)
    print(f"  -> {p.name}")

    print(f"\n8 variantes en {out_dir}/")

if __name__ == "__main__":
    main()
