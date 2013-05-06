module main;
reg a,b;
wire sum, carry;
halfadder halfadder(a, b, sum, carry);

always@(sum or carry)
begin
$display("time=%d:%b + %b = %b, carry = %b\n", $time, a, b, sum, carry);
end

initial
begin
$dumpfile("waves.vcd");
$dumpvars;
a = 0; b = 0;
#5
a = 0; b = 1;
#5
a = 1; b = 0;
#5
a = 1; b = 1;
#5
$finish;
end
endmodule
