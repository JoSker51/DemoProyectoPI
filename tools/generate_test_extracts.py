#!/usr/bin/env python3
"""
generate_test_extracts.py

Generador one-shot de PDFs de extractos sinteticos siguiendo el template
estandar del proyecto. Produce N PDFs variados en pdfs/examples/.

Este script NO es parte del runtime del proyecto. Es solo una herramienta
de desarrollo para producir datos de prueba.

Uso:
    python tools/generate_test_extracts.py [--count 10] [--out pdfs/examples]

Dependencias: reportlab (pip install reportlab)
"""
from __future__ import annotations

import argparse
import os
import random
from dataclasses import dataclass, field
from datetime import date, timedelta
from pathlib import Path

from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak
)
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import cm

# =============================================================================
# Catálogos para variar los datos
# =============================================================================
CLIENTES = [
    ("MARTHA STELLA GORDILLO PEREZ",      "52.123.456",   "AVENIDA CALLE 26 # 85 D 55",       "BOGOTA D.C"),
    ("CARLOS ANDRES RODRIGUEZ MARTINEZ",  "79.654.321",   "CRA 11 # 93-46 OFICINA 502",       "BOGOTA D.C"),
    ("DIANA PATRICIA LOPEZ HERNANDEZ",    "1.020.345.678","CALLE 100 # 8A-49 TORRE B",        "BOGOTA D.C"),
    ("JUAN MANUEL VARGAS SOTO",           "80.111.222",   "AVENIDA EL POBLADO # 5-200",       "MEDELLIN"),
    ("INVERSIONES GAMA Y ASOCIADOS S.A.S","901.555.444-1","CARRERA 7 # 71-21",                "BOGOTA D.C"),
    ("LUISA FERNANDA TORRES BENAVIDES",   "43.876.234",   "CALLE 10 NORTE # 6N-25",           "CALI"),
    ("FAMILIA BERNAL HOLDING S.A.",       "900.234.567-2","AVENIDA SANTANDER # 25-30",        "BUCARAMANGA"),
    ("PEDRO ANTONIO RAMIREZ GARCIA",      "19.345.678",   "CRA 53 # 70-67 APTO 1102",         "BARRANQUILLA"),
    ("CONSTRUCTORA ROBLEDO LTDA",         "830.111.999-5","CALLE 33 # 6B-24",                 "MEDELLIN"),
    ("ANA MARIA QUINTERO ESCOBAR",        "39.555.789",   "CALLE 90 # 11-40 OFICINA 305",     "BOGOTA D.C"),
]

ASESORES = [
    "MARTHA STELLA GORDILLO PEREZ",
    "RICARDO ANDRES PARRA NAVAS",
    "SANDRA MILENA PEREZ TORRES",
    "FELIPE GOMEZ MUÑOZ",
    "ANA SOFIA CARDENAS LEON",
]

EMISORES_CDT = ["BANCO BOGOTA", "BANCOLOMBIA", "DAVIVIENDA", "BBVA COLOMBIA", "AV VILLAS"]
NEMOS_PREFIX = ["CDT", "TES", "BONOS"]

NOMBRES_FONDO = [
    ("FIC ACCIVAL VISTA",      "830025594"),
    ("FIC RENTA LIQUIDA",      "830030172"),
    ("FIC ACCIONES PLUS",      "830041885"),
    ("FIC SUMAR",              "830055701"),
]

BANCOS_AHORROS = ["BANCO BOGOTA", "BANCOLOMBIA", "DAVIVIENDA", "BBVA"]


# =============================================================================
# Estructura interna
# =============================================================================
@dataclass
class CDT:
    nemo:   str
    f_emi:  date
    f_vto:  date
    f_cmp:  date
    t_facial: float       # %
    valor_nominal: float  # $
    t_neg: float          # %
    t_val: float          # %
    valor_mercado: float  # $

