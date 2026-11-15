PROGRAM MANDEL;
{ The Mandelbrot set in text: 21 rows of 30 points, up to 8 iterations,
  a point is printed as the character 40 - N. After the vibasc test 2010-mandelbrot. }
CONST	ROWS = 21;
	COLUMNS = 30;
	MAXITER = 8;
VAR	X, Y, N: INTEGER;
	CR, CI, ZR, ZI, R2, I2, VR, VI, ZOOM, SCALE, SQ2: REAL;
BEGIN
	SCALE := 4.0; SQ2 := 4.0;
	VR := -2.3 * SCALE; VI := -1.0 * SCALE; ZOOM := 0.1 * SCALE;
	FOR Y := 0 TO ROWS - 1 DO
	BEGIN
		CI := VI + Y * ZOOM;
		FOR X := 0 TO COLUMNS - 1 DO
		BEGIN
			CR := VR + X * ZOOM;
			ZR := CR; ZI := CI;
			N := 0;
			R2 := ZR * ZR / SQ2; I2 := ZI * ZI / SQ2;
			WHILE (N < MAXITER) AND (R2 + I2 <= 4.0 * SCALE) DO
			BEGIN
				ZI := 2.0 * ZR * ZI / SQ2 + CI;
				ZR := R2 - I2 + CR;
				R2 := ZR * ZR / SQ2; I2 := ZI * ZI / SQ2;
				N := N + 1
			END;
			WRITE(CHR(40 - N))
		END;
		WRITELN
	END
END.
