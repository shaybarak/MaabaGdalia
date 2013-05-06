module main;
   reg a,b,select;
   wire result1, result2;
   mux1 mux1(a,b,select,result1);
   mux2 mux2(a,b,select,result2);

   initial
     begin
	$monitor("time=%d: a %b, b %b, select %b, mux1 %b, mux2 %b\n", $time, a, b, select, result1, result2);
	$dumpfile("waves.vcd");
	$dumpvars;
          a = 0; b = 0; select = 0;
	#5
	  a = 0; b = 0; select = 1;
	#5
	  a = 0; b = 1; select = 0;
	#5
	  a = 0; b = 1; select = 1;
	#5
	  a = 1; b = 0; select = 0;
	#5
	  a = 1; b = 0; select = 1;
	#5
	  a = 1; b = 1; select = 0;
	#5
	  a = 1; b = 1; select = 1;
	#5;
	$finish;
     end
endmodule