@dataclass
class Fondo:
    nombre: str
    codigo: str
    saldo_anterior: float
    adiciones: float
    retiros: float
    rendimientos: float
    rentabilidades: dict          # {"Mensual": 4.46, ...}

    @property
    def nuevo_saldo(self) -> float:
        return self.saldo_anterior + self.adiciones - self.retiros + self.rendimientos

@dataclass
class Saldo:
    cuenta: str
    banco:  str
    saldo:  float

@dataclass
class Extracto:
    cliente: str
    nit:     str
    direccion: str
    ciudad:    str
    asesor:    str
    f_inicio:  date
    f_fin:     date
    cdts:      list = field(default_factory=list)
    fondos:    list = field(default_factory=list)
    saldos:    list = field(default_factory=list)

    @property
    def total_rf(self):     return sum(c.valor_mercado for c in self.cdts)
    @property
    def total_fic(self):    return sum(f.nuevo_saldo for f in self.fondos)
    @property
    def total_efectivo(self): return sum(s.saldo for s in self.saldos)
    @property
    def total(self): return self.total_rf + self.total_fic + self.total_efectivo


# =============================================================================
# Generación de datos sintéticos
# =============================================================================
def _money(rng: random.Random, lo: float, hi: float) -> float:
    """Genera un valor monetario con cifras 'redondeadas-pero-no-redondas'."""
    base = rng.uniform(lo, hi)
    return round(base / 1000) * 1000

def _date_in(rng: random.Random, start: date, end: date) -> date:
    delta = (end - start).days
    return start + timedelta(days=rng.randint(0, max(1, delta)))

def gen_cdt(rng: random.Random, f_inicio: date) -> CDT:
    emisor = rng.choice(EMISORES_CDT)
    nemo   = f"{rng.choice(NEMOS_PREFIX)} {emisor.split()[0]}"
    f_cmp  = _date_in(rng, f_inicio - timedelta(days=200), f_inicio)
    f_emi  = f_cmp
    plazo_dias = rng.choice([90, 180, 270, 360, 540, 720])
    f_vto  = f_emi + timedelta(days=plazo_dias)
    t_fac  = round(rng.uniform(7.0, 11.5), 4)
    t_neg  = round(t_fac + rng.uniform(-0.5, 0.8), 2)
    t_val  = round(t_neg + rng.uniform(0.2, 1.5), 2)
    nom    = _money(rng, 100_000_000, 2_000_000_000)
    valor_mercado = round(nom * (1 + (t_val / 100) * (rng.randint(5, 90) / 365)), 2)
    return CDT(nemo, f_emi, f_vto, f_cmp, t_fac, nom, t_neg, t_val, valor_mercado)

def gen_fondo(rng: random.Random) -> Fondo:
    nombre, codigo = rng.choice(NOMBRES_FONDO)
    saldo_ant = _money(rng, 50_000_000, 3_000_000_000)
    adic      = _money(rng, 0, 200_000_000) if rng.random() < 0.4 else 0
    ret       = _money(rng, 0, 100_000_000) if rng.random() < 0.3 else 0
    rend      = round(saldo_ant * rng.uniform(0.001, 0.012), 2)
    rentas = {
        "Mensual":         round(rng.uniform(0.3, 1.2),  2),
        "Trimestral":      round(rng.uniform(1.0, 3.5),  2),
        "Semestral":       round(rng.uniform(2.5, 7.0),  2),
        "Año corrido":     round(rng.uniform(1.0, 5.0),  2),
        "Último año":      round(rng.uniform(5.0, 12.0), 2),
        "Últimos 2 años":  round(rng.uniform(6.0, 14.0), 2),
    }
    return Fondo(nombre, codigo, saldo_ant, adic, ret, rend, rentas)

def gen_saldo(rng: random.Random) -> Saldo:
    banco = rng.choice(BANCOS_AHORROS)
    cuenta = "".join(str(rng.randint(0, 9)) for _ in range(10))
    saldo  = _money(rng, 100_000, 50_000_000)
    return Saldo(cuenta, banco, saldo)

