module	addsub_tb;
	
	// for DUT port connection
	reg					mode;
	reg 	[3:0]	operand_a, operand_b;
	wire	[3:0] result;
	wire co;
	
	// for pattern generation
	integer			a, b, expect, error_num, t;
	reg					overflow;
	wire	[3:0]	expect_out = expect[3:0];	// compare "expect_out" with "result"

	// FILL here

	// give stimulus pattern
	initial begin
	error_num = 0;

	$dumpfile("waves.vcd");
	$dumpvars;

	$display( "\nTest subtraction mode." );
	#10 mode = 1'b1;
	
	for( a=-8; a<=7; a=a+1 ) begin
		for( b=-8; b<=7; b=b+1 ) begin
			
			operand_a = a[3:0];
			operand_b = b[3:0];
			expect = a - b;
	
			#1;
			if( expect >= -8 && expect <= 7 ) begin
				overflow = 1'b0;
				if( expect[3:0] !== result ) begin
					error_num = error_num + 1;
					$display( "Error(%d):(%d)-(%d) Expect(%b) Output(%b)", error_num[9:0], a, b, expect[3:0], result );
				end
			end	else begin
				overflow = 1'b1;
			end
	
			#1;
		end
	end

	$display( "\nTest addition mode." );
	mode = 1'b0;
	
	for( a=-8; a<=7; a=a+1 ) begin
		for( b=-8; b<=7; b=b+1 ) begin
			
			operand_a = a[3:0];
			operand_b = b[3:0];
			expect = a + b;
	
			#1;
			if( expect >= -8 && expect <= 7 ) begin
				overflow = 1'b0;
				if( expect[3:0] !== result ) begin
					error_num = error_num + 1;
					$display( "Error(%d):(%d)+(%d) Expect(%b) Output(%b)", error_num[9:0], a, b, expect[3:0], result );
				end
			end	else begin
				overflow = 1'b1;
			end
	
			#1;
		end
	end
	
	if( error_num==0 )
		$display( "\nPass all test\n" );
	else
		$display( "\nFail, error number = %d\n", error_num );
	
	#10 $finish;
	end

endmodule
