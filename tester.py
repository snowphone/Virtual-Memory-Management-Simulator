#!/usr/bin/python3

import os, sys, subprocess, random, itertools


trace_path = "../mtraces/"
trace_list = [trace_path + trace for trace in os.listdir(trace_path)]

trace_args = []
for i in range(1, 4):
	trace_args += list(map(list, itertools.product(trace_list, repeat=i)))


i = 1
while True:
	firstLvBits = random.randint(1, 19)
	phyMem = random.randint(12, 32)
	traces = random.choice(trace_args)

	args = [str(firstLvBits), str(phyMem)] + traces
	answer = subprocess.check_output(["./memsim"] + args)
	mine = subprocess.check_output(["./memsimhw"] + args)

	if answer == mine:
		print(i,[firstLvBits, phyMem]+traces, "PASSED")
	else:
		raise "Failed"

	i += 1

