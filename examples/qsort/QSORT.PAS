PROGRAM QSORT;
{ QuickSort (C. A. R. Hoare) of 1000 pseudo-random numbers.
  The numbers come from a linear congruential generator X := (21 * X + 7) mod 1024
  with the full period: all 1000 numbers are different.
  After sorting the order is checked, then the first and the last numbers are printed. }
CONST N = 1000;
VAR	A: ARRAY[1..N] OF INTEGER;
	I, X, ERRORS: INTEGER;

PROCEDURE SORT(L, R: INTEGER);
VAR	I, J, P, T: INTEGER;
BEGIN
	I := L;
	J := R;
	P := A[(L + R) DIV 2];
	REPEAT
		WHILE A[I] < P DO I := I + 1;
		WHILE P < A[J] DO J := J - 1;
		IF I <= J THEN
		BEGIN
			T := A[I];
			A[I] := A[J];
			A[J] := T;
			I := I + 1;
			J := J - 1
		END
	UNTIL I > J;
	IF L < J THEN SORT(L, J);
	IF I < R THEN SORT(I, R)
END;

BEGIN
	X := 1;
	FOR I := 1 TO N DO
	BEGIN
		X := (21 * X + 7) MOD 1024;
		A[I] := X
	END;
	WRITE('QUICKSORT OF');
	WRITE(N:5);
	WRITELN(' NUMBERS');
	SORT(1, N);
	ERRORS := 0;
	FOR I := 2 TO N DO
		IF A[I - 1] > A[I] THEN ERRORS := ERRORS + 1;
	WRITE('ERRORS');
	WRITELN(ERRORS:5);
	WRITE('FIRST');
	FOR I := 1 TO 10 DO WRITE(A[I]:5);
	WRITELN;
	WRITE('LAST ');
	FOR I := N - 9 TO N DO WRITE(A[I]:5);
	WRITELN
END.
