# The error value: division by zero, and how it PROPAGATES through every op
grid 3 8
A1 = 3.5
A2 = 0
A3 = =A1/A2
? A1/A2
? A1/A2+A1
? A1*(A1/A2)
? -(A1/A2)
? ABS(A1/A2)
? ROUND(A1/A2)
? A3
? A3+1
? SUM(A1:A3)
? IF(1,A1,A1/A2)
? (A1/A2)<A1
