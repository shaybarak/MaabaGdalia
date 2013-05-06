module halfadder(a,b,sum,carry);
  input a,b;
  output sum, carry;
  wire sum, carry;

  xor g0(sum, a, b);
  and g1(carry, a, b);

endmodule
