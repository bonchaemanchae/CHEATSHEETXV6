	.text

	/*
         * save the old thread's registers,
         * restore the new thread's registers.
         */

	.globl thread_switch
thread_switch:
	# a0 = old context ptr, a1 = new context ptr

	# save old
	# xv6 64bit address 8bytes, a0 is first parameter of a function call
	# only till s11
	# sd is save to destination, so 'sd ra' is saving the value of return address(register) to the beginning of a0
	# thread switching; saving current thread, loading next thread
	# registers contains address to instructions, and store in a0
	sd ra, 0(a0) 
	sd sp, 8(a0) # saving sp to a0 + 8bytes
	sd s0, 16(a0)
	sd s1, 24(a0)
	sd s2, 32(a0)
	sd s3, 40(a0)
	sd s4, 48(a0)
	sd s5, 56(a0)
	sd s6, 64(a0)
	sd s7, 72(a0)
	sd s8, 80(a0)
	sd s9, 88(a0)
	sd s10, 96(a0)
	sd s11, 104(a0)

	# restore 
	# loading instructions from a1 (nxt thread) to registers
	ld ra, 0(a1) # load the value in a1 to ra, ra = next instru to run
	ld sp, 8(a1)
	ld s0, 16(a1)
	ld s1, 24(a1)
	ld s2, 32(a1)
	ld s3, 40(a1)
	ld s4, 48(a1)
	ld s5, 56(a1)
	ld s6, 64(a1)
	ld s7, 72(a1)
	ld s8, 80(a1)
	ld s9, 88(a1)
	ld s10, 96(a1)
	ld s11, 104(a1)

	ret    /* return to ra */
