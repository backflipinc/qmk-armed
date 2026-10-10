# Дисплей ST7789 320x172 через Quantum Painter
QUANTUM_PAINTER_ENABLE = yes
QUANTUM_PAINTER_DRIVERS += st7789_spi

# Матрица клавиш + RGB-подсветка: один движок в прерывании CT16B3 (как сток), колонки общие
SRC += ledscan.c

# Страховки: ранний выход по Esc, сторожевой таймер, QK_BOOT с выключением WDT
SRC += safety.c

# Свой слой связи QP <-> ST7789 на регистрах SPI0 (spi_master на этой плате не работает)
SRC += lcd_spi.c

# Беспровод: радиомодуль BT/2.4G на UART2 (docs/uart-protocol.md).
# Встроенный механизм QMK «свой Bluetooth-драйвер»: отчёты идут через bluetooth_send_*()
BLUETOOTH_ENABLE = yes
BLUETOOTH_DRIVER = custom
SRC += wireless.c

# Батарея: счётчик заряда CW2017 на программном I²C (P0.14/P0.15)
BATTERY_ENABLE = yes
BATTERY_DRIVER = custom
SRC += battery_cw2017.c

# Заряд по USB: HID Battery Strength. Требует патча ядра QMK: qmk/patches/hid-battery.patch
OPT_DEFS += -DHID_BATTERY_ENABLE

# Интерфейс дисплея (меню, HUD) + скорость печати на главном экране
SRC += ui.c
WPM_ENABLE = yes

# Двухцветные эффекты подсветки (rgb_matrix_kb.inc)
RGB_MATRIX_CUSTOM_KB = yes

# Светодиодная полоса под дисплеем (9 LED вне RGB Matrix)
SRC += strip.c

SRC += snaptap.c

SRC += settings.c

# Raw HID channel for vial-gui-armed (only built with VIA)
SRC += io84_hid.c
