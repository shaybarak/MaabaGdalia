module main;
   reg in, clk, reset;
   wire out;

   // FILL HERE

   always #5 clk = ~clk;

   always @(negedge clk)
     begin
	if (!reset)
          $display("time %d: in %b, out %b", $time, in, out);
	in = ($random / 16) % 2;
     end
   
   initial
     begin
        $dumpfile("waves.vcd");
        $dumpvars;
        clk = 0;
	in = 0;
        reset = 1;
        #10
	reset = 0;
        #300;
	@(negedge clk);
	#1;
	if (out == 1)
	  $display("ACCEPT");
        $finish;
     end // initial begin
endmodule
