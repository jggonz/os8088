"""Profile the window lz_decomp_x -> ld_run_body_x.expanded of one launch by
stepping the guest in fixed cycle quanta and bucketing flat_ip."""
import os, sys, collections
ROOT = os.getcwd(); sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty, os88ui, ksample
sysimg, apps, machine, folder, pkg = sys.argv[1:6]
start = sys.argv[6] if len(sys.argv) > 6 else "lz_decomp_x"
stop = sys.argv[7] if len(sys.argv) > 7 else "ld_run_body_x.expanded"
Q = 3000
with os88ui.boot(sysimg, apps=apps, machine=machine, quick=True) as ui:
    m = ui.m
    ui.open_drive("B")
    if folder != "-": ui.open(folder)
    km = ksample.KMap(m)
    win = ui.raise_window(ui.disk_window())
    idx, ty = ui.entry(pkg, win)
    row = ui.scroll_to(idx, win=win); x, y = ui.row_xy(win, row)
    m.bp_exec(m.sym(start))
    ui.mo.dblclick(x, y, settle=0) if False else None
    # dblclick with breakpoint armed would fail its proof: arm via trace hold
    m.breakpoints([])
    ui.mo.dblclick(x, y, settle=0)
    m.bp_exec(m.sym(start)); m.run()
    assert m.wait_stop(300) is not None, "never reached " + start
    c0 = m.status()["cycles"]
    m.bp_exec(m.sym(stop))
    buck = collections.Counter(); n = 0
    while True:
        st = m.advance(cycles=Q)
        st = m.status()
        a = st.get("flat_ip") or ((st["cs"] << 4) + st["ip"])
        buck[km.name(a)] += 1; n += 1
        if st.get("state") == "breakpoint" and a == m.sym(stop):
            break
        if n > 20000: break
    c1 = m.status()["cycles"]
    print("%s -> %s: %d cycles = %.1f ms, %d samples" % (start, stop, c1 - c0, (c1 - c0) / 4772.727, n))
    for k, v in buck.most_common(25):
        print("  %6.1f%%  %s" % (100.0 * v / n, k))
