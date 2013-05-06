module mux1(a,b,select,result);
   input a;
   input b;
   input select;
   output result;

   assign result = (a & ~select) | (b & select);
endmodule

module mux2(a,b,select,result);
   input a;
   input b;
   input select;
   output result;

   reg result;
   always @(a or b or select)
     begin
        result = (a & ~select) | (b & select);
     end
endmodule