def gen_extracto(seed: int) -> Extracto:
    rng = random.Random(seed)
    cliente, nit, direccion, ciudad = rng.choice(CLIENTES)
    asesor = rng.choice(ASESORES)
    # Periodo: ultimo mes de algun año entre 2024 y 2026
    yr = rng.choice([2024, 2025, 2026])
    mo = rng.randint(1, 12)
    f_inicio = date(yr, mo, 1)
    if mo == 12:
        f_fin = date(yr, 12, 31)
    else:
        f_fin = date(yr, mo + 1, 1) - timedelta(days=1)

    ext = Extracto(cliente, nit, direccion, ciudad, asesor, f_inicio, f_fin)
    n_cdts   = rng.choice([2, 3, 4, 5, 6, 7])
    n_fondos = rng.choice([0, 1, 1, 2])
    n_saldos = rng.choice([0, 1, 1, 2])
    ext.cdts   = [gen_cdt(rng, f_inicio) for _ in range(n_cdts)]
    ext.fondos = [gen_fondo(rng) for _ in range(n_fondos)]
    ext.saldos = [gen_saldo(rng) for _ in range(n_saldos)]
    return ext


# =============================================================================
# Renderizado del PDF (template estandar v1)
# =============================================================================
def _fmt_money(v: float) -> str:
    s = f"{v:,.2f}"
    return "$ " + s.replace(",", "X").replace(".", ",").replace("X", ".")

def _fmt_pct(v: float) -> str:
    return f"{v:.2f}%".replace(".", ",")

def _fmt_date(d: date) -> str:
    return d.strftime("%d-%m-%Y")

