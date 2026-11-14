PROGRAM LZ77;
{ LZ77 compression (Lempel, Ziv, 1977) of the text file LZDATA.TXT.
  A token is a literal byte or a match: the offset back (1..255) and the
  length (3..18) of the longest match in the window behind. The packed size
  is counted in bits, as a real packer would write it: a literal takes
  1 + 8 bits, a match 1 + 8 + 4 bits. The tokens are kept in integer arrays
  (the Char of OMSI Pascal is 7-bit). The program packs the text, unpacks it
  again and compares the result with the original. Line ends are kept as #10. }
CONST MAXN = 4000;
	WINDOW = 255;
	MINLEN = 3;
	MAXLEN = 18;
VAR	F: TEXT;
	BUF, RES: ARRAY[1..MAXN] OF CHAR;
	TOFF, TVAL: ARRAY[1..MAXN] OF INTEGER;
	N, NTOK, NLIT, NMATCH, BITS, POS, I, K, BEST, BESTOFF, START, V, ERRORS: INTEGER;
	CH: CHAR;

BEGIN
	{ The text into BUF, a line end as #10 }
	ASSIGN(F, 'LZDATA.TXT'); RESET(F); V := 1;
	N := 0;
	WHILE NOT EOF(F) DO
	BEGIN
		WHILE NOT EOLN(F) DO
		BEGIN
			READ(F, CH);
			N := N + 1;
			BUF[N] := CH
		END;
		READLN(F);
		N := N + 1;
		BUF[N] := CHR(10)
	END;
	CLOSE(F);

	{ Packing: the longest match in the window behind POS }
	NTOK := 0;
	NLIT := 0;
	NMATCH := 0;
	POS := 1;
	WHILE POS <= N DO
	BEGIN
		BEST := 0;
		BESTOFF := 0;
		START := POS - WINDOW;
		IF START < 1 THEN START := 1;
		I := POS - 1;
		WHILE I >= START DO
		BEGIN
			K := 0;
			WHILE (K < MAXLEN) AND (POS + K <= N) AND (BUF[I + K] = BUF[POS + K]) DO
				K := K + 1;
			IF K > BEST THEN
			BEGIN
				BEST := K;
				BESTOFF := POS - I
			END;
			I := I - 1
		END;
		NTOK := NTOK + 1;
		IF BEST >= MINLEN THEN
		BEGIN
			TOFF[NTOK] := BESTOFF;
			TVAL[NTOK] := BEST;
			NMATCH := NMATCH + 1;
			POS := POS + BEST
		END
		ELSE
		BEGIN
			TOFF[NTOK] := 0;
			TVAL[NTOK] := ORD(BUF[POS]);
			NLIT := NLIT + 1;
			POS := POS + 1
		END
	END;
	BITS := NLIT * 9 + NMATCH * 13;

	{ Unpacking into RES }
	K := 0;
	FOR I := 1 TO NTOK DO
		IF TOFF[I] = 0 THEN
		BEGIN
			K := K + 1;
			RES[K] := CHR(TVAL[I])
		END
		ELSE
			FOR POS := 1 TO TVAL[I] DO
			BEGIN
				K := K + 1;
				RES[K] := RES[K - TOFF[I]]
			END;

	ERRORS := 0;
	IF K <> N THEN ERRORS := 1;
	FOR I := 1 TO N DO
		IF RES[I] <> BUF[I] THEN ERRORS := ERRORS + 1;

	WRITE('INPUT BYTES ');
	WRITELN(N:6);
	WRITE('LITERALS    ');
	WRITELN(NLIT:6);
	WRITE('MATCHES     ');
	WRITELN(NMATCH:6);
	WRITE('PACKED BYTES');
	WRITELN((BITS + 7) DIV 8:6);
	WRITE('PACKED, %   ');
	WRITELN(((BITS + 7) DIV 8) DIV (N DIV 100):6);
	WRITE('ERRORS      ');
	WRITELN(ERRORS:6)
END.
