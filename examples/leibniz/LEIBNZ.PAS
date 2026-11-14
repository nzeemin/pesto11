PROGRAM LEIBNZ;
{ Calculation of Pi by the Leibniz series: Pi/4 = 1 - 1/3 + 1/5 - 1/7 + ...
  The partial sums are printed every 1000 terms, the last line also shows
  the average of two neighbouring sums, which is much closer to Pi. }
CONST NTERMS = 10000;
	STEP = 1000;
VAR	I, K: INTEGER;
	S, SIGN, PREV, AVG: REAL;

FUNCTION TERM(N: INTEGER): REAL;
BEGIN
	TERM := 1.0 / (2 * N + 1)
END;

BEGIN
	WRITELN('PI BY THE LEIBNIZ SERIES');
	S := 0.0;
	SIGN := 1.0;
	PREV := 0.0;
	K := 0;
	FOR I := 0 TO NTERMS - 1 DO
	BEGIN
		PREV := S;
		S := S + SIGN * TERM(I);
		SIGN := -SIGN;
		K := K + 1;
		IF K = STEP THEN
		BEGIN
			K := 0;
			WRITE(I + 1:6);
			WRITELN(4.0 * S:12:7)
		END
	END;
	AVG := 2.0 * (S + PREV);
	WRITE('AVERAGE ');
	WRITELN(AVG:12:7)
END.