def render_pdf(ext: Extracto, out_path: Path) -> None:
    styles = getSampleStyleSheet()
    title  = ParagraphStyle("title",  parent=styles["Heading1"], fontSize=16,
                            alignment=1, textColor=colors.HexColor("#0B3D91"),
                            spaceAfter=4)
    sub    = ParagraphStyle("sub",    parent=styles["Normal"],   fontSize=9,
                            alignment=1, textColor=colors.HexColor("#555555"),
                            spaceAfter=12)
    h2     = ParagraphStyle("h2",     parent=styles["Heading2"], fontSize=12,
                            textColor=colors.HexColor("#0B3D91"),
                            spaceBefore=10, spaceAfter=6)
    body   = ParagraphStyle("body",   parent=styles["Normal"],   fontSize=10)

    doc = SimpleDocTemplate(str(out_path), pagesize=letter,
                            leftMargin=2*cm, rightMargin=2*cm,
                            topMargin=1.5*cm, bottomMargin=1.5*cm)
    story = []

    # --- Header ---
    story.append(Paragraph("EXTRACTO DE INVERSIONES", title))
    story.append(Paragraph(
        f"Periodo: {_fmt_date(ext.f_inicio)} al {_fmt_date(ext.f_fin)}", sub))

    # --- Datos del cliente ---
    cli_data = [
        ["Cliente:",   ext.cliente,   "NIT/CC:",   ext.nit],
        ["Direccion:", ext.direccion, "Ciudad:",   ext.ciudad],
        ["Asesor:",    ext.asesor,    "",          ""],
    ]
    cli_tbl = Table(cli_data, colWidths=[2.3*cm, 7.5*cm, 2.0*cm, 5.0*cm])
    cli_tbl.setStyle(TableStyle([
        ("FONTSIZE",    (0, 0), (-1, -1), 9),
        ("FONTNAME",    (0, 0), (0, -1),  "Helvetica-Bold"),
        ("FONTNAME",    (2, 0), (2, -1),  "Helvetica-Bold"),
        ("BOTTOMPADDING",(0, 0),(-1,-1), 3),
        ("TOPPADDING",   (0, 0),(-1,-1), 3),
    ]))
    story.append(cli_tbl)

    # --- Resumen del portafolio ---
    story.append(Paragraph("RESUMEN DEL PORTAFOLIO", h2))
    total = max(ext.total, 1)
    res_data = [["Componente", "Valor", "Participacion"]]
    if ext.cdts:
        res_data.append(["Renta Fija",
                         _fmt_money(ext.total_rf),
                         _fmt_pct(ext.total_rf / total * 100)])
    if ext.fondos:
        res_data.append(["Fondos (FIC)",
                         _fmt_money(ext.total_fic),
                         _fmt_pct(ext.total_fic / total * 100)])
    if ext.saldos:
        res_data.append(["Saldos en Efectivo",
                         _fmt_money(ext.total_efectivo),
                         _fmt_pct(ext.total_efectivo / total * 100)])
    res_data.append(["TOTAL PORTAFOLIO", _fmt_money(ext.total), "100,00%"])
    res_tbl = Table(res_data, colWidths=[6*cm, 5*cm, 4*cm])
    res_tbl.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0),  colors.HexColor("#0B3D91")),
        ("TEXTCOLOR",  (0, 0), (-1, 0),  colors.white),
        ("FONTNAME",   (0, 0), (-1, 0),  "Helvetica-Bold"),
        ("FONTNAME",   (0, -1),(-1, -1), "Helvetica-Bold"),
        ("BACKGROUND", (0, -1),(-1, -1), colors.HexColor("#E8EEF7")),
        ("ALIGN",      (1, 0), (-1, -1), "RIGHT"),
        ("ALIGN",      (0, 0), (0, 0),   "LEFT"),
        ("GRID",       (0, 0), (-1, -1), 0.4, colors.grey),
        ("FONTSIZE",   (0, 0), (-1, -1), 9),
        ("BOTTOMPADDING",(0, 0),(-1, -1), 4),
        ("TOPPADDING",   (0, 0),(-1, -1), 4),
    ]))
    story.append(res_tbl)

    # --- Renta Fija detalle ---
    if ext.cdts:
        story.append(Paragraph("RENTA FIJA - DETALLE", h2))
        rf_head = ["Nemotecnico", "F.Emi", "F.Vto", "F.Cmp",
                   "T.Facial", "Periodicidad", "Valor Nominal",
                   "T.Negociacion", "T.Valoracion", "Valor Mercado"]
        rf_rows = [rf_head]
        for c in ext.cdts:
            rf_rows.append([
                c.nemo,
                _fmt_date(c.f_emi),
                _fmt_date(c.f_vto),
                _fmt_date(c.f_cmp),
                _fmt_pct(c.t_facial),
                "TV",
                _fmt_money(c.valor_nominal),
                _fmt_pct(c.t_neg),
                _fmt_pct(c.t_val),
                _fmt_money(c.valor_mercado),
            ])
        rf_tbl = Table(rf_rows, repeatRows=1)
        rf_tbl.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0),  colors.HexColor("#0B3D91")),
            ("TEXTCOLOR",  (0, 0), (-1, 0),  colors.white),
            ("FONTNAME",   (0, 0), (-1, 0),  "Helvetica-Bold"),
            ("FONTSIZE",   (0, 0), (-1, -1), 7),
            ("ALIGN",      (4, 1), (-1, -1), "RIGHT"),
            ("GRID",       (0, 0), (-1, -1), 0.3, colors.grey),
            ("ROWBACKGROUNDS", (0, 1), (-1, -1),
                [colors.white, colors.HexColor("#F5F7FA")]),
            ("BOTTOMPADDING",(0, 0),(-1, -1), 3),
            ("TOPPADDING",   (0, 0),(-1, -1), 3),
        ]))
        story.append(rf_tbl)

    # --- Fondos ---
    for f in ext.fondos:
        story.append(Paragraph(f"FONDO DE INVERSION: {f.nombre}", h2))
        f_data = [
            ["Codigo:",         f.codigo,
             "Saldo Anterior:", _fmt_money(f.saldo_anterior)],
            ["Adiciones:",      _fmt_money(f.adiciones),
             "Retiros:",        _fmt_money(f.retiros)],
            ["Rendimientos:",   _fmt_money(f.rendimientos),
             "Nuevo Saldo:",    _fmt_money(f.nuevo_saldo)],
        ]
        f_tbl = Table(f_data, colWidths=[3.2*cm, 5*cm, 3.2*cm, 5*cm])
        f_tbl.setStyle(TableStyle([
            ("FONTSIZE",  (0, 0), (-1, -1), 9),
            ("FONTNAME",  (0, 0), (0, -1), "Helvetica-Bold"),
            ("FONTNAME",  (2, 0), (2, -1), "Helvetica-Bold"),
            ("ALIGN",     (1, 0), (1, -1), "RIGHT"),
            ("ALIGN",     (3, 0), (3, -1), "RIGHT"),
            ("BOTTOMPADDING",(0, 0),(-1, -1), 3),
            ("TOPPADDING",   (0, 0),(-1, -1), 3),
        ]))
        story.append(f_tbl)
        story.append(Spacer(1, 4))
        rent_data = [["Periodo", "Rentabilidad (% E.A.)"]]
        for k, v in f.rentabilidades.items():
            rent_data.append([k, _fmt_pct(v)])
        rent_tbl = Table(rent_data, colWidths=[6*cm, 5*cm])
        rent_tbl.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0),  colors.HexColor("#E8EEF7")),
            ("FONTNAME",   (0, 0), (-1, 0),  "Helvetica-Bold"),
            ("FONTSIZE",   (0, 0), (-1, -1), 8),
            ("ALIGN",      (1, 0), (-1, -1), "RIGHT"),
            ("GRID",       (0, 0), (-1, -1), 0.3, colors.grey),
            ("BOTTOMPADDING",(0, 0),(-1, -1), 3),
            ("TOPPADDING",   (0, 0),(-1, -1), 3),
        ]))
        story.append(rent_tbl)

    # --- Saldos en efectivo ---
    if ext.saldos:
        story.append(Paragraph("SALDOS EN EFECTIVO", h2))
        s_rows = [["Cuenta", "Banco", "Saldo"]]
        for s in ext.saldos:
            s_rows.append([s.cuenta, s.banco, _fmt_money(s.saldo)])
        s_tbl = Table(s_rows, colWidths=[5*cm, 6*cm, 5*cm])
        s_tbl.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0),  colors.HexColor("#0B3D91")),
            ("TEXTCOLOR",  (0, 0), (-1, 0),  colors.white),
            ("FONTNAME",   (0, 0), (-1, 0),  "Helvetica-Bold"),
            ("FONTSIZE",   (0, 0), (-1, -1), 9),
            ("ALIGN",      (2, 0), (-1, -1), "RIGHT"),
            ("GRID",       (0, 0), (-1, -1), 0.3, colors.grey),
            ("BOTTOMPADDING",(0, 0),(-1, -1), 3),
            ("TOPPADDING",   (0, 0),(-1, -1), 3),
        ]))
        story.append(s_tbl)

    doc.build(story)


# =============================================================================
# Main
# =============================================================================
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=10)
    ap.add_argument("--out",   type=Path, default=Path("pdfs/examples"))
    ap.add_argument("--seed",  type=int, default=42)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    print(f"Generando {args.count} PDFs en {args.out}/ ...")
    for i in range(args.count):
        ext = gen_extracto(args.seed + i)
        name = f"extracto_{i+1:02d}_{ext.cliente.split()[0].lower()}.pdf"
        path = args.out / name
        render_pdf(ext, path)
        print(f"  [{i+1:02d}] {name}  "
              f"({len(ext.cdts)} CDT, {len(ext.fondos)} fondos, "
              f"{len(ext.saldos)} saldos, total={_fmt_money(ext.total)})")

    print(f"\nOK: {args.count} PDFs generados en {args.out}/")

if __name__ == "__main__":
    main()
