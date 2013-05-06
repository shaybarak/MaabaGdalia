module fulladder( sum, co, a, b, ci);

  input   a, b, ci;
  output  sum, co;
  wire    w1, w2, w3;

  xor g0(w1, a, b);
  xor g1(sum, w1, ci);
  and g2(w2, w1, ci);
  and g3(w3, a, b);
  or  g4(co, w2, w3);

endmodule
