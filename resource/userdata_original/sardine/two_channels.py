def bass():
    N("36 . 36 43", div=4, dur=1/8, vel=115, ch=1)
    again(1, bass)

def lead():
    N("60 64 67 72 67 64", div=8, dur=1/16, vel=90, ch=2)
    again(1, lead)

bass()
lead()
