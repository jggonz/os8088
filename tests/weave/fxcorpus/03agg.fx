# The five aggregates over a range: empty and label cells are SKIPPED
grid 3 10
A1 = 1
A2 = 2
A3 = "text"
A4 = 4
A6 = 10.5
B1 = 5
B2 = -3
? SUM(A1:A6)
? MIN(A1:A6)
? MAX(A1:A6)
? AVG(A1:A6)
? COUNT(A1:A6)
? SUM(A9:A10)
? AVG(A9:A10)
? MIN(A9:A10)
? COUNT(A9:A10)
? SUM(A1:B2)
? MAX(B1:B2)
? SUM(A1:A6)+SUM(B1:B2)
