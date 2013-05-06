module parity(clk, in, reset, out);

   input clk, in, reset;
   output out;

   reg 	  out;
   reg 	  state;

   parameter zero=0, one=1;

   always @(posedge clk)
     begin
	if (reset)
	  state <= zero;
	else
	  case (state)
	    1'b0: state <= in;
	    1'b1: state <= ~in;
	  endcase // case (state)
     end

   always @(state)
     begin
	out = state;	
     end

endmodule
