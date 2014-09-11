# OpenMP Parallel Kcore

Yet another way of computing K-Cores

## What this is

This yet another way of computing K-Cores in a graph.
The program takes a "graph file" as input, and then performs a parallel decomposition using the algorithm documented here:

    http://www.computer.org/csdl/trans/td/2013/02/ttd2013020288-abs.html

For those that don't want to read it, the basic idea is that core values "converge" over a period of time.
This would be better implemented in something like Giraph or GraphX, but I already did that.
A graph file takes the form of "vertex_id neighbor_id neighbor_id2 neighbor_id3", etc.
When running this, you need to specify the highest vertex id in the graph as a command line argument, an array is allocated of that size.

## Warning

This is academic code, meaning that it works really well in a controlled environment.
I wrote it to quickly generate K-Cores for smaller (< 500k) node graphs.
I think there's also a memory leak, I'll clean that up sometime.
