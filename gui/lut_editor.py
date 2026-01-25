#!/usr/bin/env python3
import sys
import bisect
from PyQt6.QtWidgets import (
    QApplication, QWidget, QPushButton, QVBoxLayout, QHBoxLayout, QFileDialog,
    QLabel, QSpinBox, QCheckBox
)
from PyQt6.QtGui import QPainter, QPen, QColor, QFont
from PyQt6.QtCore import Qt, QRectF, QPointF

class LUTEditor(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("HDR LUT Editor")
        self.setMinimumSize(800, 600)

        # Curve points: (intended_nits, actual_nits)
        self.points = [(0, 0), (1000, 1000)]
        self.selected_index = None

        # Config
        self.max_nits = 1000
        self.inverse_enabled = True

        # UI
        self.init_ui()
        self.dragging = False

    def init_ui(self):
        # Main layout
        main_layout = QVBoxLayout()
        self.setLayout(main_layout)

        # Top controls: Max nits + export
        top_layout = QHBoxLayout()
        main_layout.addLayout(top_layout)
        top_layout.addWidget(QLabel("Max nits:"))
        self.max_nits_spin = QSpinBox()
        self.max_nits_spin.setRange(100, 10000)
        self.max_nits_spin.setValue(self.max_nits)
        self.max_nits_spin.valueChanged.connect(self.update_max_nits)
        top_layout.addWidget(self.max_nits_spin)
        top_layout.addStretch()
        self.export_btn = QPushButton("Export LUT")
        self.export_btn.clicked.connect(self.export_lut)
        top_layout.addWidget(self.export_btn)

        # Canvas
        self.canvas = QWidget()
        self.canvas.setMinimumSize(600, 400)
        main_layout.addWidget(self.canvas)
        self.canvas.paintEvent = self.paintEvent
        self.canvas.mousePressEvent = self.mousePressEvent
        self.canvas.mouseReleaseEvent = self.mouseReleaseEvent
        self.canvas.mouseMoveEvent = self.mouseMoveEvent

        # Bottom buttons
        bottom_layout = QHBoxLayout()
        main_layout.addLayout(bottom_layout)
        self.add_btn = QPushButton("Add Point")
        self.add_btn.clicked.connect(self.add_point)
        bottom_layout.addWidget(self.add_btn)
        self.remove_btn = QPushButton("Remove Point")
        self.remove_btn.clicked.connect(self.remove_point)
        bottom_layout.addWidget(self.remove_btn)
        self.reset_btn = QPushButton("Reset Curve")
        self.reset_btn.clicked.connect(self.reset_curve)
        bottom_layout.addWidget(self.reset_btn)
        bottom_layout.addStretch()

        self.inverse_checkbox = QCheckBox("Inverse correction")
        self.inverse_checkbox.setChecked(True)
        self.inverse_checkbox.stateChanged.connect(self.on_inverse_toggled)
        bottom_layout.addWidget(self.inverse_checkbox)

        # Info label
        self.info_label = QLabel("Point: ")
        main_layout.addWidget(self.info_label)

    def update_max_nits(self, value):
        self.max_nits = value
        self.update()

    def on_inverse_toggled(self, state):
        self.inverse_enabled = (state == Qt.CheckState.Checked)

    def add_point(self):
        if len(self.points) < 20:
            x = self.max_nits // 2
            y = self.max_nits // 2
            self.points.append((x, y))
            self.points.sort()
            self.update()

    def remove_point(self):
        if self.selected_index is not None and self.selected_index not in (0, len(self.points)-1):
            self.points.pop(self.selected_index)
            self.selected_index = None
            self.update()

    def reset_curve(self):
        self.points = [(0, 0), (self.max_nits, self.max_nits)]
        self.selected_index = None
        self.update()

    def export_lut(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save LUT", "lut", "All files (*.*)")
        if path:
            with open(path, "w") as f:
                if self.inverse_enabled:
                    f.write("0 0\n")
                    for intended, measured in self.points[1:]:
                        scale = measured / intended
                        corrected = intended / scale
                        f.write(f"{intended} {corrected}\n")
                else:
                    for x, y in self.points:
                        f.write(f"{x} {y}\n")
                
                if self.points[-1][0] < 10000:
                    f.write("10000 10000\n")

    def paintEvent(self, event):
        painter = QPainter(self.canvas)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        w = self.canvas.width()
        h = self.canvas.height()
        margin = 50
        graph_w = w - 2*margin
        graph_h = h - 2*margin

        # Draw background
        painter.fillRect(self.canvas.rect(), QColor(30, 30, 30))

        # Axes
        pen = QPen(QColor(200, 200, 200))
        pen.setWidth(2)
        painter.setPen(pen)
        painter.drawLine(margin, h-margin, w-margin, h-margin)  # X-axis
        painter.drawLine(margin, margin, margin, h-margin)      # Y-axis

        # Axis ticks and numbers
        steps = 10
        font = QFont("Monospace", 8)
        painter.setFont(font)
        for i in range(steps+1):
            # X-axis
            x = margin + i*graph_w/steps
            y = h-margin
            painter.drawLine(int(x), y-5, int(x), y+5)
            val = int(i*self.max_nits/steps)
            painter.drawText(int(x)-10, y+20, str(val))
            # Y-axis
            y_val = margin + i*graph_h/steps
            painter.drawLine(margin-5, int(h-y_val), margin+5, int(h-y_val))
            painter.drawText(5, int(h-y_val+5), str(int(i*self.max_nits/steps)))

        # Draw lines
        pen = QPen(QColor(0, 255, 0))
        pen.setWidth(2)
        painter.setPen(pen)
        for i in range(len(self.points)-1):
            x1 = margin + self.points[i][0]/self.max_nits*graph_w
            y1 = h - margin - self.points[i][1]/self.max_nits*graph_h
            x2 = margin + self.points[i+1][0]/self.max_nits*graph_w
            y2 = h - margin - self.points[i+1][1]/self.max_nits*graph_h
            painter.drawLine(int(x1), int(y1), int(x2), int(y2))

        # Draw points
        for i, (xv, yv) in enumerate(self.points):
            x = margin + xv/self.max_nits*graph_w
            y = h - margin - yv/self.max_nits*graph_h
            if i == self.selected_index:
                painter.setBrush(QColor(255, 0, 0))
            else:
                painter.setBrush(QColor(0, 255, 0))
            painter.drawEllipse(QPointF(x, y), 6, 6)

    def mousePressEvent(self, event):
        pos = event.position()
        w = self.canvas.width()
        h = self.canvas.height()
        margin = 50
        graph_w = w - 2*margin
        graph_h = h - 2*margin
        clicked = False
        for i, (xv, yv) in enumerate(self.points):
            x = margin + xv/ self.max_nits * graph_w
            y = h - margin - yv/ self.max_nits * graph_h
            if (pos.x()-x)**2 + (pos.y()-y)**2 <= 8**2:
                self.selected_index = i
                self.dragging = True
                clicked = True
                self.update()
                break
        if not clicked:
            self.selected_index = None
            self.update()

    def mouseReleaseEvent(self, event):
        self.dragging = False

    def mouseMoveEvent(self, event):
        if self.dragging and self.selected_index is not None:
            pos = event.position()
            w = self.canvas.width()
            h = self.canvas.height()
            margin = 50
            graph_w = w - 2*margin
            graph_h = h - 2*margin

            x = min(max(pos.x(), margin), margin + graph_w)
            y = min(max(pos.y(), margin), margin + graph_h)

            intended = int((x - margin)/graph_w*self.max_nits)
            actual = int((h - margin - y)/graph_h*self.max_nits)

            # Keep first and last points fixed X
            if self.selected_index == 0:
                intended = 0
            elif self.selected_index == len(self.points)-1:
                intended = self.max_nits

            self.points[self.selected_index] = (intended, actual)
            self.info_label.setText(f"Point {self.selected_index}: intended={intended}, actual={actual}")
            self.update()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    editor = LUTEditor()
    editor.show()
    sys.exit(app.exec())
