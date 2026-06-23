def guitar():
    N("d4 a4 g4 a4 fs4 a4 e4 a4", div=8, dur=1/16, vel=92, ch=1)
    again(1, guitar)

def bass():
    N("d2 . d2 . c2 . g1 .", div=8, dur=1/8, vel=110, ch=2)
    again(1, bass)

def drums():
    N("36 42 38 42 36 42 38 46", div=8, dur=1/16, vel=115, ch=10)
    again(1, drums)

guitar()
bass()
drums()
