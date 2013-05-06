module main;
reg a, b, ci;
wire sum, co;
fulladder fulladder(sum, co, a, b, ci);

initial
begin
$monitor("time=%d:%b + %b + %b = %b, carry = %b\n", $time, a, b, ci, sum, co);
$dumpfile("waves.vcd");
$dumpvars;
a = 0; b = 0; ci = 0;
#5
a = 0; b = 1; ci = 0;
#5
a = 1; b = 0; ci = 0;
#5
a = 1; b = 1; ci = 0;
#5
a = 0; b = 0; ci = 1;
#5
a = 0; b = 1; ci = 1;
#5
a = 1; b = 0; ci = 1;
#5
a = 1; b = 1; ci = 1;
#5
$finish;
end
endmodule
