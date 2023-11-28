all:
	@clear
	@gcc -Wall -static -o mpc_trajectory mpc_trajectory.c -lqpSWIFT -lm
	@-./mpc_trajectory
