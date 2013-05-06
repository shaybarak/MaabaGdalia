module div4(clk, in, reset, out);

   input clk, in, reset;
   output    out;
   reg    out;
   reg[1:0]  state;
   parameter zero=0, one=1;

   always @(posedge clk)
     begin
	if (reset)
	  state <= 0;
	else
	  begin
	     state[1] <= state[0];
	     state[0] <= in;
	  end	
     end

   always @(state)
     begin
	out = (state == 0);	
     end

endmodule
