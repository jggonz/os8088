"""WireFleet's native deployment, captured through its own input."""
def capture(ui, shot):
    ui.path('B:/WIREFLET.O88')
    ui.m.key('KeyA')
    ui.settle()
    ui.m.key('KeyY')
    ui.settle()
    shot('wirefleet-ready', window='WireFleet', pad=4)
