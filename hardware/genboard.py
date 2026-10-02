# Generates the ESP32-DevKitC V4 (38-pin) board block of the wiring SVG.
# USB at the bottom, matching the canonical Espressif pinout orientation.
import sys

Y0, PITCH = 98, 24
HOLE_L, HOLE_R = 416, 544
LAB_L, LAB_R = 426, 534

C = {  # silkscreen colours, keyed by role
    "off":  "#8A929A",
    "flash":"#9C6060",
    "spi":  "#E0A155",
    "i2s":  "#A394F2",
    "i2c":  "#48C2D2",
    "gpio": "#F0819C",
    "gnd":  "#C8CFD6",
    "pwr":  "#FF8A7E",
}

LEFT = [("3V3","pwr"),("EN","off"),("VP","off"),("VN","off"),("D34","off"),
        ("D35","off"),("D32","gpio"),("D33","gpio"),("D25","i2s"),("D26","i2s"),
        ("D27","i2s"),("D14","gpio"),("D12","off"),("GND","gnd"),("D13","off"),
        ("SD2","flash"),("SD3","flash"),("CMD","flash"),("5V","pwr")]

RIGHT = [("GND","off"),("D23","spi"),("D22","i2c"),("TX0","off"),("RX0","off"),
         ("D21","i2c"),("GND","gnd"),("D19","spi"),("D18","spi"),("D5","spi"),
         ("D17","off"),("D16","i2c"),("D4","i2c"),("D0","off"),("D2","off"),
         ("D15","off"),("SD1","flash"),("SD0","flash"),("CLK","flash")]

def y(i): return Y0 + PITCH * i

def holes(cx, n):
    out = []
    for i in range(n):
        out.append(f'        <circle cx="{cx}" cy="{y(i)}" r="5" fill="#D8AB4A"/>'
                   f'<circle cx="{cx}" cy="{y(i)}" r="2.2" fill="#1A1D22"/>')
    return "\n".join(out)

def labels(x, anchor, pins):
    out = []
    for i, (name, role) in enumerate(pins):
        used = role not in ("off", "flash")
        cls = "silk-on" if used else "silk"
        out.append(f'        <text class="{cls}" x="{x}" y="{y(i)+3}" '
                   f'text-anchor="{anchor}" fill="{C[role]}">{name}</text>')
    return "\n".join(out)

print("      <!-- pastilles du header gauche -->")
print("      <g>"); print(holes(HOLE_L, len(LEFT))); print("      </g>")
print("      <!-- serigraphie gauche -->")
print("      <g>"); print(labels(LAB_L, "start", LEFT)); print("      </g>")
print("      <!-- pastilles du header droit -->")
print("      <g>"); print(holes(HOLE_R, len(RIGHT))); print("      </g>")
print("      <!-- serigraphie droite -->")
print("      <g>"); print(labels(LAB_R, "end", RIGHT)); print("      </g>")

print("\n\n=== REPERES (stderr) ===", file=sys.stderr)
for side, pins, hole in (("G", LEFT, HOLE_L), ("D", RIGHT, HOLE_R)):
    for i, (n, r) in enumerate(pins):
        if r not in ("off", "flash"):
            print(f"{side} {n:4s} {r:5s} x={hole} y={y(i)}", file=sys.stderr)
