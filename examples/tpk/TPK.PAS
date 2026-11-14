PROGRAM TPK;
{ The TPK algorithm of Luis Trabb Pardo and Donald Knuth ("The early development
  of programming languages", 1976): read 11 numbers, then in the reverse order
  compute f(t) = sqrt(abs(t)) + 5 * t^3 and print it, or a message when it is
  too large. A small program the early languages were compared on. }
VAR	A: ARRAY[0..10] OF REAL;
	I: INTEGER;
	Y: REAL;

FUNCTION F(T: REAL): REAL;
BEGIN
	F := SQRT(ABS(T)) + 5.0 * T * T * T
END;

BEGIN
	WRITELN('ENTER 11 NUMBERS, ONE PER LINE:');
	FOR I := 0 TO 10 DO
		READLN(A[I]);
	FOR I := 10 DOWNTO 0 DO
	BEGIN
		Y := F(A[I]);
		WRITE(I:3);
		IF Y > 400.0 THEN
			WRITELN(' TOO LARGE')
		ELSE
			WRITELN(Y:14:4)
	END
END.
