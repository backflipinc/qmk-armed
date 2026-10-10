#!/usr/bin/env python3
# Generate keymaps/vial/vial.json from keyboard.json: python3 tools/vial_json.py keyboard.json vial.json
import json, sys
kb = json.load(open(sys.argv[1]))
keys = kb['layouts']['LAYOUT']['layout']
rows = {}
for k in keys:
    rows.setdefault(k['y'], []).append(k)
kle, prev_y = [], -1.0
for y in sorted(rows):
    row, x = [], 0.0
    first = True
    for k in sorted(rows[y], key=lambda k: k['x']):
        props = {}
        if first and y - prev_y - 1 > 1e-6:
            props['y'] = round(y - prev_y - 1, 2)
        if abs(k['x'] - x) > 1e-6:
            props['x'] = round(k['x'] - x, 2)
        if k.get('w', 1) != 1:
            props['w'] = k['w']
        if props:
            row.append(props)
        r, c = k['matrix']
        row.append(f"{r},{c}")
        x = k['x'] + k.get('w', 1)
        first = False
    if y == 0:  # knobs: encoder 0 (Esc spot) and 1 (PrtSc spot)
        row += [{"x": 0.5}, "0,0\n\n\n\n\n\n\n\n\ne", "0,1\n\n\n\n\n\n\n\n\ne",
                {"x": 0.25}, "1,0\n\n\n\n\n\n\n\n\ne", "1,1\n\n\n\n\n\n\n\n\ne"]
    kle.append(row)
    prev_y = y
custom = [
    ("BT_1", "Bluetooth slot 1 (hold 3 s: pair)"), ("BT_2", "Bluetooth slot 2 (hold 3 s: pair)"),
    ("BT_3", "Bluetooth slot 3 (hold 3 s: pair)"), ("BT_4", "Bluetooth slot 4 (hold 3 s: pair)"),
    ("BT_5", "Bluetooth slot 5 (hold 3 s: pair)"), ("RF_PAIR", "2.4 GHz: hold 3 s to pair"),
    ("SLEEP_T", "Auto-sleep timer"), ("UI_UP", "Menu up / brightness +"), ("UI_DOWN", "Menu down / brightness -"),
    ("UI_LEFT", "Menu left / previous effect"), ("UI_RIGHT", "Menu right / next effect"),
    ("UI_OK", "Open menu / select"), ("UI_BACK", "Menu back"),
]
out = {
    "name": kb['keyboard_name'],
    "vendorId": kb['usb']['vid'], "productId": kb['usb']['pid'],
    "matrix": {"rows": kb['matrix_size']['rows'], "cols": kb['matrix_size']['cols']},
    "lighting": "vialrgb",
    "customKeycodes": [{"name": n, "title": t, "shortName": n} for n, t in custom],
    "layouts": {"keymap": kle},
}
json.dump(out, open(sys.argv[2], 'w'), indent=2)
