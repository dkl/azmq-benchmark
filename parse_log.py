#!/usr/bin/env python3
import sys
import re

class packet_stats:
	def __init__(self, packetspersec):
		self.packetspersec = packetspersec

def parse_stats(s):
	pattern = re.compile(".*: ([0-9]+) packets/sec, ([.0-9 a-zA-Z]+)/sec")
	match = pattern.match(s)
	if match:
		packetspersec = int(match[1])
		return packet_stats(packetspersec)

	pattern = re.compile(".*: ([0-9]+) packets or ([0-9]+) bytes in ([.0-9]+) msec \(([.0-9]+) packets/sec, ([.0-9]+) bytes/sec\)")
	match = pattern.match(s)
	assert match

	packets = int(match[1])
	bytes_ = int(match[2])
	msec = float(match[3])
	packetspersec = float(match[4])
	bytespersec = float(match[5])
	#print('{} .... {}, {}, {}, {}, {}'.format(match[0], match[1], match[2], match[3], match[4], match[5]))

	return packet_stats(packetspersec)

def parse_buffer_size(s):
	pattern = re.compile("buffer size: ([0-9]+)")
	match = pattern.match(s)
	assert match
	return int(match[1])

class per_run_stats:
	def __init__(self, name):
		self.name = name
		self.send = packet_stats(0)
		self.recv = packet_stats(0)

def prettify_name(name):
	if "6675a0a" in name:
		return "original"
	if "1a53ed3" in name:
		return "leakfix"
	if "15a4f45" in name:
		return "leakfix+refactor"
	return name

class per_commit_stats:
	def __init__(self, name):
		self.prettyname = prettify_name(name)
		self.name = name
		self.buffersize = 0
		self.runs = []

	def calc_average_recv_packets(self):
		total = 0
		for run in self.runs:
			total += run.recv.packetspersec
		return total / len(self.runs)

	def calc_average_send_packets(self):
		total = 0
		for run in self.runs:
			total += run.send.packetspersec
		return total / len(self.runs)

logfile = sys.argv[1]
commits = []
with open(logfile, 'r') as f:
	for line in f.readlines():
		line = line.strip()
		if line.startswith('testing commit'):
			commits.append(per_commit_stats(line))
		elif line.startswith('buffer size'):
			assert commits[-1].buffersize == 0
			commits[-1].buffersize = parse_buffer_size(line)
		elif line.startswith('run'):
			commits[-1].runs.append(per_run_stats(line))
		elif 'total' in line:
			if line.startswith('recv'):
				commits[-1].runs[-1].recv = parse_stats(line)
			elif line.startswith('send'):
				commits[-1].runs[-1].send = parse_stats(line)

for commit in commits:
	print('{}, buffer size {}'.format(commit.name, commit.buffersize))
	print('    name, send packets/sec, recv packets/sec')
	for run in commit.runs:
		print('    {}, {}, {}'.format(run.name, run.send.packetspersec, run.recv.packetspersec))

print()
for commit in commits:
	print('{:40.40} ... average packets/sec: buffer size {}, send {}, recv {}'.format(commit.name, commit.buffersize, commit.calc_average_send_packets(), commit.calc_average_recv_packets()))

print()
print("commit,buffersize,average send packets/sec,average recv packets/sec")
for commit in commits:
	print('{},{},{},{}'.format(commit.prettyname, commit.buffersize, commit.calc_average_send_packets(), commit.calc_average_recv_packets()))
