#!/usr/bin/env python3
"""Render the original SVG into the committed desktop PNG sizes.

Ubuntu dependency: python3-pyqt5.qtsvg
Run with QT_QPA_PLATFORM=offscreen python3 tools/render-icons.py.
"""
from pathlib import Path

from PyQt5.QtCore import Qt
from PyQt5.QtGui import QImage, QPainter
from PyQt5.QtSvg import QSvgRenderer
from PyQt5.QtWidgets import QApplication


def main():
    application = QApplication([])
    root = Path(__file__).resolve().parent.parent
    renderer = QSvgRenderer(str(root / "assets/disklens.svg"))
    if not renderer.isValid():
        raise SystemExit("Invalid application SVG")
    for size in (16, 32, 48, 64, 128, 256, 512):
        output = root / "assets/icons" / str(size) / "disklens.png"
        output.parent.mkdir(parents=True, exist_ok=True)
        image = QImage(size, size, QImage.Format_ARGB32)
        image.fill(Qt.transparent)
        painter = QPainter(image)
        renderer.render(painter)
        painter.end()
        if not image.save(str(output)):
            raise SystemExit(f"Cannot save {output}")
    return application


if __name__ == "__main__":
    main()
