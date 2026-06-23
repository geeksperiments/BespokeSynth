def chords():
    N(["c3", "e3", "g3", "b3"], dur=1/4)
    N(["f3", "a3", "c4", "e4"], delay=1/2, dur=1/4)
    again(1, chords)

chords()
