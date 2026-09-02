# FX comparisons, which yield 1.0 or 0.0 (WEAVE-SPEC 5.2)
grid 4 8
A1 = 3.5
A2 = 7.25
A3 = 3.5
? A1<A2
? A1>A2
? A1=A3
? A1<>A3
? A1<=A3
? A2>=A1
? (A1<A2)+(A2>A1)
? IF(A1<A2,10,20)
? IF(A1>A2,10,20)
