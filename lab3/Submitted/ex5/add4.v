module  add4( sum, co, a, b, ci);

  input   [3:0] a, b;
  input   ci;
  output  [3:0] sum;
  output  co;

  wire co0, co1, co2;

  fulladder fa0(sum[0], co0, a[0], b[0], ci);
  fulladder fa1(sum[1], co1, a[1], b[1], co0);
  fulladder fa2(sum[2], co2, a[2], b[2], co1);
  fulladder fa3(sum[3], co,  a[3], b[3], co2);
endmodule
