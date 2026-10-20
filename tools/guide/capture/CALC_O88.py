"""Calculator's screenshots, for tools/guide/manuals/CALC_O88.md."""


def capture(ui, shot):
    w = ui.path("B:/APPS/CALC.O88")
    ui.m.type_text("12*3=")
    ui.settle()
    shot("calc-window", window="Calculator", pad=10)
    ui.m.type_text("+4=")
    ui.m.type_text("h")
    ui.settle()
    shot("calc-history", window="Calculator", pad=10)
