"""Chart's screenshots, for tools/guide/manuals/CHART_O88.md.

    python3 tools/guide/capture/run.py CHART_O88 --apps build/office360.img
"""
import os88ui


def capture(ui, shot):
    def pick(menu, item):
        for attempt in range(3):
            try:
                ui.menu_pick(menu, item)
                break
            except os88ui.UIError:
                if attempt == 2:
                    raise
                ui.m.key("Escape")
                ui.settle()
        ui.mo.to(632, 440)
        ui.settle()

    w = ui.path("B:/CHART.O88")
    ui.raise_window(w)
    pick("File", "Open...")
    ui.chooser_open("SALES.SLK")          # the chooser opens in MEDIA
    ui.mo.to(632, 440)
    ui.settle()
    shot("chart-window", window="Chart", pad=10)
    pick("Data", "Column C")
    pick("Gallery", "Line")
    shot("chart-line", window="Chart", pad=10)
    pick("Gallery", "Pie")
    ui.m.advance(frames=70 * 30)          # a pie takes a while to sweep
    ui.m.run()
    ui.mo.to(632, 440)
    shot("chart-pie", window="Chart", pad=10)
    pick("Data", "Automatic")
    pick("Gallery", "Combination")
    shot("chart-combination", window="Chart", pad=10)
